/* dockprep: pending "this app_id's next map should dock straight into this
 * rect" requests. Registered via the "dockprep" IPC command (ipc.c) before a
 * shell panel spawns its backing terminal, consumed one-shot in mapnotify().
 * Self-contained: owns only the fixed-size dockprep_pending table below;
 * touches no compositor globals. Public (dockprep_register/dockprep_consume in
 * kalin.h) so ipc.c's separate TU can reach the register side.
 *
 * #include'd into dwl.c (a feature module in its translation unit, like
 * spawn/tmux_spawn.c): the definitions land in dwl.c's TU, and dwl.c's
 * forward-decls (near dock_hover_client) satisfy mapnotify()'s earlier call. */

#define DOCKPREP_MAX 8
struct dockprep_entry {
	char appid[256];
	struct wlr_box rect;
	int used;
};
static struct dockprep_entry dockprep_pending[DOCKPREP_MAX];

void
dockprep_register(const char *appid, struct wlr_box rect)
{
	int i, slot = 0;

	if (!appid || !*appid)
		return;
	for (i = 0; i < DOCKPREP_MAX; i++) {
		if (dockprep_pending[i].used && strcmp(dockprep_pending[i].appid, appid) == 0) {
			slot = i;
			goto set;
		}
		if (!dockprep_pending[i].used) {
			slot = i;
			goto set;
		}
	}
	/* All slots full and none matched — overwrite slot 0 rather than drop
	 * the request silently; this shouldn't happen in practice (panel count
	 * is well under DOCKPREP_MAX), but a stuck stale slot is worse than
	 * evicting one. */
set:
	snprintf(dockprep_pending[slot].appid, sizeof(dockprep_pending[slot].appid), "%s", appid);
	dockprep_pending[slot].rect = rect;
	dockprep_pending[slot].used = 1;
}

/* Matches and consumes (one-shot) a pending dockprep request for `appid`.
 * Returns 1 and fills *out on a match, 0 otherwise. */
int
dockprep_consume(const char *appid, struct wlr_box *out)
{
	int i;

	if (!appid || !*appid)
		return 0;
	for (i = 0; i < DOCKPREP_MAX; i++) {
		if (dockprep_pending[i].used && strcmp(dockprep_pending[i].appid, appid) == 0) {
			*out = dockprep_pending[i].rect;
			dockprep_pending[i].used = 0;
			return 1;
		}
	}
	return 0;
}
