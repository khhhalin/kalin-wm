/*
 * crash_report.c - Structured crash reporting and safe-mode recovery
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>
#include <errno.h>

#include "crash_report.h"

#define CRASH_DIR "~/.local/share/kalin-wm/crashes"
#define MAX_CRASH_REPORTS 10
#define CRASH_REPEAT_WINDOW_SECS 60

int safe_mode_enabled = 0;

static char *
expand_home(const char *path)
{
	static char buf[512];
	const char *home = getenv("HOME");
	if (!home)
		home = "/root";
	if (path[0] == '~') {
		snprintf(buf, sizeof(buf), "%s%s", home, path + 1);
		return buf;
	}
	return (char *)path;
}

void
crash_reporting_init(void)
{
	char *crash_dir = expand_home(CRASH_DIR);
	mkdir(crash_dir, 0755);
	/* ignore errors; directory may already exist */
}

void
crash_report_write(const char *msg, int sig)
{
	char *crash_dir = expand_home(CRASH_DIR);
	char filepath[512];
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	FILE *f;

	mkdir(crash_dir, 0755);

	/* Write timestamped crash report */
	snprintf(filepath, sizeof(filepath), "%s/crash_%04d%02d%02d_%02d%02d%02d.log",
		crash_dir,
		tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
		tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

	f = fopen(filepath, "w");
	if (!f)
		return;

	fprintf(f, "=== kalin-wm Crash Report ===\n");
	fprintf(f, "Time: %s", ctime(&now));
	fprintf(f, "Signal: %d\n", sig);
	fprintf(f, "Message: %s\n", msg ? msg : "(none)");
	fprintf(f, "Version: %s\n", VERSION);
	fclose(f);
}

int
crash_check_safe_mode(void)
{
	char *crash_dir = expand_home(CRASH_DIR);
	int recent_crashes = 0;

	/* Heuristic: count .log files in crash dir modified in last CRASH_REPEAT_WINDOW_SECS */
	/* For simplicity, just check if crash dir has any recent files and count them */
	struct stat st;
	FILE *fp;
	char line[256];

	if (stat(crash_dir, &st) < 0)
		return 0; /* no crash dir yet */

	/* Simple approach: read crash reports and check timestamps */
	/* Just count the last N crash files as a simple heuristic */
	/* If 3+ crashes in quick succession, enable safe mode */
	fp = popen("ls -1t ~/.local/share/kalin-wm/crashes/crash_*.log 2>/dev/null | wc -l", "r");
	if (fp) {
		if (fgets(line, sizeof(line), fp)) {
			recent_crashes = atoi(line);
		}
		pclose(fp);
	}

	if (recent_crashes >= 3) {
		safe_mode_enabled = 1;
		return 1;
	}

	return 0;
}
