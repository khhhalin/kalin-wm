#ifndef RUNTIME_MODULES_H
#define RUNTIME_MODULES_H

typedef struct Client Client;
struct wlr_box;

int client_accept_requested_size(Client *c);
void compositor_resize_client(Client *c, const struct wlr_box *geo, int interact);

#endif
