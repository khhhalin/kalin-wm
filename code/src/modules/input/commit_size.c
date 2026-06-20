#include "../../include/kalin.h"
#include "../../include/client_inline.h"
#include "../../include/runtime_modules.h"

int
client_accept_requested_size(Client *c)
{
	struct wlr_box g;
	struct wlr_box geo;
	int want_w, want_h;

	if (!c || c->isfullscreen || !c->isfloating)
		return 0;
	if (!c->scene || !c->scene_surface || !client_surface(c)->mapped)
		return 0;
	if (c->crop.active)
		return 0;

	client_get_geometry(c, &g);
	if (g.width <= 0 || g.height <= 0)
		return 0;

	want_w = g.width + 2 * (int)c->bw;
	want_h = g.height + 2 * (int)c->bw;
	if (want_w <= 0 || want_h <= 0)
		return 0;

	if (want_w == c->geom.width && want_h == c->geom.height)
		return 0;

	geo = c->geom;
	geo.width = want_w;
	geo.height = want_h;
	compositor_resize_client(c, &geo, 1);
	return 1;
}
