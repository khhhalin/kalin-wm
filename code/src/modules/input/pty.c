/* PTY process tracking: spawn()-launched children get a pseudo-terminal so
 * their stdout/stderr can be logged and, via pty_inject(), fed input (used by
 * the crash-report/persistence "restart and replay" flows). One PTYProc per
 * tracked child, in a simple singly-linked list — process counts here are
 * always small (spawned helper processes), so no need for anything fancier.
 *
 * Separately-compiled TU: links against dwl.c's externed `event_loop` via
 * kalin.h. pty_register() is dwl.c-internal (called only from spawn()),
 * forward-declared there directly rather than in kalin.h; pty_inject(),
 * pty_log_for(), and pty_child_reaped() are the public API other modules use
 * (declared in kalin.h). */
#include "kalin.h"

typedef struct PTYProc {
	pid_t pid;
	int master_fd;
	struct wl_event_source *source;
	char *cmd;
	char *logbuf;
	size_t loglen;
	struct PTYProc *next;
} PTYProc;

static PTYProc *pty_list = NULL;

static PTYProc *
pty_find(pid_t pid)
{
	PTYProc *p;

	for (p = pty_list; p; p = p->next) {
		if (p->pid == pid)
			return p;
	}

	return NULL;
}

static int
pty_append_log(PTYProc *p, const char *buf, size_t len)
{
	char *newbuf;
	size_t newlen;

	if (!p || !buf || len == 0)
		return -1;
	newlen = p->loglen + len;
	newbuf = realloc(p->logbuf, newlen + 1);
	if (!newbuf)
		return -1;
	p->logbuf = newbuf;
	memcpy(p->logbuf + p->loglen, buf, len);
	p->loglen = newlen;
	p->logbuf[p->loglen] = '\0';
	return 0;
}

int
pty_inject(pid_t pid, const char *text)
{
	PTYProc *p = pty_find(pid);
	size_t len;

	if (!p || p->master_fd < 0 || !text)
		return -1;
	len = strlen(text);
	return (int)write(p->master_fd, text, len);
}

const char *
pty_log_for(pid_t pid)
{
	PTYProc *p = pty_find(pid);

	if (!p)
		return NULL;
	return p->logbuf;
}

static int
pty_read_cb(int fd, uint32_t mask, void *data)
{
	PTYProc *p = data;
	char buf[1024];
	ssize_t n;
	PTYProc **pp;

	(void)mask;
	if (!p) return 0;
	n = read(fd, buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		wlr_log(WLR_INFO, "pty[%d]: %s", p->pid, buf);
		pty_append_log(p, buf, (size_t)n);
		return 1; /* keep the source */
	}

	/* EOF or error - unregister */
	if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
		if (p->source)
			wl_event_source_remove(p->source);
		if (p->master_fd >= 0)
			close(p->master_fd);
		/* remove from list */
		pp = &pty_list;
		while (*pp) {
			if (*pp == p) {
				*pp = p->next;
				break;
			}
			pp = &(*pp)->next;
		}
		free(p->cmd);
		free(p->logbuf);
		free(p);
	}
	return 0;
}

void
pty_register(pid_t pid, int master_fd, const char *cmd)
{
	PTYProc *p = calloc(1, sizeof(*p));
	if (!p) return;
	p->pid = pid;
	p->master_fd = master_fd;
	p->cmd = cmd ? strdup(cmd) : NULL;
	p->logbuf = NULL;
	p->loglen = 0;
	p->next = pty_list;
	pty_list = p;
	if (event_loop) {
		p->source = wl_event_loop_add_fd(event_loop, master_fd, WL_EVENT_READABLE, pty_read_cb, p);
	}
}

void
pty_child_reaped(pid_t pid)
{
	PTYProc *p = pty_list;
	PTYProc *prev = NULL;
	while (p) {
		if (p->pid == pid) {
			if (p->source)
				wl_event_source_remove(p->source);
			if (p->master_fd >= 0)
				close(p->master_fd);
			if (prev)
				prev->next = p->next;
			else
				pty_list = p->next;
			free(p->cmd);
			free(p->logbuf);
			free(p);
			return;
		}
		prev = p;
		p = p->next;
	}
}
