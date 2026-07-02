/* See LICENSE.dwm file for copyright and license details. */

#ifndef KALIN_UTIL_H
#define KALIN_UTIL_H

#include <stddef.h>
#include <stdlib.h>
#include <wayland-server-core.h>

#if defined(__GNUC__) || defined(__clang__)
#define ATTR_NORETURN __attribute__((noreturn))
#define ATTR_PRINTF(a, b) __attribute__((format(printf, a, b)))
#define ATTR_MALLOC __attribute__((malloc))
#define ATTR_RETURNS_NONNULL __attribute__((returns_nonnull))
#else
#define ATTR_NORETURN
#define ATTR_PRINTF(a, b)
#define ATTR_MALLOC
#define ATTR_RETURNS_NONNULL
#endif

void die(const char *fmt, ...) ATTR_NORETURN ATTR_PRINTF(1, 2);
void *ecalloc(size_t nmemb, size_t size) ATTR_MALLOC ATTR_RETURNS_NONNULL;
int fd_set_nonblock(int fd);

typedef struct StaticListener {
	struct wl_listener listener;
	struct wl_list link;
} StaticListener;

extern struct wl_list static_listeners;

static inline void
listen_static(struct wl_signal *signal, wl_notify_func_t notify)
{
	StaticListener *sl = ecalloc(1, sizeof(*sl));
	sl->listener.notify = notify;
	wl_signal_add(signal, &sl->listener);
	wl_list_insert(&static_listeners, &sl->link);
}

static inline void
static_listener_free(struct wl_listener *listener)
{
	StaticListener *sl = wl_container_of(listener, sl, listener);
	wl_list_remove(&listener->link);
	wl_list_remove(&sl->link);
	free(sl);
}

#define LISTEN_STATIC(E, H) listen_static((E), (H))

#undef ATTR_NORETURN
#undef ATTR_PRINTF
#undef ATTR_MALLOC
#undef ATTR_RETURNS_NONNULL

#endif /* KALIN_UTIL_H */
