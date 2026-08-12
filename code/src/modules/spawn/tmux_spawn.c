/* App launching via a persistent tmux session ("kalin-apps", bootstrapped in
 * run()): every spawned command becomes a tmux *window*, so its stdout/stderr
 * is inspectable live (`tmux attach -t kalin-apps`) and tmux's own window
 * list / kill-window is the management interface — the compositor never tracks
 * raw child pids for that purpose.
 *
 * #include'd into dwl.c (a feature module in its translation unit, like
 * ui/offscreen_indicators.c): the functions stay in dwl.c's TU, so their
 * forward declarations there satisfy the callers (bind_invoke / togglescratchpad)
 * and no headers are needed here. */

/* Synchronously ask tmux to kill a window in the persistent "kalin-apps"
 * session (see run()'s bootstrap and spawn_named() below) — returns 1 if it
 * existed (and is now gone), 0 if it didn't (tmux exits non-zero for "no
 * such window"). Blocks briefly (a local tmux control-mode round trip, not
 * network I/O): acceptable for a one-off, user-initiated toggle, unlike the
 * fire-and-forget spawns this pairs with. */
static int
tmux_kill_window(const char *window_name)
{
	pid_t pid;
	int status;
	char target[128];

	snprintf(target, sizeof(target), "kalin-apps:%s", window_name);

	pid = fork();
	if (pid < 0) {
		wlr_log(WLR_ERROR, "Failed to fork for tmux kill-window: %s", strerror(errno));
		return 0;
	}
	if (pid == 0) {
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
		}
		execlp("tmux", "tmux", "kill-window", "-t", target, NULL);
		_exit(1);
	}
	if (waitpid(pid, &status, 0) < 0)
		return 0;
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Fork, and in the child re-exec `arg->v` as a new tmux window named
 * `window_name` inside the persistent "kalin-apps" session (bootstrapped in
 * run()) instead of exec'ing it directly. Every launched app's stdout/
 * stderr becomes visible live via `tmux attach -t kalin-apps`, and tmux's
 * own window list / kill-window become the management interface — no more
 * tracking raw pids in the compositor for that purpose. The child becomes a
 * short-lived tmux client (exits once the window is created); the actual
 * command keeps running as a child of the tmux *server*, so its real pid is
 * never visible to us here, which is fine since nothing needs it. */
static void
spawn_named(const Arg *arg, const char *window_name)
{
	pid_t pid;
	const char *cmd = ((char **)arg->v)[0];
	int errpipe[2];

	if (pipe(errpipe) < 0) {
		wlr_log(WLR_ERROR, "Failed to create spawn pipe: %s", strerror(errno));
		return;
	}
	if (fcntl(errpipe[1], F_SETFD, FD_CLOEXEC) < 0) {
		wlr_log(WLR_ERROR, "Failed to set CLOEXEC on spawn pipe: %s", strerror(errno));
		close(errpipe[0]);
		close(errpipe[1]);
		return;
	}

	pid = fork();
	if (pid < 0) {
		wlr_log(WLR_ERROR, "Failed to fork for spawn: %s", strerror(errno));
		close(errpipe[0]);
		close(errpipe[1]);
		return;
	}

	if (pid == 0) {
		char *targv[32];
		size_t i, n;
		int err;

		close(errpipe[0]);
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();

		targv[0] = "tmux";
		targv[1] = "new-window";
		targv[2] = "-t";
		targv[3] = "kalin-apps";
		targv[4] = "-n";
		targv[5] = (char *)window_name;
		targv[6] = "--";
		n = 7;
		for (i = 0; ((char **)arg->v)[i] && n < LENGTH(targv) - 1; i++, n++)
			targv[n] = ((char **)arg->v)[i];
		targv[n] = NULL;

		execvp("tmux", targv);
		err = errno;
		/* Best-effort: report exec failure to the parent. Nothing we can
		 * do in the doomed child if the write itself fails. */
		if (write(errpipe[1], &err, sizeof(err)) < 0) { /* ignore */ }
		wlr_log(WLR_ERROR, "Failed to exec tmux for %s: %s", cmd, strerror(err));
		_exit(1);
	}

	close(errpipe[1]);
	{
		struct pollfd pfd = {.fd = errpipe[0], .events = POLLIN};
		int pr = poll(&pfd, 1, 100);
		if (pr > 0 && (pfd.revents & POLLIN)) {
			int err = 0;
			ssize_t n = read(errpipe[0], &err, sizeof(err));
			if (n == (ssize_t)sizeof(err))
				wlr_log(WLR_ERROR, "Spawn failed for %s: %s", cmd, strerror(err));
		}
	}
	close(errpipe[0]);

	wlr_log(WLR_DEBUG, "Spawned %s as tmux window '%s' in kalin-apps (client pid %d)", cmd, window_name, pid);
}

void
spawn(const Arg *arg)
{
	spawn_named(arg, ((char **)arg->v)[0]);
}
