/*
 * client.c - Client and window management implementation
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/util/log.h>

#include "kalin.h"
#include "client.h"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static void
printstatus(void);

static void
focus_top(Monitor *m, int lift);

/* ============================================================================
 * Client Lifecycle Management
 * ============================================================================ */

void
createnotify(struct wl_listener *listener, void *data)
{
    /* This event is raised when a client creates a new toplevel (application window). */
    struct wlr_xdg_toplevel *toplevel = data;
    Client *c = NULL;

    if (toplevel->base->data) {
        wlr_log(WLR_ERROR, "Duplicate createnotify for xdg_toplevel; keeping existing client");
        return;
    }

    /* Allocate a Client for this surface */
    c = toplevel->base->data = ecalloc(1, sizeof(*c));
    c->surface.xdg = toplevel->base;
    c->bw = borderpx;
    wl_list_init(&c->link);
    wl_list_init(&c->flink);

    LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
    LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
    LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
    LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
    LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
    LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
    LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}

void
createpopup(struct wl_listener *listener, void *data)
{
    /* This event is raised when a client (either xdg-shell or layer-shell)
     * creates a new popup. */
    struct wlr_xdg_popup *popup = data;
    LISTEN_STATIC(&popup->base->surface->events.commit, commitpopup);
}

void
destroynotify(struct wl_listener *listener, void *data)
{
    /* Called when the xdg_toplevel is destroyed. */
    Client *c = wl_container_of(listener, c, destroy);
    wl_list_remove(&c->destroy.link);
    wl_list_remove(&c->set_title.link);
    wl_list_remove(&c->fullscreen.link);
#ifdef XWAYLAND
    if (c->type != XDGShell) {
        wl_list_remove(&c->activate.link);
        wl_list_remove(&c->associate.link);
        wl_list_remove(&c->configure.link);
        wl_list_remove(&c->dissociate.link);
        wl_list_remove(&c->set_hints.link);
    } else
#endif
    {
        wl_list_remove(&c->commit.link);
        wl_list_remove(&c->map.link);
        wl_list_remove(&c->unmap.link);
        wl_list_remove(&c->maximize.link);
    }
    free(c);
}

void
unmapnotify(struct wl_listener *listener, void *data)
{
    /* Called when the surface is unmapped, and should no longer be shown. */
    Client *c = wl_container_of(listener, c, unmap);
    if (c == grabc) {
        cursor_mode = CurNormal;
        grabc = NULL;
    }

    if (client_is_unmanaged(c)) {
        if (c == exclusive_focus) {
            exclusive_focus = NULL;
            if (selmon)
                focus_top(selmon, 1);
        }
    } else {
        if (c->link.prev && c->link.next)
            wl_list_remove(&c->link);
        setmon(c, NULL, 0);
        if (c->flink.prev && c->flink.next)
            wl_list_remove(&c->flink);
    }

    wlr_scene_node_destroy(&c->scene->node);
    printstatus();
    motionnotify(0, NULL, 0, 0, 0, 0);
}

void
mapnotify(struct wl_listener *listener, void *data)
{
    /* Called when the surface is mapped, or ready to display on-screen. */
    Client *p = NULL;
    Client *w, *c = wl_container_of(listener, c, map);
    Monitor *m;
    int i;

    /* Create scene tree for this client and its border */
    c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
    if (!c->scene) {
        client_surface(c)->data = NULL;
        wlr_log(WLR_ERROR, "Failed to create scene tree for mapped client");
        return;
    }
    /* Enabled later by a call to arrange() */
    wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
    c->scene_surface = c->type == XDGShell
            ? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
            : wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
    c->scene->node.data = c->scene_surface->node.data = c;

    client_get_geometry(c, &c->geom);

    /* Handle unmanaged clients first so we can return prior create borders */
    if (client_is_unmanaged(c)) {
        /* Unmanaged clients always are floating */
        wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
        wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
        client_set_size(c, c->geom.width, c->geom.height);
        if (client_wants_focus(c)) {
            focusclient(c, 1);
            exclusive_focus = c;
        }
        goto unset_fullscreen;
    }

    for (i = 0; i < 4; i++) {
        c->border[i] = wlr_scene_rect_create(c->scene, 0, 0,
                c->isurgent ? urgentcolor : bordercolor);
        c->border[i]->node.data = c;
    }

    if (focusringpx > 0) {
        for (i = 0; i < 4; i++) {
            c->focus_ring[i] = wlr_scene_rect_create(c->scene, 0, 0, focuscolor);
            c->focus_ring[i]->node.data = c;
            wlr_scene_node_set_enabled(&c->focus_ring[i]->node, 0);
        }
    }

    /* Initialize client geometry with room for border */
    client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
    c->geom.width += 2 * c->bw;
    c->geom.height += 2 * c->bw;

    /* Insert this client into client lists. */
    wl_list_insert(&clients, &c->link);
    wl_list_insert(&fstack, &c->flink);

    /* Set initial monitor, tags, floating status, and focus:
     * we always consider floating, clients that have parent and thus
     * we set the same tags and monitor as its parent.
     * If there is no parent, apply rules */
    if ((p = client_get_parent(c))) {
        c->isfloating = 1;
        setmon(c, p->mon, p->tags);
    } else {
        applyrules(c);
    }
    printstatus();

unset_fullscreen:
    m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
    wl_list_for_each(w, &clients, link) {
        if (w != c && w != p && w->isfullscreen && m == w->mon && (w->tags & c->tags))
            setfullscreen(w, 0);
    }
}

/* ============================================================================
 * Surface Commit Handling
 * ============================================================================ */

void
commitnotify(struct wl_listener *listener, void *data)
{
    Client *c = wl_container_of(listener, c, commit);

    if (c->surface.xdg->initial_commit) {
        /*
         * Get the monitor this client will be rendered on
         * Note that if the user set a rule in which the client is placed on
         * a different monitor based on its title, this will likely select
         * a wrong monitor.
         */
        applyrules(c);
        if (c->mon) {
            client_set_scale(client_surface(c), c->mon->wlr_output->scale);
        }
        setmon(c, NULL, 0); /* Make sure to reapply rules in mapnotify() */

        wlr_xdg_toplevel_set_wm_capabilities(c->surface.xdg->toplevel,
                WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
        if (c->decoration)
            requestdecorationmode(&c->set_decoration_mode, c->decoration);
        wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
        return;
    }

    resize(c, c->geom, (c->isfloating && !c->isfullscreen));

    /* mark a pending resize as completed */
    if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
        c->resize = 0;
}

void
commitpopup(struct wl_listener *listener, void *data)
{
    struct wlr_surface *surface = data;
    struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(surface);
    LayerSurface *l = NULL;
    Client *c = NULL;
    struct wlr_box box;
    int type = -1;

    if (!popup)
        return;

    if (!popup->base->initial_commit)
        return;

    type = toplevel_from_wlr_surface(popup->base->surface, &c, &l);
    if (!popup->parent || type < 0)
        return;
    popup->base->surface->data = wlr_scene_xdg_surface_create(
            popup->parent->data, popup->base);
    if ((l && !l->mon) || (c && !c->mon)) {
        wlr_xdg_popup_destroy(popup);
        return;
    }
    box = type == LayerShell ? l->mon->m : c->mon->w;
    box.x -= (type == LayerShell ? l->scene->node.x : c->geom.x);
    box.y -= (type == LayerShell ? l->scene->node.y : c->geom.y);
    wlr_xdg_popup_unconstrain_from_box(popup, &box);
    static_listener_free(listener);
}

void
commitlayersurfacenotify(struct wl_listener *listener, void *data)
{
    LayerSurface *l = wl_container_of(listener, l, surface_commit);
    struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;
    struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->current.layer]];
    struct wlr_layer_surface_v1_state old_state;

    if (l->layer_surface->initial_commit) {
        client_set_scale(layer_surface->surface, l->mon->wlr_output->scale);

        /* Temporarily set the layer's current state to pending
         * so that we can easily arrange it */
        old_state = l->layer_surface->current;
        l->layer_surface->current = l->layer_surface->pending;
        arrangelayers(l->mon);
        l->layer_surface->current = old_state;
        return;
    }

    if (layer_surface->current.committed == 0 && l->mapped == layer_surface->surface->mapped)
        return;
    l->mapped = layer_surface->surface->mapped;

    if (scene_layer != l->scene->node.parent) {
        wlr_scene_node_reparent(&l->scene->node, scene_layer);
        wl_list_remove(&l->link);
        wl_list_insert(&l->mon->layers[layer_surface->current.layer], &l->link);
        wlr_scene_node_reparent(&l->popups->node, (layer_surface->current.layer
                < ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer));
    }

    arrangelayers(l->mon);
}

/* ============================================================================
 * XDG Decoration Handling
 * ============================================================================ */

void
createdecoration(struct wl_listener *listener, void *data)
{
    struct wlr_xdg_toplevel_decoration_v1 *deco = data;
    Client *c = deco->toplevel->base->data;
    c->decoration = deco;

    LISTEN(&deco->events.request_mode, &c->set_decoration_mode, requestdecorationmode);
    LISTEN(&deco->events.destroy, &c->destroy_decoration, destroydecoration);

    requestdecorationmode(&c->set_decoration_mode, deco);
}

void
destroydecoration(struct wl_listener *listener, void *data)
{
    Client *c = wl_container_of(listener, c, destroy_decoration);

    wl_list_remove(&c->destroy_decoration.link);
    wl_list_remove(&c->set_decoration_mode.link);
}

void
requestdecorationmode(struct wl_listener *listener, void *data)
{
    Client *c = wl_container_of(listener, c, set_decoration_mode);
    if (c->surface.xdg->initialized)
        wlr_xdg_toplevel_decoration_v1_set_mode(c->decoration,
                WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

/* ============================================================================
 * Window Arrangement and Layout
 * ============================================================================ */

void
arrange(Monitor *m)
{
    Client *c;

    if (!m->wlr_output->enabled)
        return;

    wl_list_for_each(c, &clients, link) {
        if (c->mon == m) {
            wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
            client_set_suspended(c, !VISIBLEON(c, m));
        }
    }

    wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
            (c = focustop(m)) && c->isfullscreen);

    snprintf(m->ltsymbol, sizeof(m->ltsymbol), "%s", m->lt[m->sellt]->symbol);

    /* We move all clients (except fullscreen and unmanaged) to LyrTile while
     * in floating layout to avoid "real" floating clients be always on top */
    wl_list_for_each(c, &clients, link) {
        if (c->mon != m || c->scene->node.parent == layers[LyrFS])
            continue;

        wlr_scene_node_reparent(&c->scene->node,
                (!m->lt[m->sellt]->arrange && c->isfloating)
                        ? layers[LyrTile]
                        : (m->lt[m->sellt]->arrange && c->isfloating)
                                ? layers[LyrFloat]
                                : c->scene->node.parent);
    }

    if (m->lt[m->sellt]->arrange)
        m->lt[m->sellt]->arrange(m);
    motionnotify(0, NULL, 0, 0, 0, 0);
    checkidleinhibitor(NULL);
}

void
arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive)
{
    LayerSurface *l;
    struct wlr_box full_area = m->m;

    wl_list_for_each(l, list, link) {
        struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;

        if (!layer_surface->initialized)
            continue;

        if (exclusive != (layer_surface->current.exclusive_zone > 0))
            continue;

        wlr_scene_layer_surface_v1_configure(l->scene_layer, &full_area, usable_area);
        wlr_scene_node_set_position(&l->popups->node, l->scene->node.x, l->scene->node.y);
    }
}

void
arrangelayers(Monitor *m)
{
    int i;
    struct wlr_box usable_area = m->m;
    LayerSurface *l;
    uint32_t layers_above_shell[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    };
    if (!m->wlr_output->enabled)
        return;

    /* Arrange exclusive surfaces from top->bottom */
    for (i = 3; i >= 0; i--)
        arrangelayer(m, &m->layers[i], &usable_area, 1);

    if (!wlr_box_equal(&usable_area, &m->w)) {
        m->w = usable_area;
        arrange(m);
    }

    /* Arrange non-exclusive surfaces from top->bottom */
    for (i = 3; i >= 0; i--)
        arrangelayer(m, &m->layers[i], &usable_area, 0);

    /* Find topmost keyboard interactive layer, if such a layer exists */
    for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
        wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link) {
            if (locked || !l->layer_surface->current.keyboard_interactive || !l->mapped)
                continue;
            /* Deactivate the focused client. */
            focusclient(NULL, 0);
            exclusive_focus = l;
            client_notify_enter(l->layer_surface->surface, wlr_seat_get_keyboard(seat));
            return;
        }
    }
}

void
applyrules(Client *c)
{
    /* rule matching */
    const char *appid, *title;
    uint32_t newtags = 0;
    int i;
    const Rule *r;
    Monitor *mon = selmon, *m;

    appid = client_get_appid(c);
    title = client_get_title(c);

    for (r = rules; r < END(rules); r++) {
        if ((!r->title || strstr(title, r->title))
                && (!r->id || strstr(appid, r->id))) {
            c->isfloating = r->isfloating;
            newtags |= r->tags;
            i = 0;
            wl_list_for_each(m, &mons, link) {
                if (r->monitor == i++)
                    mon = m;
            }
        }
    }

    c->isfloating |= client_is_float_type(c);
    setmon(c, mon, newtags);
}

void
applybounds(Client *c, struct wlr_box *bbox)
{
    /* set minimum possible */
    c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
    c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);

    if (c->geom.x >= bbox->x + bbox->width)
        c->geom.x = bbox->x + bbox->width - c->geom.width;
    if (c->geom.y >= bbox->y + bbox->height)
        c->geom.y = bbox->y + bbox->height - c->geom.height;
    if (c->geom.x + c->geom.width <= bbox->x)
        c->geom.x = bbox->x;
    if (c->geom.y + c->geom.height <= bbox->y)
        c->geom.y = bbox->y;
}

void
resize(Client *c, struct wlr_box geo, int interact)
{
    struct wlr_box *bbox;
    struct wlr_box clip;
    int view_x, view_y;

    if (!c->mon || !client_surface(c)->mapped)
        return;

    bbox = interact ? &sgeom : &c->mon->w;

    client_set_bounds(c, geo.width, geo.height);
    c->geom = geo;
    applybounds(c, bbox);

    /* Apply viewport transform: world -> screen coordinates */
    view_x = WORLD_TO_SCREEN_X(c->geom.x);
    view_y = WORLD_TO_SCREEN_Y(c->geom.y);

    /* Update scene-graph (window size remains in world coords, 
     * zoom will be applied when we implement buffer scaling) */
    wlr_scene_node_set_position(&c->scene->node, view_x, view_y);
    wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
    wlr_scene_rect_set_size(c->border[0], c->geom.width, c->bw);
    wlr_scene_rect_set_size(c->border[1], c->geom.width, c->bw);
    wlr_scene_rect_set_size(c->border[2], c->bw, c->geom.height - 2 * c->bw);
    wlr_scene_rect_set_size(c->border[3], c->bw, c->geom.height - 2 * c->bw);
    wlr_scene_node_set_position(&c->border[1]->node, 0, c->geom.height - c->bw);
    wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
    wlr_scene_node_set_position(&c->border[3]->node, c->geom.width - c->bw, c->bw);

    if (focusringpx > 0 && c->focus_ring[0]) {
        int ring = (int)focusringpx;
        int ring_w = c->geom.width + 2 * ring;
        int ring_h = c->geom.height + 2 * ring;
        wlr_scene_rect_set_size(c->focus_ring[0], ring_w, ring);
        wlr_scene_rect_set_size(c->focus_ring[1], ring_w, ring);
        wlr_scene_rect_set_size(c->focus_ring[2], ring, ring_h);
        wlr_scene_rect_set_size(c->focus_ring[3], ring, ring_h);
        wlr_scene_node_set_position(&c->focus_ring[0]->node, -ring, -ring);
        wlr_scene_node_set_position(&c->focus_ring[1]->node, -ring, c->geom.height);
        wlr_scene_node_set_position(&c->focus_ring[2]->node, -ring, -ring);
        wlr_scene_node_set_position(&c->focus_ring[3]->node, c->geom.width, -ring);
    }

    /* this is a no-op if size hasn't changed */
    c->resize = client_set_size(c, c->geom.width - 2 * c->bw,
            c->geom.height - 2 * c->bw);
    client_get_clip(c, &clip);
    wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
}

/* ============================================================================
 * Focus Management
 * ============================================================================ */

void
focusclient(Client *c, int lift)
{
    struct wlr_surface *old = seat->keyboard_state.focused_surface;
    int unused_lx, unused_ly, old_client_type;
    Client *old_c = NULL;
    LayerSurface *old_l = NULL;

    if (locked)
        return;

    /* Raise client in stacking order if requested */
    if (c && lift)
        wlr_scene_node_raise_to_top(&c->scene->node);

    if (c && client_surface(c) == old)
        return;

    if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
        struct wlr_xdg_popup *popup, *tmp;
        wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
            wlr_xdg_popup_destroy(popup);
    }

    /* Put the new client atop the focus stack and select its monitor */
    if (c && !client_is_unmanaged(c)) {
        wl_list_remove(&c->flink);
        wl_list_insert(&fstack, &c->flink);
        selmon = c->mon;
        c->isurgent = 0;

        /* Don't change border color if there is an exclusive focus or we are
         * handling a drag operation */
        if (!exclusive_focus && !seat->drag) {
            client_set_border_color(c, focuscolor);
            client_set_focus_ring(c, 1);
        }
    }

    /* Deactivate old client if focus is changing */
    if (old && (!c || client_surface(c) != old)) {
        /* If an overlay is focused, don't focus or activate the client,
         * but only update its position in fstack to render its border with focuscolor
         * and focus it after the overlay is closed. */
        if (old_client_type == LayerShell && wlr_scene_node_coords(
                    &old_l->scene->node, &unused_lx, &unused_ly)
                && old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
            return;
        } else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
            return;
        /* Don't deactivate old client if the new one wants focus, as this causes issues with winecfg
         * and probably other clients */
        } else if (old_c && !client_is_unmanaged(old_c) && (!c || !client_wants_focus(c))) {
            client_set_border_color(old_c, bordercolor);
            client_set_focus_ring(old_c, 0);

            client_activate_surface(old, 0);
        }
    }
    printstatus();

    if (!c) {
        /* With no client, all we have left is to clear focus */
        wlr_seat_keyboard_notify_clear_focus(seat);
        return;
    }

    /* Change cursor surface */
    motionnotify(0, NULL, 0, 0, 0, 0);

    /* Have a client, so focus its top-level wlr_surface */
    client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));

    /* Activate the new client */
    client_activate_surface(client_surface(c), 1);
    
    /* Camera follow mode - center on focused window */
    viewport_follow_focus();
}

Client *
focustop(Monitor *m)
{
    Client *c;
    if (!m)
        return NULL;
    wl_list_for_each(c, &fstack, flink) {
        if (VISIBLEON(c, m))
            return c;
    }
    return NULL;
}

static void
focus_top(Monitor *m, int lift)
{
    Client *top = focustop(m);
    focusclient(top, lift);
}

void
focusstack(const Arg *arg)
{
    /* Focus the next or previous client (in tiling order) on selmon */
    Client *c, *sel = focustop(selmon);
    if (!sel || (sel->isfullscreen && !client_has_children(sel)))
        return;
    if (arg->i > 0) {
        wl_list_for_each(c, &sel->link, link) {
            if (&c->link == &clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(c, selmon))
                break; /* found it */
        }
    } else {
        wl_list_for_each_reverse(c, &sel->link, link) {
            if (&c->link == &clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(c, selmon))
                break; /* found it */
        }
    }
    /* If only one client is visible on selmon, then c == sel */
    focusclient(c, 1);
}

void
fullscreennotify(struct wl_listener *listener, void *data)
{
    Client *c = wl_container_of(listener, c, fullscreen);
    setfullscreen(c, client_wants_fullscreen(c));
}

void
maximizenotify(struct wl_listener *listener, void *data)
{
    /* This event is raised when a client would like to maximize itself,
     * typically because the user clicked on the maximize button on
     * client-side decorations. dwl doesn't support maximization, but
     * to conform to xdg-shell protocol we still must send a configure.
     * Since xdg-shell protocol v5 we should ignore request of unsupported
     * capabilities, just schedule a empty configure when the client uses <5
     * protocol version
     * wlr_xdg_surface_schedule_configure() is used to send an empty reply. */
    Client *c = wl_container_of(listener, c, maximize);
    if (c->surface.xdg->initialized
            && wl_resource_get_version(c->surface.xdg->toplevel->resource)
                    < XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
        wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

void
updatetitle(struct wl_listener *listener, void *data)
{
    Client *c = wl_container_of(listener, c, set_title);
    if (c == focustop(c->mon))
        printstatus();
}

void
urgent(struct wl_listener *listener, void *data)
{
    struct wlr_xdg_activation_v1_request_activate_event *event = data;
    Client *c = NULL;
    toplevel_from_wlr_surface(event->surface, &c, NULL);
    if (!c || c == focustop(selmon))
        return;

    c->isurgent = 1;
    printstatus();

    if (client_surface(c)->mapped)
        client_set_border_color(c, urgentcolor);
}

/* ============================================================================
 * Window State Management
 * ============================================================================ */

void
setfloating(Client *c, int floating)
{
    Client *p = client_get_parent(c);
    c->isfloating = floating;
    /* If in floating layout do not change the client's layer */
    if (!c->mon || !client_surface(c)->mapped || !c->mon->lt[c->mon->sellt]->arrange)
        return;
    wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen ||
            (p && p->isfullscreen) ? LyrFS
            : c->isfloating ? LyrFloat : LyrTile]);
    arrange(c->mon);
    printstatus();
}

void
setfullscreen(Client *c, int fullscreen)
{
    c->isfullscreen = fullscreen;
    if (!c->mon || !client_surface(c)->mapped)
        return;
    c->bw = fullscreen ? 0 : borderpx;
    client_set_fullscreen(c, fullscreen);
    wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen
            ? LyrFS : c->isfloating ? LyrFloat : LyrTile]);

    if (fullscreen) {
        c->prev = c->geom;
        resize(c, c->mon->m, 0);
    } else {
        /* restore previous size instead of arrange for floating windows since
         * client positions are set by the user and cannot be recalculated */
        resize(c, c->prev, 0);
    }
    arrange(c->mon);
    printstatus();
}

void
togglefloating(const Arg *arg)
{
    Client *sel = focustop(selmon);
    /* return if fullscreen */
    if (sel && !sel->isfullscreen)
        setfloating(sel, !sel->isfloating);
}

void
togglefullscreen(const Arg *arg)
{
    Client *sel = focustop(selmon);
    if (sel)
        setfullscreen(sel, !sel->isfullscreen);
}

void
moveresize(const Arg *arg)
{
    if (cursor_mode != CurNormal && cursor_mode != CurPressed)
        return;
    xytonode(cursor->x, cursor->y, NULL, &grabc, NULL, NULL, NULL);
    if (!grabc || client_is_unmanaged(grabc) || grabc->isfullscreen)
        return;

    /* Float the window and tell motionnotify to grab it */
    setfloating(grabc, 1);
    switch (cursor_mode = arg->ui) {
    case CurMove:
        grabcx = (int)round(cursor->x) - grabc->geom.x;
        grabcy = (int)round(cursor->y) - grabc->geom.y;
        wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
        break;
    case CurResize:
        /* Doesn't work for X11 output - the next absolute motion event
         * returns the cursor to where it started */
        wlr_cursor_warp_closest(cursor, NULL,
                grabc->geom.x + grabc->geom.width,
                grabc->geom.y + grabc->geom.height);
        wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
        break;
    }
}

/* ============================================================================
 * Monitor Management
 * ============================================================================ */

void
createmon(struct wl_listener *listener, void *data)
{
    /* This event is raised by the backend when a new output (aka a display or
     * monitor) becomes available. */
    struct wlr_output *wlr_output = data;
    const MonitorRule *r;
    size_t i;
    struct wlr_output_state state;
    Monitor *m;

    if (!wlr_output_init_render(wlr_output, alloc, drw))
        return;

    m = wlr_output->data = ecalloc(1, sizeof(*m));
    m->wlr_output = wlr_output;

    for (i = 0; i < LENGTH(m->layers); i++)
        wl_list_init(&m->layers[i]);

    wlr_output_state_init(&state);
    /* Initialize monitor state using configured rules */
    m->tagset[0] = m->tagset[1] = 1;
    for (r = monrules; r < END(monrules); r++) {
        if (!r->name || strstr(wlr_output->name, r->name)) {
            m->m.x = r->x;
            m->m.y = r->y;
            m->mfact = r->mfact;
            m->nmaster = r->nmaster;
            m->lt[0] = r->lt;
            m->lt[1] = &layouts[LENGTH(layouts) > 1 && r->lt != &layouts[1]];
            snprintf(m->ltsymbol, sizeof(m->ltsymbol), "%s", m->lt[m->sellt]->symbol);
            wlr_output_state_set_scale(&state, r->scale);
            wlr_output_state_set_transform(&state, r->rr);
            break;
        }
    }

    /* The mode is a tuple of (width, height, refresh rate), and each
     * monitor supports only a specific set of modes. We just pick the
     * monitor's preferred mode; a more sophisticated compositor would let
     * the user configure it. */
    wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));

    /* Set up event listeners */
    LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
    LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);
    LISTEN(&wlr_output->events.request_state, &m->request_state, requestmonstate);

    wlr_output_state_set_enabled(&state, 1);
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    wl_list_insert(&mons, &m->link);
    printstatus();

    /* The xdg-protocol specifies:
     *
     * If the fullscreened surface is not opaque, the compositor must make
     * sure that other screen content not part of the same surface tree (made
     * up of subsurfaces, popups or similarly coupled surfaces) are not
     * visible below the fullscreened surface.
     *
     */
    /* updatemons() will resize and set correct position */
    m->fullscreen_bg = wlr_scene_rect_create(layers[LyrFS], 0, 0, fullscreen_bg);
    wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

    /* Initialize stationary wallpaper background */
    wallpaper_init();

    /* Adds this to the output layout in the order it was configured.
     *
     * The output layout utility automatically adds a wl_output global to the
     * display, which Wayland clients can see to find out information about the
     * output (such as DPI, scale factor, manufacturer, etc).
     */
    m->scene_output = wlr_scene_output_create(scene, wlr_output);
    if (m->m.x == -1 && m->m.y == -1)
        wlr_output_layout_add_auto(output_layout, wlr_output);
    else
        wlr_output_layout_add(output_layout, wlr_output, m->m.x, m->m.y);
}

void
cleanupmon(struct wl_listener *listener, void *data)
{
    Monitor *m = wl_container_of(listener, m, destroy);
    LayerSurface *l, *tmp;
    size_t i;

    /* m->layers[i] are intentionally not unlinked */
    for (i = 0; i < LENGTH(m->layers); i++) {
        wl_list_for_each_safe(l, tmp, &m->layers[i], link)
            wlr_layer_surface_v1_destroy(l->layer_surface);
    }

    wl_list_remove(&m->destroy.link);
    wl_list_remove(&m->frame.link);
    wl_list_remove(&m->link);
    wl_list_remove(&m->request_state.link);
    if (m->lock_surface)
        destroylocksurface(&m->destroy_lock_surface, NULL);
    m->wlr_output->data = NULL;
    wlr_output_layout_remove(output_layout, m->wlr_output);
    wlr_scene_output_destroy(m->scene_output);

    closemon(m);
    wlr_scene_node_destroy(&m->fullscreen_bg->node);
    free(m);
}

void
updatemons(struct wl_listener *listener, void *data)
{
    /*
     * Called whenever the output layout changes: adding or removing a
     * monitor, changing an output's mode or position, etc. This is where
     * the change officially happens and we update geometry, window
     * positions, focus, and the stored configuration in wlroots'
     * output-manager implementation.
     */
    struct wlr_output_configuration_v1 *config
            = wlr_output_configuration_v1_create();
    Client *c;
    struct wlr_output_configuration_head_v1 *config_head;
    Monitor *m;

    /* First remove from the layout the disabled monitors */
    wl_list_for_each(m, &mons, link) {
        if (m->wlr_output->enabled || m->asleep)
            continue;
        config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);
        config_head->state.enabled = 0;
        /* Remove this output from the layout to avoid cursor enter inside it */
        wlr_output_layout_remove(output_layout, m->wlr_output);
        closemon(m);
        m->m = m->w = (struct wlr_box){0};
    }
    /* Insert outputs that need to */
    wl_list_for_each(m, &mons, link) {
        if (m->wlr_output->enabled
                && !wlr_output_layout_get(output_layout, m->wlr_output))
            wlr_output_layout_add_auto(output_layout, m->wlr_output);
    }

    /* Now that we update the output layout we can get its box */
    wlr_output_layout_get_box(output_layout, NULL, &sgeom);

    wlr_scene_node_set_position(&root_bg->node, sgeom.x, sgeom.y);
    wlr_scene_rect_set_size(root_bg, sgeom.width, sgeom.height);

    /* Make sure the clients are hidden when dwl is locked */
    wlr_scene_node_set_position(&locked_bg->node, sgeom.x, sgeom.y);
    wlr_scene_rect_set_size(locked_bg, sgeom.width, sgeom.height);

    wl_list_for_each(m, &mons, link) {
        if (!m->wlr_output->enabled)
            continue;
        config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);

        /* Get the effective monitor geometry to use for surfaces */
        wlr_output_layout_get_box(output_layout, m->wlr_output, &m->m);
        m->w = m->m;
        wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);

        wlr_scene_node_set_position(&m->fullscreen_bg->node, m->m.x, m->m.y);
        wlr_scene_rect_set_size(m->fullscreen_bg, m->m.width, m->m.height);

        if (m->lock_surface) {
            struct wlr_scene_tree *scene_tree = m->lock_surface->surface->data;
            wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
            wlr_session_lock_surface_v1_configure(m->lock_surface, m->m.width, m->m.height);
        }

        /* Calculate the effective monitor geometry to use for clients */
        arrangelayers(m);
        /* Don't move clients to the left output when plugging monitors */
        arrange(m);
        /* make sure fullscreen clients have the right size */
        if ((c = focustop(m)) && c->isfullscreen)
            resize(c, m->m, 0);

        /* Try to re-set the gamma LUT when updating monitors,
         * it's only really needed when enabling a disabled output, but meh. */
        m->gamma_lut_changed = 1;

        config_head->state.x = m->m.x;
        config_head->state.y = m->m.y;

        if (!selmon) {
            selmon = m;
        }
    }

    if (selmon && selmon->wlr_output->enabled) {
        wl_list_for_each(c, &clients, link) {
            if (!c->mon && client_surface(c)->mapped)
                setmon(c, selmon, c->tags);
        }
        focus_top(selmon, 1);
        if (selmon->lock_surface) {
            client_notify_enter(selmon->lock_surface->surface,
                    wlr_seat_get_keyboard(seat));
            client_activate_surface(selmon->lock_surface->surface, 1);
        }
    }

    /* FIXME: figure out why the cursor image is at 0,0 after turning all
     * the monitors on.
     * Move the cursor image where it used to be. It does not generate a
     * wl_pointer.motion event for the clients, it's only the image what it's
     * at the wrong position after all. */
    wlr_cursor_move(cursor, NULL, 0, 0);

    wlr_output_manager_v1_set_configuration(output_mgr, config);
}

void
setmon(Client *c, Monitor *m, uint32_t newtags)
{
    Monitor *oldmon = c->mon;

    if (oldmon == m)
        return;
    c->mon = m;
    c->prev = c->geom;

    /* Scene graph sends surface leave/enter events on move and resize */
    if (oldmon)
        arrange(oldmon);
    if (m) {
        /* Make sure window actually overlaps with the monitor */
        resize(c, c->geom, 0);
        c->tags = newtags ? newtags : m->tagset[m->seltags]; /* assign tags of target monitor */
        setfullscreen(c, c->isfullscreen); /* This will call arrange(c->mon) */
        setfloating(c, c->isfloating);
    }
    focus_top(selmon, 1);
}

void
closemon(Monitor *m)
{
    /* update selmon if needed and
     * move closed monitor's clients to the focused one */
    Client *c;
    int i = 0, nmons = wl_list_length(&mons);
    if (!nmons) {
        selmon = NULL;
    } else if (m == selmon) {
        do /* don't switch to disabled mons */
            selmon = wl_container_of(mons.next, selmon, link);
        while (!selmon->wlr_output->enabled && i++ < nmons);

        if (!selmon->wlr_output->enabled)
            selmon = NULL;
    }

    wl_list_for_each(c, &clients, link) {
        if (c->isfloating && c->geom.x > m->m.width)
            resize(c, (struct wlr_box){.x = c->geom.x - m->w.width, .y = c->geom.y,
                    .width = c->geom.width, .height = c->geom.height}, 0);
        if (c->mon == m)
            setmon(c, selmon, c->tags);
    }
    focus_top(selmon, 1);
    printstatus();
}

void
tagmon(const Arg *arg)
{
    Client *sel = focustop(selmon);
    if (sel)
        setmon(sel, dirtomon(arg->i), 0);
}

void
focusmon(const Arg *arg)
{
    int i = 0, nmons = wl_list_length(&mons);
    if (nmons) {
        do /* don't switch to disabled mons */
            selmon = dirtomon(arg->i);
        while (!selmon->wlr_output->enabled && i++ < nmons);
    }
    focus_top(selmon, 1);
}

Monitor *
dirtomon(enum wlr_direction dir)
{
    struct wlr_output *next;
    if (!wlr_output_layout_get(output_layout, selmon->wlr_output))
        return selmon;
    if ((next = wlr_output_layout_adjacent_output(output_layout,
            dir, selmon->wlr_output, selmon->m.x, selmon->m.y)))
        return next->data;
    if ((next = wlr_output_layout_farthest_output(output_layout,
            dir ^ (WLR_DIRECTION_LEFT|WLR_DIRECTION_RIGHT),
            selmon->wlr_output, selmon->m.x, selmon->m.y)))
        return next->data;
    return selmon;
}

Monitor *
xytomon(double x, double y)
{
    struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
    Monitor *m;

    if (o)
        return o->data;
    if (selmon)
        return selmon;
    if (!wl_list_empty(&mons)) {
        m = wl_container_of(mons.next, m, link);
        return m;
    }
    return NULL;
}

void
xytonode(double x, double y, struct wlr_surface **psurface,
        Client **pc, LayerSurface **pl, double *nx, double *ny)
{
    struct wlr_scene_node *node, *pnode;
    struct wlr_surface *surface = NULL;
    Client *c = NULL;
    LayerSurface *l = NULL;
    int layer;

    for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
        if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
            continue;

        if (node->type == WLR_SCENE_NODE_BUFFER)
            surface = wlr_scene_surface_try_from_buffer(
                    wlr_scene_buffer_from_node(node))->surface;
        /* Walk the tree to find a node that knows the client */
        for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
            c = pnode->data;
        if (c && c->type == LayerShell) {
            c = NULL;
            l = pnode->data;
        }
    }

    if (psurface) *psurface = surface;
    if (pc) *pc = c;
    if (pl) *pl = l;
}

/* ============================================================================
 * Tag Management
 * ============================================================================ */

void
tag(const Arg *arg)
{
    Client *sel = focustop(selmon);
    if (!sel || (arg->ui & TAGMASK) == 0)
        return;

    sel->tags = arg->ui & TAGMASK;
    focus_top(selmon, 1);
    arrange(selmon);
    printstatus();
}

void
toggletag(const Arg *arg)
{
    uint32_t newtags;
    Client *sel = focustop(selmon);
    if (!sel || !(newtags = sel->tags ^ (arg->ui & TAGMASK)))
        return;

    sel->tags = newtags;
    focus_top(selmon, 1);
    arrange(selmon);
    printstatus();
}

void
view(const Arg *arg)
{
    if (!selmon || (arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
        return;
    selmon->seltags ^= 1; /* toggle sel tagset */
    if (arg->ui & TAGMASK)
        selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
    focus_top(selmon, 1);
    arrange(selmon);
    printstatus();
}

void
toggleview(const Arg *arg)
{
    uint32_t newtagset;
    if (!(newtagset = selmon ? selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK) : 0))
        return;

    selmon->tagset[selmon->seltags] = newtagset;
    focus_top(selmon, 1);
    arrange(selmon);
    printstatus();
}

/* ============================================================================
 * Layout Functions
 * ============================================================================ */

void
tile(Monitor *m)
{
    unsigned int mw, my, ty;
    int i, n = 0;
    Client *c;

    wl_list_for_each(c, &clients, link)
        if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
            n++;
    if (n == 0)
        return;

    if (n > m->nmaster)
        mw = m->nmaster ? (int)roundf(m->w.width * m->mfact) : 0;
    else
        mw = m->w.width;
    i = my = ty = 0;
    wl_list_for_each(c, &clients, link) {
        if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
            continue;
        if (i < m->nmaster) {
            int master_div = MIN(n, m->nmaster) - i;
            if (master_div <= 0)
                master_div = 1;
            resize(c, (struct wlr_box){.x = m->w.x, .y = m->w.y + my, .width = mw,
                .height = (m->w.height - my) / master_div}, 0);
            my += c->geom.height;
        } else {
            int stack_div = n - i;
            if (stack_div <= 0)
                stack_div = 1;
            resize(c, (struct wlr_box){.x = m->w.x + mw, .y = m->w.y + ty,
                .width = m->w.width - mw, .height = (m->w.height - ty) / stack_div}, 0);
            ty += c->geom.height;
        }
        i++;
    }
}

void
monocle(Monitor *m)
{
    Client *c;
    int n = 0;

    wl_list_for_each(c, &clients, link) {
        if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
            continue;
        resize(c, m->w, 0);
        n++;
    }
    if (n)
        snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "[%d]", n);
    if ((c = focustop(m)))
        wlr_scene_node_raise_to_top(&c->scene->node);
}

/* Infinite layout: windows arranged in a scrollable strip 
 * Like Niri but the camera can pan in all directions 
 * Windows keep their natural size, just positioned in world coordinates */

/* Find the rightmost edge of all tiled windows to place new ones */
static float
infinite_rightmost_edge(Monitor *m)
{
    Client *c;
    float max_edge = 0;
    int n = 0;
    
    wl_list_for_each(c, &clients, link) {
        if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && c->world.set) {
            float edge = c->world.x + c->geom.width;
            if (edge > max_edge)
                max_edge = edge;
            n++;
        }
    }
    
    /* If no windows placed yet, start at world origin */
    return n > 0 ? max_edge : 0;
}

/* Find the topmost tiled window for vertical placement reference */
static float
infinite_topmost_y(Monitor *m)
{
    Client *c;
    float min_y = 0;
    int n = 0;
    
    wl_list_for_each(c, &clients, link) {
        if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && c->world.set) {
            if (!n || c->world.y < min_y)
                min_y = c->world.y;
            n++;
        }
    }
    
    return n > 0 ? min_y : 0;
}

/* Assign world coordinates to a new window that doesn't have them yet */
static void
infinite_place_window(Client *c, Monitor *m)
{
    float x, y;
    
    if (c->world.set)
        return; /* Already has a world position */
    
    /* Place to the right of the rightmost window */
    x = infinite_rightmost_edge(m);
    
    /* Use the same Y as other windows, or 0 if first window */
    y = infinite_topmost_y(m);
    
    c->world.x = x;
    c->world.y = y;
    c->world.set = true;
}

void
infinite(Monitor *m)
{
    Client *c;
    struct wlr_box geo;
    unsigned int gap = 10; /* gap between windows */
    
    wl_list_for_each(c, &clients, link) {
        if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
            continue;
        
        /* Assign world position if new window */
        if (!c->world.set) {
            infinite_place_window(c, m);
            /* For new windows, set a reasonable default size */
            c->geom.width = (int)(m->w.width * 0.6f);
            c->geom.height = (int)(m->w.height * 0.7f);
        }
        
        /* Position in world coordinates - keep natural window size */
        geo.x = (int)c->world.x + gap;
        geo.y = (int)c->world.y + gap;
        geo.width = c->geom.width;
        geo.height = c->geom.height;
        
        resize(c, geo, 0);
    }
}

void
zoom(const Arg *arg)
{
    Client *c, *sel = focustop(selmon);

    if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange || sel->isfloating)
        return;

    /* Search for the first tiled window that is not sel, marking sel as
     * NULL if we pass it along the way */
    wl_list_for_each(c, &clients, link) {
        if (VISIBLEON(c, selmon) && !c->isfloating) {
            if (c != sel)
                break;
            sel = NULL;
        }
    }

    /* Return if no other tiled window was found */
    if (&c->link == &clients)
        return;

    /* If we passed sel, move c to the front; otherwise, move sel to the
     * front */
    if (!sel)
        sel = c;
    wl_list_remove(&sel->link);
    wl_list_insert(&clients, &sel->link);

    focusclient(sel, 1);
    arrange(selmon);
}

void
setlayout(const Arg *arg)
{
    if (!selmon)
        return;
    if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
        selmon->sellt ^= 1;
    if (arg && arg->v)
        selmon->lt[selmon->sellt] = (Layout *)arg->v;
    snprintf(selmon->ltsymbol, sizeof(selmon->ltsymbol), "%s", selmon->lt[selmon->sellt]->symbol);
    arrange(selmon);
    printstatus();
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
    float f;

    if (!arg || !selmon || !selmon->lt[selmon->sellt]->arrange)
        return;
    f = arg->f < 1.0f ? arg->f + selmon->mfact : arg->f - 1.0f;
    if (f < 0.1 || f > 0.9)
        return;
    selmon->mfact = f;
    arrange(selmon);
}

void
incnmaster(const Arg *arg)
{
    if (!arg || !selmon)
        return;
    selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
    arrange(selmon);
}

/* ============================================================================
 * Layer Shell Management
 * ============================================================================ */

void
createlayersurface(struct wl_listener *listener, void *data)
{
    struct wlr_layer_surface_v1 *layer_surface = data;
    LayerSurface *l;
    Monitor *m;
    struct wlr_surface *surface = layer_surface->surface;
    struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->pending.layer]];

    if (!layer_surface->output
            && !(layer_surface->output = selmon ? selmon->wlr_output : NULL)) {
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }

    m = layer_surface->output ? layer_surface->output->data : NULL;
    if (!m) {
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }

    l = ecalloc(1, sizeof(*l));
    layer_surface->data = l;
    l->type = LayerShell;
    l->layer_surface = layer_surface;
    l->mon = m;
    l->scene_layer = wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);
    if (!l->scene_layer) {
        layer_surface->data = NULL;
        free(l);
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }
    l->scene = l->scene_layer->tree;
    l->popups = surface->data = wlr_scene_tree_create(layer_surface->current.layer
            < ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer);
    if (!l->popups) {
        wlr_scene_node_destroy(&l->scene->node);
        layer_surface->data = NULL;
        free(l);
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }
    l->scene->node.data = l->popups->node.data = l;

    LISTEN(&surface->events.commit, &l->surface_commit, commitlayersurfacenotify);
    LISTEN(&surface->events.unmap, &l->unmap, unmaplayersurfacenotify);
    LISTEN(&layer_surface->events.destroy, &l->destroy, destroylayersurfacenotify);

    wl_list_insert(&l->mon->layers[layer_surface->pending.layer],&l->link);
    wlr_surface_send_enter(surface, layer_surface->output);
}

void
destroylayersurfacenotify(struct wl_listener *listener, void *data)
{
    LayerSurface *l = wl_container_of(listener, l, destroy);

    wl_list_remove(&l->link);
    wl_list_remove(&l->destroy.link);
    wl_list_remove(&l->unmap.link);
    wl_list_remove(&l->surface_commit.link);
    wlr_scene_node_destroy(&l->scene->node);
    wlr_scene_node_destroy(&l->popups->node);
    free(l);
}

void
unmaplayersurfacenotify(struct wl_listener *listener, void *data)
{
    LayerSurface *l = wl_container_of(listener, l, unmap);

    l->mapped = 0;
    wlr_scene_node_set_enabled(&l->scene->node, 0);
    if (l == exclusive_focus)
        exclusive_focus = NULL;
    if (l->layer_surface->output && (l->mon = l->layer_surface->output->data))
        arrangelayers(l->mon);
    if (l->layer_surface->surface == seat->keyboard_state.focused_surface)
        focus_top(selmon, 1);
    motionnotify(0, NULL, 0, 0, 0, 0);
}

/* ============================================================================
 * Client Operations
 * ============================================================================ */

void
killclient(const Arg *arg)
{
    Client *sel = focustop(selmon);
    if (sel)
        client_send_close(sel);
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static void
printstatus(void)
{
    Monitor *m = NULL;
    Client *c;
    uint32_t occ, urg, sel;

    wl_list_for_each(m, &mons, link) {
        occ = urg = 0;
        wl_list_for_each(c, &clients, link) {
            if (c->mon != m)
                continue;
            occ |= c->tags;
            if (c->isurgent)
                urg |= c->tags;
        }
        if ((c = focustop(m))) {
            printf("%s title %s\n", m->wlr_output->name, client_get_title(c));
            printf("%s appid %s\n", m->wlr_output->name, client_get_appid(c));
            printf("%s fullscreen %d\n", m->wlr_output->name, c->isfullscreen);
            printf("%s floating %d\n", m->wlr_output->name, c->isfloating);
            sel = c->tags;
        } else {
            printf("%s title \n", m->wlr_output->name);
            printf("%s appid \n", m->wlr_output->name);
            printf("%s fullscreen \n", m->wlr_output->name);
            printf("%s floating \n", m->wlr_output->name);
            sel = 0;
        }

        printf("%s selmon %u\n", m->wlr_output->name, m == selmon);
        printf("%s tags %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32"\n",
            m->wlr_output->name, occ, m->tagset[m->seltags], sel, urg);
        printf("%s layout %s\n", m->wlr_output->name, m->ltsymbol);
    }
    fflush(stdout);
}
