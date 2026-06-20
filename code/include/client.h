/*
 * client.h - Client and window management module for kalin-wm
 *
 * This module handles all client/window operations including:
 * - Client creation and destruction
 * - Window arrangement and layout
 * - Focus management
 * - Monitor management
 * - XDG decoration handling
 * - Layer shell surface management
 */

#ifndef CLIENT_H
#define CLIENT_H

/* ============================================================================
 * Client Lifecycle Management
 * ============================================================================ */

/**
 * Create a new client from an XDG toplevel surface.
 * Called when a client creates a new application window.
 */
void createnotify(struct wl_listener *listener, void *data);

/**
 * Create a new popup window.
 * Called for both xdg-shell and layer-shell popups.
 */
void createpopup(struct wl_listener *listener, void *data);

/**
 * Handle client destruction.
 * Called when the xdg_toplevel is destroyed.
 */
void destroynotify(struct wl_listener *listener, void *data);

/**
 * Handle surface unmapping.
 * Called when the surface should no longer be shown.
 */
void unmapnotify(struct wl_listener *listener, void *data);

/**
 * Handle surface mapping.
 * Called when the surface is ready to display on-screen.
 */
void mapnotify(struct wl_listener *listener, void *data);

/* ============================================================================
 * Surface Commit Handling
 * ============================================================================ */

/**
 * Handle client surface commits.
 * Processes buffer updates and size changes from clients.
 */
void commitnotify(struct wl_listener *listener, void *data);

/**
 * Handle popup surface commits.
 * Configures popup positioning and constraints.
 */
void commitpopup(struct wl_listener *listener, void *data);

/**
 * Handle layer surface commits.
 * Updates layer surface state and arrangement.
 */
void commitlayersurfacenotify(struct wl_listener *listener, void *data);

/* ============================================================================
 * XDG Decoration Handling
 * ============================================================================ */

/**
 * Create a new XDG toplevel decoration.
 * Sets up server-side decoration handling.
 */
void createdecoration(struct wl_listener *listener, void *data);

/**
 * Destroy an XDG decoration.
 * Cleans up decoration-related listeners.
 */
void destroydecoration(struct wl_listener *listener, void *data);

/**
 * Handle decoration mode requests.
 * Enforces server-side decorations.
 */
void requestdecorationmode(struct wl_listener *listener, void *data);

/* ============================================================================
 * Window Arrangement and Layout
 * ============================================================================ */

/**
 * Arrange windows on a monitor.
 * Updates visibility and applies the current layout.
 */
void arrange(Monitor *m);

/**
 * Arrange layer shell surfaces for a monitor.
 * Processes exclusive and non-exclusive zones.
 */
void arrangelayers(Monitor *m);

/**
 * Arrange surfaces in a single layer.
 * Helper function for arrangelayers().
 */
void arrangelayer(Monitor *m, struct wl_list *list,
        struct wlr_box *usable_area, int exclusive);

/**
 * Apply rules to a new client.
 * Matches against window rules and sets initial properties.
 */
void applyrules(Client *c);

/**
 * Apply geometry bounds to a client.
 * Ensures the client stays within the specified bounding box.
 */
void applybounds(Client *c, struct wlr_box *bbox);

/**
 * Resize and reposition a client.
 * Updates scene graph and sends configure events.
 */
void resize(Client *c, struct wlr_box geo, int interact);

/* ============================================================================
 * Focus Management
 * ============================================================================ */

/**
 * Focus a client.
 * Updates focus stack, border colors, and keyboard focus.
 */
void focusclient(Client *c, int lift);

/**
 * Get the topmost focused client on a monitor.
 * Returns the most recently focused visible client.
 */
Client *focustop(Monitor *m);

/**
 * Focus the next/previous client in the stack.
 * Used for Alt-Tab style focus switching.
 */
void focusstack(const Arg *arg);

/**
 * Handle fullscreen requests from clients.
 */
void fullscreennotify(struct wl_listener *listener, void *data);

/**
 * Handle maximize requests from clients.
 * dwl doesn't support maximization but sends required configure events.
 */
void maximizenotify(struct wl_listener *listener, void *data);

/**
 * Handle urgency hints from clients.
 */
void urgent(struct wl_listener *listener, void *data);

/**
 * Update client title and print status.
 */
void updatetitle(struct wl_listener *listener, void *data);

/* ============================================================================
 * Window State Management
 * ============================================================================ */

/**
 * Set a client's floating state.
 * Repositions the client in the appropriate scene layer.
 */
void setfloating(Client *c, int floating);

/**
 * Set a client's fullscreen state.
 * Updates geometry and reparents to fullscreen layer.
 */
void setfullscreen(Client *c, int fullscreen);

/**
 * Toggle floating state of the selected client.
 */
void togglefloating(const Arg *arg);

/**
 * Toggle fullscreen state of the selected client.
 */
void togglefullscreen(const Arg *arg);

/**
 * Move/resize the selected client interactively.
 */
void moveresize(const Arg *arg);

/* ============================================================================
 * Monitor Management
 * ============================================================================ */

/**
 * Create a new monitor/output.
 * Called when a new display becomes available.
 */
void createmon(struct wl_listener *listener, void *data);

/**
 * Clean up a destroyed monitor.
 * Moves clients to other monitors and frees resources.
 */
void cleanupmon(struct wl_listener *listener, void *data);

/**
 * Update monitor configuration.
 * Called when output layout changes.
 */
void updatemons(struct wl_listener *listener, void *data);

/**
 * Assign a client to a monitor.
 * Handles client migration between monitors.
 */
void setmon(Client *c, Monitor *m, uint32_t newtags);

/**
 * Close a monitor and migrate its clients.
 */
void closemon(Monitor *m);

/**
 * Move selected client to another monitor.
 */
void tagmon(const Arg *arg);

/**
 * Focus a different monitor.
 */
void focusmon(const Arg *arg);

/**
 * Get monitor in a specific direction from current.
 */
Monitor *dirtomon(enum wlr_direction dir);

/**
 * Get monitor at specific coordinates.
 */
Monitor *xytomon(double x, double y);

/**
 * Find node at specific coordinates.
 * Returns surface, client, or layer surface at position.
 */
void xytonode(double x, double y, struct wlr_surface **psurface,
        Client **pc, LayerSurface **pl, double *nx, double *ny);

/* ============================================================================
 * Tag Management
 * ============================================================================ */

/**
 * Assign tags to the selected client.
 */
void tag(const Arg *arg);

/**
 * Toggle tags on the selected client.
 */
void toggletag(const Arg *arg);

/**
 * Switch to a different tag view.
 */
void view(const Arg *arg);

/**
 * Toggle tag view.
 */
void toggleview(const Arg *arg);

/* ============================================================================
 * Layout Functions
 * ============================================================================ */

/**
 * Tile layout - master/stack arrangement.
 */
void tile(Monitor *m);

/**
 * Monocle layout - single fullscreen window per tag.
 */
void monocle(Monitor *m);

/**
 * Infinite layout - 2D scrollable canvas.
 */
void infinite(Monitor *m);

/**
 * Zoom - swap selected window with master.
 */
void zoom(const Arg *arg);

/**
 * Change layout.
 */
void setlayout(const Arg *arg);

/**
 * Change master factor.
 */
void setmfact(const Arg *arg);

/**
 * Change number of master windows.
 */
void incnmaster(const Arg *arg);

/* ============================================================================
 * Layer Shell Management
 * ============================================================================ */

/**
 * Create a new layer shell surface.
 */
void createlayersurface(struct wl_listener *listener, void *data);

/**
 * Destroy a layer shell surface.
 */
void destroylayersurfacenotify(struct wl_listener *listener, void *data);

/**
 * Unmap a layer shell surface.
 */
void unmaplayersurfacenotify(struct wl_listener *listener, void *data);

/* ============================================================================
 * Client Operations
 * ============================================================================ */

/**
 * Kill the selected client.
 */
void killclient(const Arg *arg);

/**
 * Send close signal to a client.
 * Convenience wrapper around client_send_close().
 */
static inline void client_close(Client *c) {
    if (c)
        client_send_close(c);
}

/* ============================================================================
 * Client Helper Functions (from original client.h)
 * ============================================================================ */

#include "client_inline.h"

#endif /* CLIENT_H */
