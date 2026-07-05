/*
 * bind_engine.c - runtime bind table, dispatch, and hot reload (compositor side).
 *
 * Owns the live BindEngine, resolves input events against the active mode (with
 * fall-through to "default"), and calls dwl.c's bind_invoke() to run the action.
 * Bootstraps ~/.config/kalin-wm/binds.conf from an embedded default and watches
 * the directory with inotify so edits apply live. On a parse error the previous
 * table is retained (or, at first load, dispatch reports "inactive" so dwl.c
 * falls back to the compiled keys[]/buttons[]).
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

#include "binds.h"
#include "kalin.h"

/* The KMOD_* bits must match wlroots so we can compare against
 * wlr_keyboard_get_modifiers() directly. */
_Static_assert(KMOD_SHIFT == WLR_MODIFIER_SHIFT, "mod bit mismatch");
_Static_assert(KMOD_CTRL == WLR_MODIFIER_CTRL, "mod bit mismatch");
_Static_assert(KMOD_ALT == WLR_MODIFIER_ALT, "mod bit mismatch");
_Static_assert(KMOD_LOGO == WLR_MODIFIER_LOGO, "mod bit mismatch");
_Static_assert(KMOD_CAPS == WLR_MODIFIER_CAPS, "mod bit mismatch");

#define BIND_CLEANMASK(m) ((m) & ~KMOD_CAPS)

static BindEngine *g_engine;
/* dir is kept shorter than path so "<dir>/binds.conf" provably fits. */
static char g_config_dir[PATH_MAX - 64];
static char g_config_path[PATH_MAX];

/* Embedded default binds (shared with the parser unit test). */
#include "default_binds.h"

int
binds_active(void)
{
    return g_engine != NULL;
}

/* Run the action for a matched binding; ACT_MODE is handled here (mode switch),
 * everything else goes to the compositor's bind_invoke(). */
static void
run_binding(const Binding *b)
{
    if (b->action_id == ACT_MODE) {
        const char *target = b->arg.v;
        int i;
        for (i = 0; i < g_engine->nmodes; i++) {
            if (strcmp(g_engine->modes[i].name, target) == 0) {
                g_engine->active_mode = i;
                return;
            }
        }
        wlr_log(WLR_ERROR, "binds: mode '%s' not defined", target ? target : "?");
        return;
    }
    bind_invoke(b->action_id, &b->arg);
}

/* Scan one mode for a matching key/button/scroll binding. Returns 1 if run. */
static int
mode_dispatch(BindMode *mode, TriggerKind kind, uint32_t mods,
              xkb_keysym_t sym, uint32_t code)
{
    int i;
    for (i = 0; i < mode->count; i++) {
        Binding *b = &mode->binds[i];
        TriggerStep *s;
        if (!b->trig.active)
            continue;
        s = &b->trig.steps[0];
        if (s->kind != kind)
            continue;
        if (BIND_CLEANMASK(mods) != BIND_CLEANMASK(s->mods))
            continue;
        if (kind == TRIG_KEY) {
            if (xkb_keysym_to_lower(sym) != xkb_keysym_to_lower(s->keysym))
                continue;
        } else if (code != s->code) {
            continue;
        }
        run_binding(b);
        return 1;
    }
    return 0;
}

/* Try the active mode, then fall through to "default" (so global binds keep
 * working while a mode is active). */
static int
dispatch(TriggerKind kind, uint32_t mods, xkb_keysym_t sym, uint32_t code)
{
    if (!g_engine)
        return 0;
    if (mode_dispatch(&g_engine->modes[g_engine->active_mode], kind, mods, sym, code))
        return 1;
    if (g_engine->active_mode != 0
            && mode_dispatch(&g_engine->modes[0], kind, mods, sym, code))
        return 1;
    return 0;
}

int
bind_dispatch_key(uint32_t mods, xkb_keysym_t sym)
{
    return dispatch(TRIG_KEY, mods, sym, 0);
}

int
bind_dispatch_button(uint32_t mods, uint32_t button)
{
    return dispatch(TRIG_BUTTON, mods, XKB_KEY_NoSymbol, button);
}

int
bind_dispatch_scroll(uint32_t mods, uint32_t dir)
{
    return dispatch(TRIG_SCROLL, mods, XKB_KEY_NoSymbol, dir);
}

/* ===== modifier `tap` / `hold` gesture timing =====
 * A modifier-only bind fires on a gesture of its held modifier alone:
 *   - `hold`: after the modifier is held hold_ms uninterrupted; a paired release
 *     fires when the modifier lifts (while-held actions, e.g. the window menu).
 *   - `tap`: when the modifier is pressed and released within BIND_TAP_MAX_MS
 *     with no intervening key/button (a quick tap, e.g. the launcher).
 * Both are independent of the press-chord dispatch above; a single modifier
 * (e.g. Super) can carry both a tap and a hold bind at once. */
#define BIND_HOLD_DEFAULT_MS 1000
#define BIND_TAP_MAX_MS      250

static struct wl_event_source *hold_timer;
static Binding *hold_armed;   /* timing; fires on timeout if not interrupted */
static Binding *hold_shown;   /* fired; menu up, waiting for the modifier release */
static int hold_interrupted;
static Binding *tap_armed;    /* candidate quick-tap; fires on a prompt release */
static uint32_t tap_press_msec;

/* A modifier-only bind for the given edge whose mods match (active mode, then
 * default). */
static Binding *
find_gesture_bind(uint32_t mods, TriggerEdge edge)
{
    int pass, i;
    for (pass = 0; pass < 2; pass++) {
        BindMode *mode;
        if (pass == 0)
            mode = &g_engine->modes[g_engine->active_mode];
        else if (g_engine->active_mode != 0)
            mode = &g_engine->modes[0];
        else
            break;
        for (i = 0; i < mode->count; i++) {
            Binding *b = &mode->binds[i];
            TriggerStep *s;
            if (b->trig.nsteps != 1)
                continue;
            s = &b->trig.steps[0];
            if (s->edge != edge || s->kind != TRIG_KEY || s->keysym != XKB_KEY_NoSymbol)
                continue;
            if (BIND_CLEANMASK(mods) == BIND_CLEANMASK(s->mods))
                return b;
        }
    }
    return NULL;
}

static void
hold_disarm(void)
{
    if (hold_timer)
        wl_event_source_timer_update(hold_timer, 0);
    hold_armed = NULL;
}

static int
hold_timeout_cb(void *data)
{
    (void)data;
    if (hold_armed && !hold_interrupted) {
        bind_invoke(hold_armed->action_id, &hold_armed->arg);
        hold_shown = hold_armed;
    }
    hold_armed = NULL;
    return 0;
}

/* Cancel any arming tap/hold (a non-gesture input happened). */
void
bind_gesture_interrupt(void)
{
    hold_interrupted = 1;
    hold_disarm();
    tap_armed = NULL;
}

void
bind_gesture_key(uint32_t mods, int is_modifier_key, int pressed, uint32_t time_msec)
{
    if (!g_engine)
        return;

    if (pressed) {
        Binding *b;
        if (!is_modifier_key) {
            /* Any other key press interrupts an arming gesture. */
            bind_gesture_interrupt();
            return;
        }
        /* Modifier press/change: cancel if mods left the armed gesture. */
        if (hold_armed
                && BIND_CLEANMASK(mods) != BIND_CLEANMASK(hold_armed->trig.steps[0].mods))
            hold_disarm();
        if (tap_armed
                && BIND_CLEANMASK(mods) != BIND_CLEANMASK(tap_armed->trig.steps[0].mods))
            tap_armed = NULL;
        if (!hold_armed && !hold_shown && (b = find_gesture_bind(mods, EDGE_HOLD)) != NULL) {
            int ms = b->hold_ms > 0 ? b->hold_ms : BIND_HOLD_DEFAULT_MS;
            hold_armed = b;
            hold_interrupted = 0;
            if (!hold_timer)
                hold_timer = wl_event_loop_add_timer(event_loop, hold_timeout_cb, NULL);
            if (hold_timer)
                wl_event_source_timer_update(hold_timer, ms);
        }
        if (!tap_armed && (b = find_gesture_bind(mods, EDGE_TAP)) != NULL) {
            tap_armed = b;
            tap_press_msec = time_msec;
        }
    } else if (is_modifier_key) {
        /* Modifier release: cancel arming hold, hide a shown menu, or fire a tap
         * once the gesture modifier lifts. */
        if (hold_armed
                && BIND_CLEANMASK(mods) != BIND_CLEANMASK(hold_armed->trig.steps[0].mods))
            hold_disarm();
        if (hold_shown
                && BIND_CLEANMASK(mods) != BIND_CLEANMASK(hold_shown->trig.steps[0].mods)) {
            bind_invoke_release(hold_shown->action_id, &hold_shown->arg);
            hold_shown = NULL;
        }
        if (tap_armed
                && BIND_CLEANMASK(mods) != BIND_CLEANMASK(tap_armed->trig.steps[0].mods)) {
            if ((uint32_t)(time_msec - tap_press_msec) <= BIND_TAP_MAX_MS)
                bind_invoke(tap_armed->action_id, &tap_armed->arg);
            tap_armed = NULL;
        }
    }
}

static char *
read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;
    size_t n;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

int
binds_load(const char *path)
{
    char *text = read_file(path);
    char err[160] = {0};
    BindEngine *e;

    if (!text) {
        wlr_log(WLR_ERROR, "binds: cannot read %s: %s", path, strerror(errno));
        return -1;
    }
    e = bind_parse(text, err, sizeof(err));
    free(text);
    if (!e) {
        wlr_log(WLR_ERROR, "binds: parse error in %s: %s (keeping previous binds)",
                path, err);
        return -1;
    }
    /* hold_armed/hold_shown point into the engine we're about to free. Drop any
     * in-progress hold (hiding the menu first) so they can't dangle. */
    if (hold_shown)
        bind_invoke_release(hold_shown->action_id, &hold_shown->arg);
    hold_disarm();
    hold_shown = NULL;
    hold_interrupted = 0;
    tap_armed = NULL;

    bind_engine_free(g_engine);
    g_engine = e;
    wlr_log(WLR_INFO, "binds: loaded %s", path);
    return 0;
}

static int
on_inotify(int fd, uint32_t mask, void *data)
{
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t n;
    int reload = 0;
    char *p;
    (void)mask; (void)data;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (p = buf; p < buf + n; ) {
            struct inotify_event *ev = (struct inotify_event *)p;
            if (ev->len && strcmp(ev->name, "binds.conf") == 0)
                reload = 1;
            p += sizeof(struct inotify_event) + ev->len;
        }
    }
    if (reload)
        binds_load(g_config_path);
    return 0;
}

static void
mkdir_p(const char *path)
{
    char tmp[PATH_MAX];
    char *p;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

void
binds_init(void)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int ifd;
    if (xdg && xdg[0])
        snprintf(g_config_dir, sizeof(g_config_dir), "%s/kalin-wm", xdg);
    else if (home && home[0])
        snprintf(g_config_dir, sizeof(g_config_dir), "%s/.config/kalin-wm", home);
    else {
        wlr_log(WLR_ERROR, "binds: no HOME/XDG_CONFIG_HOME; using compiled defaults");
        return;
    }
    mkdir_p(g_config_dir);
    snprintf(g_config_path, sizeof(g_config_path), "%s/binds.conf", g_config_dir);

    if (access(g_config_path, F_OK) != 0) {
        FILE *f = fopen(g_config_path, "w");
        if (f != NULL) {
            fwrite(DEFAULT_BINDS, 1, sizeof(DEFAULT_BINDS) - 1, f);
            fclose(f);
            wlr_log(WLR_INFO, "binds: wrote default %s", g_config_path);
        } else {
            wlr_log(WLR_ERROR, "binds: cannot create %s: %s", g_config_path, strerror(errno));
        }
    }

    if (binds_load(g_config_path) != 0) {
        /* Fall back to the parsed embedded default so a broken user file still
         * boots with working keys. */
        char err[160] = {0};
        g_engine = bind_parse(DEFAULT_BINDS, err, sizeof(err));
        if (!g_engine)
            wlr_log(WLR_ERROR, "binds: embedded default failed to parse: %s", err);
    }

    ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (ifd < 0) {
        wlr_log(WLR_ERROR, "binds: inotify_init1: %s", strerror(errno));
        return;
    }
    if (inotify_add_watch(ifd, g_config_dir, IN_CLOSE_WRITE | IN_MOVED_TO) < 0) {
        wlr_log(WLR_ERROR, "binds: watch %s: %s", g_config_dir, strerror(errno));
        close(ifd);
        return;
    }
    wl_event_loop_add_fd(event_loop, ifd, WL_EVENT_READABLE, on_inotify, NULL);
}
