/* Backlight brightness control via logind's SetBrightness D-Bus method.
 *
 * The sysfs backlight brightness file (/sys/class/backlight/<name>/
 * brightness) is root-owned with no group-write bit on a stock NixOS
 * install — no udev rule grants the video group write access the way some
 * distros' default rules do, and brightnessctl itself fails to set (only
 * read) brightness here for the same reason. Rather than add a udev rule
 * (a separate system-config change + rebuild), go through
 * systemd-logind: it already runs as root and exposes exactly this as an
 * authenticated per-session D-Bus method, so no permission workaround is
 * needed at all — the standard modern-Linux path for this, not a hack.
 *
 * Separately-compiled TU: links against dwl.c's externed ipc_broadcast_state
 * (kalin.h) so a brightness change shows up in the next state broadcast the
 * same tick it takes effect. */
#include "kalin.h"

#include <dirent.h>
#include <systemd/sd-bus.h>

/* Resolving the backlight device name and our own logind session object
 * path both need a filesystem scan / D-Bus round trip — cache them after
 * the first successful call rather than repeating that on every brightness
 * change (there's normally exactly one backlight device and it never moves
 * mid-session). */
static char backlight_name[64];
static char session_path[128];
static int backlight_ready;

static int
backlight_find_device(void)
{
	DIR *d;
	struct dirent *ent;

	d = opendir("/sys/class/backlight");
	if (!d)
		return 0;
	while ((ent = readdir(d))) {
		int n;
		if (ent->d_name[0] == '.')
			continue;
		/* Real backlight device names are short (e.g. intel_backlight);
		 * skip any that wouldn't fit rather than cache a truncated name
		 * that later sysfs-path lookups would fail on. */
		n = snprintf(backlight_name, sizeof(backlight_name), "%s", ent->d_name);
		if (n < 0 || (size_t)n >= sizeof(backlight_name))
			continue;
		closedir(d);
		return 1;
	}
	closedir(d);
	return 0;
}

static int
backlight_resolve_session(void)
{
	sd_bus *bus = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	const char *path;
	int ret;

	if (sd_bus_open_system(&bus) < 0)
		return 0;

	/* GetSessionByPID(0) resolves "the session this process belongs to"
	 * without needing to already know $XDG_SESSION_ID — the normal path
	 * when kalin-wm is launched directly from a real login session's shell
	 * (its own PID is naturally part of that session's cgroup). */
	ret = sd_bus_call_method(bus,
			"org.freedesktop.login1", "/org/freedesktop/login1",
			"org.freedesktop.login1.Manager", "GetSessionByPID",
			&error, &reply, "u", 0u);
	if (ret >= 0) {
		sd_bus_message_read(reply, "o", &path);
		snprintf(session_path, sizeof(session_path), "%s", path);
		sd_bus_message_unref(reply);
		sd_bus_unref(bus);
		return 1;
	}
	wlr_log(WLR_DEBUG, "backlight: GetSessionByPID failed (%s), falling back to ListSessions",
			error.message);
	sd_bus_error_free(&error);

	/* Fallback: the calling process isn't itself part of any tracked
	 * session (e.g. relaunched from a detached/out-of-band shell during
	 * development rather than the login session's own terminal) — pick the
	 * first real seated session (non-empty seat_id, i.e. not a headless
	 * systemd-user manager scope) instead of giving up. Single-user desktop
	 * assumption: fine for "control the physical display's backlight",
	 * wrong for a genuine multi-seat/multi-user host. */
	ret = sd_bus_call_method(bus,
			"org.freedesktop.login1", "/org/freedesktop/login1",
			"org.freedesktop.login1.Manager", "ListSessions",
			&error, &reply, "");
	if (ret < 0) {
		wlr_log(WLR_ERROR, "backlight: ListSessions failed: %s", error.message);
		sd_bus_error_free(&error);
		sd_bus_unref(bus);
		return 0;
	}

	ret = sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "(susso)");
	if (ret < 0) {
		sd_bus_message_unref(reply);
		sd_bus_unref(bus);
		return 0;
	}
	while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_STRUCT, "susso") > 0) {
		const char *sid, *uname, *seat_id;
		uint32_t uid;
		sd_bus_message_read(reply, "susso", &sid, &uid, &uname, &seat_id, &path);
		if (seat_id && *seat_id)
			snprintf(session_path, sizeof(session_path), "%s", path);
		sd_bus_message_exit_container(reply);
		if (seat_id && *seat_id)
			break;
	}
	sd_bus_message_exit_container(reply);
	sd_bus_message_unref(reply);
	sd_bus_unref(bus);

	return session_path[0] != '\0';
}

static int
backlight_ensure_ready(void)
{
	if (backlight_ready)
		return 1;
	backlight_ready = backlight_find_device() && backlight_resolve_session();
	return backlight_ready;
}

/* Reads the current brightness and max brightness (plain sysfs reads —
 * unlike writing, these are world-readable, no permission issue). Returns 1
 * on success, 0 if no backlight device was found or either file couldn't be
 * read. */
int
backlight_get(int *value, int *max)
{
	char path[160];
	FILE *f;

	if (!backlight_ensure_ready())
		return 0;

	snprintf(path, sizeof(path), "/sys/class/backlight/%s/brightness", backlight_name);
	f = fopen(path, "r");
	if (!f)
		return 0;
	if (fscanf(f, "%d", value) != 1) {
		fclose(f);
		return 0;
	}
	fclose(f);

	snprintf(path, sizeof(path), "/sys/class/backlight/%s/max_brightness", backlight_name);
	f = fopen(path, "r");
	if (!f)
		return 0;
	if (fscanf(f, "%d", max) != 1) {
		fclose(f);
		return 0;
	}
	fclose(f);
	return 1;
}

/* Sets brightness to an absolute raw value (0..max_brightness from
 * backlight_get() above) via logind. Returns 1 on success, 0 otherwise. */
int
backlight_set(int value)
{
	sd_bus *bus = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int ret;

	if (!backlight_ensure_ready())
		return 0;
	if (sd_bus_open_system(&bus) < 0)
		return 0;

	ret = sd_bus_call_method(bus,
			"org.freedesktop.login1", session_path,
			"org.freedesktop.login1.Session", "SetBrightness",
			&error, NULL, "ssu", "backlight", backlight_name, (uint32_t)value);
	if (ret < 0)
		wlr_log(WLR_ERROR, "backlight: SetBrightness failed: %s", error.message);
	sd_bus_error_free(&error);
	sd_bus_unref(bus);

	if (ret >= 0)
		status_mark_dirty();
	return ret >= 0;
}
