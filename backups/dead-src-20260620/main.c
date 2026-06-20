#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>

#include "../include/kalin.h"
#include "../include/compositor.h"

/* Version string */
#define VERSION "0.8-dev"

/* Logging level */
int log_level = WLR_ERROR;

static void
usage(const char *name)
{
	fprintf(stderr, "Usage: %s [options] [startup command]\n", name);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -h          Show this help message\n");
	fprintf(stderr, "  -v          Show version\n");
	fprintf(stderr, "  -d          Debug mode (verbose logging)\n");
	fprintf(stderr, "  -s cmd      Startup command to run\n");
}

int
main(int argc, char *argv[])
{
	char *startup_cmd = NULL;
	int opt;

	/* Parse command line options */
	while ((opt = getopt(argc, argv, "hvs:d")) != -1) {
		switch (opt) {
		case 'h':
			usage(argv[0]);
			return 0;
		case 'v':
			printf("kalin-wm version %s\n", VERSION);
			return 0;
		case 'd':
			log_level = WLR_DEBUG;
			break;
		case 's':
			startup_cmd = optarg;
			break;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	/* Remaining arguments are the startup command */
	if (optind < argc) {
		/* Join remaining args into startup command */
		int len = 0;
		for (int i = optind; i < argc; i++)
			len += strlen(argv[i]) + 1;
		
		startup_cmd = malloc(len);
		if (startup_cmd) {
			startup_cmd[0] = '\0';
			for (int i = optind; i < argc; i++) {
				if (i > optind)
					strcat(startup_cmd, " ");
				strcat(startup_cmd, argv[i]);
			}
		}
	}

	/* Check for WAYLAND_DISPLAY to avoid nested compositors */
	if (getenv("WAYLAND_DISPLAY") || getenv("DISPLAY")) {
		fprintf(stderr, "Warning: Running compositor inside another compositor.\n");
	}

	/* Initialize everything */
	setup();

	/* Run the compositor */
	run(startup_cmd);

	/* Cleanup */
	cleanup();

	free(startup_cmd);

	return 0;
}
