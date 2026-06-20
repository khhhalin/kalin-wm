/*
 * crash_report.h - Structured crash reporting and safe-mode recovery
 */

#ifndef CRASH_REPORT_H
#define CRASH_REPORT_H

#include <time.h>

typedef struct {
	time_t timestamp;
	int signal_num;
	const char *message;
	int fatal;
} CrashReport;

/* Initialize crash reporting system */
void crash_reporting_init(void);

/* Write a crash report to disk */
void crash_report_write(const char *msg, int sig);

/* Check for repeated crashes and enter safe mode if needed */
int crash_check_safe_mode(void);

/* Recovery path: skip certain expensive operations */
extern int safe_mode_enabled;

#endif /* CRASH_REPORT_H */
