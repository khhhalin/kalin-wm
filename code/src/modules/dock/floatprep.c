/* floatprep: pending "this app_id's next map should float under the cursor
 * instead of joining the rail" requests — the menu-spawn (right-click) escape
 * hatch from the rail (layout rethink Phase 4, obsidian/plan/layout-rethink.md).
 * Registered via the "float-next" IPC command (ipc.c) before the shell spawns a
 * menu window, consumed one-shot in mapnotify(). Deliberately a verbatim twin of
 * dockprep (modules/dock/dockprep.c): there is no compositor-side signal that
 * tells a menu-spawn from a keyboard-spawn (both arrive via the tmux spawn path),
 * so the shell arms the intent ahead of the spawn exactly as it does for docking.
 *
 * Self-contained: owns only the fixed-size floatprep_pending table below; touches
 * no compositor globals. Public (floatprep_register/floatprep_consume in kalin.h)
 * so ipc.c's separate TU can reach the register side.
 *
 * #include'd into dwl.c (a feature module in its translation unit, like
 * dockprep.c): the definitions land in dwl.c's TU, and dwl.c's forward-decls
 * (near dockprep's) satisfy mapnotify()'s earlier call. */

#define FLOATPREP_MAX 8
struct floatprep_entry {
	char appid[256];
	int used;
};
static struct floatprep_entry floatprep_pending[FLOATPREP_MAX];

void
floatprep_register(const char *appid)
{
	int i, slot = 0;

	if (!appid || !*appid)
		return;
	for (i = 0; i < FLOATPREP_MAX; i++) {
		if (floatprep_pending[i].used && strcmp(floatprep_pending[i].appid, appid) == 0) {
			slot = i;
			goto set;
		}
		if (!floatprep_pending[i].used) {
			slot = i;
			goto set;
		}
	}
	/* All slots full and none matched — overwrite slot 0 rather than drop the
	 * request silently; a stuck stale slot is worse than evicting one (same
	 * reasoning as dockprep_register). */
set:
	snprintf(floatprep_pending[slot].appid, sizeof(floatprep_pending[slot].appid), "%s", appid);
	floatprep_pending[slot].used = 1;
}

/* Matches and consumes (one-shot) a pending float-next request for `appid`.
 * Returns 1 on a match, 0 otherwise — carries no rect (unlike dockprep): the
 * float position is picked from the live cursor in mapnotify(), not pre-supplied. */
int
floatprep_consume(const char *appid)
{
	int i;

	if (!appid || !*appid)
		return 0;
	for (i = 0; i < FLOATPREP_MAX; i++) {
		if (floatprep_pending[i].used && strcmp(floatprep_pending[i].appid, appid) == 0) {
			floatprep_pending[i].used = 0;
			return 1;
		}
	}
	return 0;
}
