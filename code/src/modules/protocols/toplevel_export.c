/* hyprland-toplevel-export-v1: per-window frame capture, hand-implemented
 * (wlroots has no wlr_* wrapper for this — it's not a wlroots-blessed
 * protocol, unlike wlr-foreign-toplevel-management-v1 below).
 *
 * Why this protocol: Quickshell's ScreencopyView, when bound to a Toplevel
 * capture source (used by the taskbar hover-preview and the Overview grid's
 * thumbnails), is hard-locked to this exact protocol client-side — confirmed
 * by disassembling its compiled dispatcher, not just reading docs. The
 * standard ext-image-copy-capture-v1/ext-foreign-toplevel-image-capture-
 * source-v1 pair exists in Quickshell's binary but is dead code for the
 * Toplevel case (only ever used for whole-output capture), so implementing
 * that instead would produce zero visible change.
 *
 * Wire model mirrors wlr-screencopy-v1 (buffer -> buffer_done -> client
 * sends copy(buffer) -> flags+damage+ready, or failed). It keys off
 * `zwlr_foreign_toplevel_handle_v1` — the same handle type
 * modules/foreign_toplevel.c already creates per-Client (ftl_create() sets
 * c->foreign_toplevel->data = c), so no new toplevel-listing protocol is
 * needed, just a way to resolve a capture request's handle argument back to
 * a Client*.
 *
 * Source side: c's own current committed texture, straight from
 * wlr_surface_get_texture() — not a render of the on-canvas scene region
 * (an earlier version of this file rendered through a scratch headless
 * output + wlr_scene_output positioned over the shared scene, mirroring
 * capture.c's whole-screen screenshot technique). Two reasons for the
 * change:
 *   - Perf: the scene-render approach cost a full scene render *plus* a
 *     second render pass to blit the result into the destination buffer,
 *     every single request. Reading the surface's already-rendered texture
 *     directly needs at most one blit.
 *   - Correctness for minimized windows: setminimized() (dwl.c) disables
 *     the client's scene node (wlr_scene_node_set_enabled) so it doesn't
 *     take up screen space, which means a scene-graph render of a minimized
 *     window's region produces nothing. wlr_surface_get_texture() reads the
 *     surface's last-committed content directly, bypassing the scene graph
 *     entirely, so a minimized (or otherwise scene-hidden) client's last
 *     frame is still capturable — the whole point of a "peek at a
 *     minimized window" feature.
 * Trade-off: this captures the client's raw surface content only, not
 * kalin-wm's decorations (borders/focus ring) or any subsurfaces
 * positioned outside the main surface — acceptable for a small thumbnail,
 * and arguably preferable (decorations scaled down to thumbnail size read
 * poorly anyway).
 *
 * Destination side: two paths, because they turned out not to be
 * interchangeable (see render_client_to_buffer()'s own comment) — a GPU
 * render-pass blit when Quickshell's buffer is dmabuf-backed (the common
 * and fast case), or a CPU nearest-neighbor scale-and-copy when it's only
 * CPU-accessible (shm), since wlr_texture_read_pixels() has no scaling of
 * its own and the source texture's native size essentially never matches
 * the destination exactly (c->geom includes kalin-wm's border, the raw
 * surface doesn't).
 *
 * Separately-compiled TU: links against dwl.c's externed globals (dpy, drw)
 * via kalin.h. */
#include "kalin.h"
#include "hyprland-toplevel-export-v1-protocol.h"

#include <wlr/render/wlr_texture.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/pass.h>
#include <wlr/render/dmabuf.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/log.h>

#include <stdlib.h>
#include <time.h>

typedef struct {
	struct wl_resource *resource;
	Client *client;             /* NULL once the window's gone — see notify_handle_destroy */
	struct wl_listener handle_destroy;
	int listening;              /* whether handle_destroy is actually linked */
} ExportFrame;

/* Client's current committed texture — owned by the surface, caller must
 * NOT destroy it. See the file header for why this reads the surface
 * directly instead of rendering the scene.
 *
 * c->surface.xdg is set once at Client creation and never cleared today, so
 * this NULL check isn't reachable right now — but every other dereference in
 * this file (frame->client, frame->client->mon) is guarded, and this was the
 * one that wasn't, which is exactly the pattern that bit this file twice
 * already this session (the null-captureSource crashes) before it was
 * caught. Cheap to guard, and future-proofs against a refactor that starts
 * invalidating surface.xdg on unmap, the way other Client fields already
 * are. */
static struct wlr_texture *
client_surface_texture(Client *c)
{
	struct wlr_texture *tex;

	if (!c->surface.xdg || !c->surface.xdg->surface)
		return NULL;

	tex = wlr_surface_get_texture(c->surface.xdg->surface);
	if (!tex)
		wlr_log(WLR_ERROR, "toplevel_export: client has no committed buffer yet");
	return tex;
}

/* CPU/shm destination path: wlr_texture_read_pixels() has no scaling, so
 * read tex at its native size into a temporary buffer (in dst's own pixel
 * format, so the per-pixel copy below needs no conversion) and
 * nearest-neighbor scale that into dst_buffer's mapped memory. Assumes a
 * 4-byte-per-pixel dst format — true for every ARGB/XRGB8888-family format
 * actually seen from Quickshell/Qt's shm path here. */
static int
read_and_scale_to_cpu_buffer(struct wlr_texture *tex, struct wlr_buffer *dst_buffer)
{
	void *dst_data;
	uint32_t dst_format;
	size_t dst_stride;
	uint32_t *src_pixels;
	int sw = (int)tex->width, sh = (int)tex->height;
	int dw = dst_buffer->width, dh = dst_buffer->height;
	int ok = 0;

	if (sw <= 0 || sh <= 0)
		return 0;
	if (!wlr_buffer_begin_data_ptr_access(dst_buffer, WLR_BUFFER_DATA_PTR_ACCESS_WRITE,
			&dst_data, &dst_format, &dst_stride))
		return 0;

	src_pixels = malloc((size_t)sw * (size_t)sh * 4);
	if (src_pixels) {
		/* wlr_texture_read_pixels_options has a const member (src_box), so
		 * it must be initialized at declaration, not assigned to later. */
		struct wlr_texture_read_pixels_options opts = {
			.data = src_pixels,
			.format = dst_format,
			.stride = (uint32_t)sw * 4,
		};
		if (wlr_texture_read_pixels(tex, &opts)) {
			int x, y;
			for (y = 0; y < dh; y++) {
				int sy = sh == dh ? y : y * sh / dh;
				uint32_t *drow = (uint32_t *)((uint8_t *)dst_data + y * dst_stride);
				uint32_t *srow = src_pixels + (size_t)sy * sw;
				for (x = 0; x < dw; x++)
					drow[x] = srow[sw == dw ? x : x * sw / dw];
			}
			ok = 1;
		}
		free(src_pixels);
	}

	wlr_buffer_end_data_ptr_access(dst_buffer);
	return ok;
}

/* Copy c's content into dst_buffer, a client-supplied buffer already
 * imported via wlr_buffer_try_from_resource(). Two destination paths,
 * because they turned out not to be interchangeable:
 *
 *  - dmabuf-backed: a GPU render-pass blit (wlr_renderer_begin_buffer_pass),
 *    which scales via the destination box regardless of source/dest size
 *    mismatch. Confirmed necessary on both the test VM and real hardware —
 *    Quickshell submits a dmabuf-backed wl_buffer to copy() regardless of
 *    what this protocol advertises (it never gets a linux_dmabuf event
 *    here).
 *  - anything else that supports direct CPU access (shm-backed buffers, and
 *    possibly others): wlroots' GL renderer can't target a plain CPU-mapped
 *    buffer with a render pass at all — falls back to
 *    read_and_scale_to_cpu_buffer() above. Uses the generic
 *    wlr_buffer_begin_data_ptr_access() rather than the wl_shm-specific
 *    wl_shm_buffer_get(): a buffer that imports successfully via
 *    wlr_buffer_try_from_resource() but isn't recognized by *either*
 *    wlr_buffer_get_dmabuf() nor wl_shm_buffer_get() was observed in
 *    practice (some other wlr_buffer-backed type), and the generic accessor
 *    covers that case too. */
static int
render_client_to_buffer(Client *c, struct wlr_buffer *dst_buffer)
{
	struct wlr_texture *tex;
	struct wlr_dmabuf_attributes dmabuf_attrs;
	int ok = 0;

	/* dst_buffer is allocated by the client (Quickshell) based on whatever
	 * size it last saw this toplevel report — if the window has since been
	 * resized (e.g. by fitwidth()/fitheight(), or a plain drag-resize) and
	 * a stale, smaller buffer arrives before Quickshell catches up, this
	 * refuses the mismatched frame instead of scaling into a buffer sized
	 * for stale geometry; the client will ask again with a correctly-sized
	 * buffer once it processes the toplevel's new size. */
	if (dst_buffer->width != c->geom.width || dst_buffer->height != c->geom.height) {
		wlr_log(WLR_ERROR, "toplevel_export: dst buffer %dx%d doesn't match "
				"current client size %dx%d, refusing (stale size?)",
				dst_buffer->width, dst_buffer->height, c->geom.width, c->geom.height);
		return 0;
	}

	tex = client_surface_texture(c);
	if (!tex)
		return 0;

	if (wlr_buffer_get_dmabuf(dst_buffer, &dmabuf_attrs)) {
		struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(drw, dst_buffer, NULL);
		if (pass) {
			struct wlr_render_texture_options opts = {
				.texture = tex,
				.dst_box = { .width = c->geom.width, .height = c->geom.height },
			};
			wlr_render_pass_add_texture(pass, &opts);
			ok = wlr_render_pass_submit(pass);
		} else {
			wlr_log(WLR_ERROR, "toplevel_export: wlr_renderer_begin_buffer_pass failed");
		}
	} else if (!(ok = read_and_scale_to_cpu_buffer(tex, dst_buffer))) {
		wlr_log(WLR_ERROR, "toplevel_export: dst buffer supports neither "
				"dmabuf render nor direct CPU access");
	}

	/* tex is owned by the surface (client_surface_texture()) — do not destroy. */
	return ok;
}

/* ── frame interface ────────────────────────────────────────────────────── */

static void
notify_handle_destroy(struct wl_listener *listener, void *data)
{
	ExportFrame *frame = wl_container_of(listener, frame, handle_destroy);
	(void)data;
	frame->client = NULL;
	wl_list_remove(&frame->handle_destroy.link);
	frame->listening = 0;
}

static void
frame_copy(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *buffer_resource, int32_t ignore_damage)
{
	ExportFrame *frame = wl_resource_get_user_data(resource);
	struct wlr_buffer *dst_buffer;
	struct timespec now;
	uint64_t sec;
	(void)client;

	if (!frame->client || !frame->client->mon) {
		hyprland_toplevel_export_frame_v1_send_failed(resource);
		return;
	}

	/* Imports either an shm- or a dmabuf-backed client buffer uniformly;
	 * render_client_to_buffer() branches on which one it actually got. */
	dst_buffer = wlr_buffer_try_from_resource(buffer_resource);
	if (!dst_buffer) {
		wlr_log(WLR_ERROR, "toplevel_export: copy() buffer isn't importable (class=%s)",
				wl_resource_get_class(buffer_resource));
		hyprland_toplevel_export_frame_v1_send_failed(resource);
		return;
	}

	if (!render_client_to_buffer(frame->client, dst_buffer)) {
		wlr_buffer_unlock(dst_buffer);
		hyprland_toplevel_export_frame_v1_send_failed(resource);
		return;
	}
	wlr_buffer_unlock(dst_buffer);

	if (!ignore_damage) {
		hyprland_toplevel_export_frame_v1_send_damage(resource, 0, 0,
				(uint32_t)frame->client->geom.width, (uint32_t)frame->client->geom.height);
	}
	hyprland_toplevel_export_frame_v1_send_flags(resource, 0);

	clock_gettime(CLOCK_MONOTONIC, &now);
	sec = (uint64_t)now.tv_sec;
	hyprland_toplevel_export_frame_v1_send_ready(resource,
			(uint32_t)(sec >> 32), (uint32_t)(sec & 0xffffffffu), (uint32_t)now.tv_nsec);
}

static void
frame_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	wl_resource_destroy(resource);
}

static const struct hyprland_toplevel_export_frame_v1_interface frame_impl = {
	.copy = frame_copy,
	.destroy = frame_destroy_request,
};

static void
frame_resource_destroy(struct wl_resource *resource)
{
	ExportFrame *frame = wl_resource_get_user_data(resource);
	if (frame->listening)
		wl_list_remove(&frame->handle_destroy.link);
	free(frame);
}

/* ── manager interface ──────────────────────────────────────────────────── */

static void
manager_capture_toplevel(struct wl_client *client, struct wl_resource *resource,
		uint32_t id, int32_t overlay_cursor, uint32_t handle)
{
	/* Version-1 request, keyed by a raw address-like handle (as printed by
	 * `hyprctl clients`) rather than an actual protocol object — kalin-wm
	 * has no such registry, and Quickshell (confirmed by disassembly) only
	 * ever calls the version-2 wlr-toplevel-handle variant below. Create the
	 * frame so the client gets a valid object per the protocol's object
	 * lifecycle, then fail it immediately. */
	struct wl_resource *frame_resource;
	ExportFrame *frame;
	(void)overlay_cursor;
	(void)handle;

	frame_resource = wl_resource_create(client, &hyprland_toplevel_export_frame_v1_interface,
			wl_resource_get_version(resource), id);
	if (!frame_resource) {
		wl_client_post_no_memory(client);
		return;
	}
	frame = ecalloc(1, sizeof(*frame));
	frame->resource = frame_resource;
	wl_resource_set_implementation(frame_resource, &frame_impl, frame, frame_resource_destroy);
	hyprland_toplevel_export_frame_v1_send_failed(frame_resource);
}

static void
manager_capture_toplevel_with_wlr_toplevel_handle(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, int32_t overlay_cursor,
		struct wl_resource *handle_resource)
{
	struct wlr_foreign_toplevel_handle_v1 *handle;
	struct wl_resource *frame_resource;
	ExportFrame *frame;
	(void)overlay_cursor;

	frame_resource = wl_resource_create(client, &hyprland_toplevel_export_frame_v1_interface,
			wl_resource_get_version(resource), id);
	if (!frame_resource) {
		wl_client_post_no_memory(client);
		return;
	}

	frame = ecalloc(1, sizeof(*frame));
	frame->resource = frame_resource;
	wl_resource_set_implementation(frame_resource, &frame_impl, frame, frame_resource_destroy);

	/* libwayland-server's own dispatcher already validated handle_resource's
	 * interface matches this request's declared arg type before calling us,
	 * so this is safe without an explicit wl_resource_instance_of check. */
	handle = wl_resource_get_user_data(handle_resource);
	frame->client = handle ? handle->data : NULL;

	if (!frame->client || !frame->client->mon) {
		hyprland_toplevel_export_frame_v1_send_failed(frame_resource);
		return;
	}

	frame->handle_destroy.notify = notify_handle_destroy;
	wl_signal_add(&handle->events.destroy, &frame->handle_destroy);
	frame->listening = 1;

	hyprland_toplevel_export_frame_v1_send_buffer(frame_resource, WL_SHM_FORMAT_XRGB8888,
			(uint32_t)frame->client->geom.width, (uint32_t)frame->client->geom.height,
			(uint32_t)frame->client->geom.width * 4);
	hyprland_toplevel_export_frame_v1_send_buffer_done(frame_resource);
}

static void
manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	(void)client;
	wl_resource_destroy(resource);
}

static const struct hyprland_toplevel_export_manager_v1_interface manager_impl = {
	.capture_toplevel = manager_capture_toplevel,
	.destroy = manager_destroy,
	.capture_toplevel_with_wlr_toplevel_handle = manager_capture_toplevel_with_wlr_toplevel_handle,
};

static void
manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(client,
			&hyprland_toplevel_export_manager_v1_interface, (int)version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

void
toplevel_export_init(struct wl_display *display)
{
	if (!wl_global_create(display, &hyprland_toplevel_export_manager_v1_interface,
			2, NULL, manager_bind))
		wlr_log(WLR_ERROR, "toplevel_export: wl_global_create failed");
}
