/* High-resolution screenshot: render the focused output's current view to a
 * supersampled (2x) offscreen buffer and write a PNG — a "4K screenshot on an
 * HD screen". Bound to a keybind (see config).
 *
 * Approach: a throwaway headless output at 2x the focused output's resolution,
 * with its own wlr_scene_output positioned over the same layout region, renders
 * the shared scene at 2x density (crisp because clients render high-DPI under
 * fractional-scale). We read the rendered buffer back and encode a PNG with a
 * self-contained, dependency-free writer (stored/uncompressed deflate).
 *
 * Separately-compiled TU: links against dwl.c's externed globals (event_loop,
 * alloc, drw, scene, selmon, output_layout) via kalin.h. */
#include "kalin.h"

#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* fourcc for XRGB8888 (little-endian memory order: B, G, R, X). */
#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888 0x34325258
#endif

#define CAPTURE_SUPERSAMPLE 2   /* 2x → 4K-class from a 1080p panel */

/* ── minimal PNG writer (RGB, no external deps) ───────────────────────────── */

static uint32_t
crc32_of(const unsigned char *buf, size_t len, uint32_t crc)
{
	static uint32_t table[256];
	static int have_table;
	size_t i;
	if (!have_table) {
		for (uint32_t n = 0; n < 256; n++) {
			uint32_t c = n;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
			table[n] = c;
		}
		have_table = 1;
	}
	crc ^= 0xffffffffu;
	for (i = 0; i < len; i++)
		crc = table[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
	return crc ^ 0xffffffffu;
}

static void
put_be32(FILE *f, uint32_t v)
{
	unsigned char b[4] = { v >> 24, v >> 16, v >> 8, v };
	fwrite(b, 1, 4, f);
}

static void
png_chunk(FILE *f, const char *type, const unsigned char *data, size_t len)
{
	unsigned char *tmp = malloc(4 + len);
	uint32_t crc = 0;
	put_be32(f, (uint32_t)len);
	fwrite(type, 1, 4, f);
	if (len)
		fwrite(data, 1, len, f);
	/* CRC over type + data. */
	if (tmp) {
		memcpy(tmp, type, 4);
		if (len)
			memcpy(tmp + 4, data, len);
		crc = crc32_of(tmp, 4 + len, 0);
		free(tmp);
	}
	put_be32(f, crc);
}

/* Wrap raw filtered image bytes in a zlib stream using stored (uncompressed)
 * deflate blocks — no compression library needed. */
static unsigned char *
zlib_store(const unsigned char *raw, size_t rawlen, size_t *outlen)
{
	/* zlib header (2) + per-64KB-block header (5) + data + adler32 (4). */
	size_t nblocks = (rawlen + 65534) / 65535;
	if (nblocks == 0)
		nblocks = 1;
	size_t cap = 2 + nblocks * 5 + rawlen + 4;
	unsigned char *out = malloc(cap);
	size_t o = 0, pos = 0;
	uint32_t a = 1, b = 0;
	if (!out)
		return NULL;

	out[o++] = 0x78;            /* CMF: deflate, 32K window */
	out[o++] = 0x01;            /* FLG */

	while (pos < rawlen || o == 2) {
		size_t chunk = rawlen - pos;
		if (chunk > 65535)
			chunk = 65535;
		out[o++] = (pos + chunk >= rawlen) ? 1 : 0;   /* BFINAL, BTYPE=00 */
		out[o++] = chunk & 0xff;
		out[o++] = (chunk >> 8) & 0xff;
		out[o++] = ~chunk & 0xff;
		out[o++] = (~chunk >> 8) & 0xff;
		memcpy(out + o, raw + pos, chunk);
		o += chunk;
		pos += chunk;
		if (rawlen == 0)
			break;
	}

	/* adler32 over the raw data. */
	for (size_t i = 0; i < rawlen; i++) {
		a = (a + raw[i]) % 65521;
		b = (b + a) % 65521;
	}
	uint32_t adler = (b << 16) | a;
	out[o++] = adler >> 24;
	out[o++] = adler >> 16;
	out[o++] = adler >> 8;
	out[o++] = adler;

	*outlen = o;
	return out;
}

/* Write an RGB PNG from XRGB8888 pixel data (memory order B,G,R,X). */
static int
write_png_xrgb(const char *path, const unsigned char *px, int w, int h, int stride)
{
	FILE *f = fopen(path, "wb");
	unsigned char ihdr[13];
	unsigned char *raw, *zdat;
	size_t rawlen, zlen;
	int y, x;
	if (!f)
		return 0;

	static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
	fwrite(sig, 1, 8, f);

	ihdr[0] = w >> 24; ihdr[1] = w >> 16; ihdr[2] = w >> 8; ihdr[3] = w;
	ihdr[4] = h >> 24; ihdr[5] = h >> 16; ihdr[6] = h >> 8; ihdr[7] = h;
	ihdr[8] = 8;    /* bit depth */
	ihdr[9] = 2;    /* colour type: truecolour RGB */
	ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
	png_chunk(f, "IHDR", ihdr, sizeof(ihdr));

	/* Filtered raw: each row = filter byte (0) + RGB triples. */
	rawlen = (size_t)h * (1 + (size_t)w * 3);
	raw = malloc(rawlen);
	if (!raw) { fclose(f); return 0; }
	for (y = 0; y < h; y++) {
		unsigned char *row = raw + (size_t)y * (1 + (size_t)w * 3);
		const unsigned char *src = px + (size_t)y * stride;
		*row++ = 0;
		for (x = 0; x < w; x++) {
			const unsigned char *p = src + (size_t)x * 4; /* B,G,R,X */
			*row++ = p[2];   /* R */
			*row++ = p[1];   /* G */
			*row++ = p[0];   /* B */
		}
	}

	zdat = zlib_store(raw, rawlen, &zlen);
	free(raw);
	if (!zdat) { fclose(f); return 0; }
	png_chunk(f, "IDAT", zdat, zlen);
	free(zdat);

	png_chunk(f, "IEND", NULL, 0);
	fclose(f);
	return 1;
}

/* ── capture ──────────────────────────────────────────────────────────────── */

void
capture_screenshot(const Arg *arg)
{
	Monitor *m = selmon;
	struct wlr_output *hout;
	struct wlr_scene_output *so;
	struct wlr_output_state ost;
	struct wlr_texture *tex;
	struct wlr_box mbox;
	unsigned char *data;
	int cw, ch;
	size_t stride;
	const char *dir;
	char path[512];
	time_t t;
	struct tm tmv;
	int ok;
	(void)arg;

	if (!m || !m->wlr_output || !event_loop || !alloc || !drw || !scene) {
		wlr_log(WLR_ERROR, "capture: not ready");
		return;
	}

	cw = m->wlr_output->width  * CAPTURE_SUPERSAMPLE;
	ch = m->wlr_output->height * CAPTURE_SUPERSAMPLE;
	if (cw <= 0 || ch <= 0)
		return;

	/* Throwaway headless backend + output at the supersampled resolution. */
	struct wlr_backend *hb = wlr_headless_backend_create(event_loop);
	if (!hb) {
		wlr_log(WLR_ERROR, "capture: headless backend create failed");
		return;
	}
	hout = wlr_headless_add_output(hb, cw, ch);
	if (!hout || !wlr_output_init_render(hout, alloc, drw)) {
		wlr_log(WLR_ERROR, "capture: output/init_render failed");
		wlr_backend_destroy(hb);
		return;
	}

	wlr_output_state_init(&ost);
	wlr_output_state_set_enabled(&ost, true);
	wlr_output_state_set_custom_mode(&ost, cw, ch, 0);
	wlr_output_state_set_scale(&ost, m->wlr_output->scale * CAPTURE_SUPERSAMPLE);
	if (!wlr_output_commit_state(hout, &ost)) {
		wlr_log(WLR_ERROR, "capture: output enable failed");
		wlr_output_state_finish(&ost);
		wlr_backend_destroy(hb);
		return;
	}
	wlr_output_state_finish(&ost);

	/* Render the shared scene over the focused monitor's layout region. */
	so = wlr_scene_output_create(scene, hout);
	if (!so) {
		wlr_backend_destroy(hb);
		return;
	}
	wlr_output_layout_get_box(output_layout, m->wlr_output, &mbox);
	wlr_scene_output_set_position(so, mbox.x, mbox.y);

	wlr_output_state_init(&ost);
	if (!wlr_scene_output_build_state(so, &ost, NULL) || !ost.buffer) {
		wlr_log(WLR_ERROR, "capture: scene render failed");
		wlr_output_state_finish(&ost);
		wlr_scene_output_destroy(so);
		wlr_backend_destroy(hb);
		return;
	}

	/* Read pixels back from the rendered buffer. */
	tex = wlr_texture_from_buffer(drw, ost.buffer);
	stride = (size_t)cw * 4;
	data = malloc(stride * ch);
	ok = 0;
	if (tex && data) {
		struct wlr_texture_read_pixels_options opts = {
			.data = data,
			.format = DRM_FORMAT_XRGB8888,
			.stride = (uint32_t)stride,
		};
		ok = wlr_texture_read_pixels(tex, &opts);
	}
	if (tex)
		wlr_texture_destroy(tex);
	wlr_output_state_finish(&ost);

	if (ok) {
		dir = getenv("KALIN_SHOT_DIR");
		if (!dir)
			dir = getenv("HOME");
		t = time(NULL);
		localtime_r(&t, &tmv);
		snprintf(path, sizeof(path), "%s/kalin-%04d%02d%02d-%02d%02d%02d.png",
				dir ? dir : "/tmp",
				tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
				tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
		if (write_png_xrgb(path, data, cw, ch, (int)stride))
			wlr_log(WLR_INFO, "capture: wrote %dx%d %s", cw, ch, path);
		else
			wlr_log(WLR_ERROR, "capture: PNG write failed: %s", path);
	} else {
		wlr_log(WLR_ERROR, "capture: read_pixels failed");
	}

	free(data);
	wlr_scene_output_destroy(so);
	wlr_backend_destroy(hb);
}
