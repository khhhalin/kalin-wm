# kalin-wm - Modular Wayland compositor based on dwl
# Makefile - Hybrid approach with gradual modularization

# Variables from environment or defaults
PREFIX    ?= /usr/local
BINDIR    ?= $(PREFIX)/bin
MANDIR    ?= $(PREFIX)/share/man/man1
WAYLAND_SESSION_DIR ?= /usr/share/wayland-sessions
BUILD_DIR ?= build

# Flags
WLR_FLAGS  = `pkg-config --cflags wlroots-0.20`
WLR_LIBS   = `pkg-config --libs wlroots-0.20`
WL_FLAGS   = `pkg-config --cflags wayland-server xkbcommon libinput`
WL_LIBS    = `pkg-config --libs wayland-server xkbcommon libinput`

CFLAGS   = $(WLR_FLAGS) $(WL_FLAGS) -I. -Icode/config -Icode/include -Icode/include/protocols -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L
CFLAGS  += -DVERSION=\"0.8-dev\"
CFLAGS  += -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement
CFLAGS  += -Wno-unused-parameter -Wshadow -Wunused-macros
CFLAGS  += -Werror=strict-prototypes -Werror=implicit -Werror=return-type
CFLAGS  += -Werror=incompatible-pointer-types -Wfloat-conversion -O1
# Auto-generate header/source dependencies so edits to any #include'd file
# (headers AND the modules dwl.c #include's directly) trigger a rebuild.
CFLAGS  += -MMD -MP

LDFLAGS  = $(WLR_LIBS) $(WL_LIBS) -lm

# Source files. dwl.c is the core translation unit; it #include's the feature
# modules under code/src/modules/{crop,layout,ui,viewport,input}/ directly.
# commit_size.c is compiled as its own translation unit. The other listed files
# (util/crash_report/persistence) are independent translation units.
SRCS = code/src/dwl.c code/src/util.c code/src/modules/input/commit_size.c code/src/modules/input/resize_actions.c code/src/modules/foreign_toplevel.c code/src/modules/viewport/viewport_ops.c code/src/modules/layout/layout_world.c code/src/modules/ui/wallpaper.c code/src/modules/crop/crop_mode.c code/src/crash_report.c code/src/persistence.c

OBJS = $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))
DEPS = $(OBJS:.o=.d)
BIN = $(BUILD_DIR)/kalin-wm

# Pull in auto-generated dependency files (-MMD). The '-' ignores them on a
# clean tree where they don't exist yet.
-include $(DEPS)

# Protocol files
PROTO_HDRS = code/include/protocols/xdg-shell-protocol.h \
	     code/include/protocols/wlr-layer-shell-unstable-v1-protocol.h \
	     code/include/protocols/wlr-output-power-management-unstable-v1-protocol.h \
	     code/include/protocols/pointer-constraints-unstable-v1-protocol.h \
	     code/include/protocols/cursor-shape-v1-protocol.h

# Main target
all: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(OBJS) $(CFLAGS) $(LDFLAGS) -o $@

kalin-wm: $(BIN)

# Pattern rules
$(BUILD_DIR)/%.o: %.c code/config/config.h $(PROTO_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Protocol header generation (wayland-scanner). System protocols come from the
# wayland-protocols data dir; wlr-* protocols are vendored under protocols/.
# These headers are .gitignore'd and regenerated on build, which keeps a clean
# checkout (and the hermetic `nix build`) self-contained.
WAYLAND_PROTOCOLS = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER   = wayland-scanner

code/include/protocols/xdg-shell-protocol.h:
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) server-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

code/include/protocols/cursor-shape-v1-protocol.h:
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) server-header $(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@

code/include/protocols/pointer-constraints-unstable-v1-protocol.h:
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) server-header $(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@

code/include/protocols/%-protocol.h: protocols/%.xml
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) server-header $< $@

# Config
check-config:
	@-[ -f code/config/config.h ] || cp code/config/config.def.h code/config/config.h

code/config/config.h: check-config

# Installation
install: $(BIN)
	install -D -m 755 $(BIN) $(DESTDIR)$(BINDIR)/kalin-wm
	install -D -m 644 docs/man/dwl.1 $(DESTDIR)$(MANDIR)/kalin-wm.1 2>/dev/null || true
	install -D -m 644 docs/desktop/dwl.desktop $(DESTDIR)$(WAYLAND_SESSION_DIR)/kalin-wm.desktop

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/kalin-wm
	rm -f $(DESTDIR)$(MANDIR)/kalin-wm.1
	rm -f $(DESTDIR)$(WAYLAND_SESSION_DIR)/kalin-wm.desktop

# Cleanup
clean:
	rm -rf $(BUILD_DIR)
	rm -f kalin-wm
	rm -f tests/test_client_lifecycle tests/*.gcda tests/*.gcno tests/*.gcov

distclean: clean
	rm -f code/config/config.h

# Development
debug: CFLAGS += -g3 -O0 -DDEBUG
debug: kalin-wm

release: CFLAGS += -O3 -DNDEBUG
release: kalin-wm

# Unit tests (no wlroots dependency)
test-unit:
	@echo "=== Running Unit Tests ==="
	@gcc -std=c99 -Wall -Wextra -Wshadow -O1 -g -o tests/test_client_lifecycle code/tests/test_client_lifecycle.c -lm && tests/test_client_lifecycle

test-unit-coverage:
	@echo "=== Running Unit Tests with Coverage ==="
	@rm -f tests/*.gcda tests/*.gcno tests/*.gcov && \
		gcc -std=c99 -Wall -Wextra -Wshadow -O1 -g --coverage -o tests/test_client_lifecycle code/tests/test_client_lifecycle.c -lm && \
		tests/test_client_lifecycle && \
		gcov -b code/tests/test_client_lifecycle.c >/dev/null && \
		echo "Coverage report: test_client_lifecycle.c.gcov"

test-integration:
	@echo "=== Running Integration Tests ==="
	@cd tests && bash test_spawn_crash.sh

# Testing in TTY (requires root/sudo or running from TTY)
test-tty: $(BIN)
	./scripts/run-tty

test-manual:
	@echo "Manual test checklist: docs/MANUAL_TESTING.md"
	@echo "Quick start (nested): ./scripts/dev/run-nested-safe.sh"
	@echo "Quick start (tty):    ./scripts/run-tty"

test: test-unit

.PHONY: all kalin-wm clean distclean install uninstall debug release test test-unit test-unit-coverage test-integration test-tty test-manual check-config

# Show modular structure
structure:
	@echo "=== Project Structure ==="
	@echo "Main compositor: code/src/dwl.c"
	@echo "Modular headers: code/include/*.h"
	@echo "Protocol headers: code/include/protocols/*.h"
	@echo "Config files: code/config/"
	@echo "Docs hub: docs/ (man/ desktop/ changelog/ incidents/ obsidian-vault/)"
	@echo ""
	@echo "Current build: code/src/dwl.c + code/src/util.c + selected runtime modules"
	@echo "Migration path: continue splitting code/src/dwl.c into code/src/modules/*.c translation units"

.PHONY: structure
