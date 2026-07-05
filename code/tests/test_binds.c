/* Unit tests for the bind DSL parser + action registry (no wlroots). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binds.h"
#include "default_binds.h"

static int test_failures = 0;
static int total_failures = 0;
static int test_passed = 0;
static int test_total = 0;

#define RUN_TEST(name) do { \
    printf("  Running " #name "... "); \
    test_total++; \
    test_failures = 0; \
    test_##name(); \
    if (test_failures == 0) { printf("PASS\n"); test_passed++; } \
    else { printf("FAIL (%d failures)\n", test_failures); total_failures += test_failures; } \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "\n    ASSERT FAIL: " #cond " at line %d\n", __LINE__); \
        test_failures++; \
    } \
} while (0)

/* Find a mode by name; NULL if absent. */
static BindMode *
find_mode(BindEngine *e, const char *name)
{
    for (int i = 0; i < e->nmodes; i++)
        if (strcmp(e->modes[i].name, name) == 0)
            return &e->modes[i];
    return NULL;
}

static void
test_simple_chord(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind Super+Shift+k -> close\n", err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) return;
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 1);
    Binding *b = &d->binds[0];
    ASSERT(b->action_id == ACT_CLOSE);
    ASSERT(b->trig.nsteps == 1);
    ASSERT(b->trig.active == 1);
    ASSERT(b->trig.steps[0].kind == TRIG_KEY);
    ASSERT(b->trig.steps[0].mods == (KMOD_LOGO | KMOD_SHIFT));
    ASSERT(b->trig.steps[0].keysym == XKB_KEY_k);
    bind_engine_free(e);
}

static void
test_literal_keys(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind Super+= -> resize width 40\n"
                               "bind Super+- -> resize width -40\n", err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) return;
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 2);
    ASSERT(d->binds[0].trig.steps[0].keysym == XKB_KEY_equal);
    ASSERT(d->binds[1].trig.steps[0].keysym == XKB_KEY_minus);
    /* resize width 40 -> int[2] {40, 0} */
    const int *pair0 = d->binds[0].arg.v;
    const int *pair1 = d->binds[1].arg.v;
    ASSERT(pair0[0] == 40 && pair0[1] == 0);
    ASSERT(pair1[0] == -40 && pair1[1] == 0);
    bind_engine_free(e);
}

static void
test_pointer_and_scroll(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind Super+BTN_LEFT -> pointer-move\n"
                               "bind Super+ScrollUp -> viewport.zoom 1.1\n", err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) return;
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 2);
    ASSERT(d->binds[0].trig.steps[0].kind == TRIG_BUTTON);
    ASSERT(d->binds[0].trig.steps[0].code == KBTN_LEFT);
    ASSERT(d->binds[0].action_id == ACT_POINTER_MOVE);
    ASSERT(d->binds[1].trig.steps[0].kind == TRIG_SCROLL);
    ASSERT(d->binds[1].trig.steps[0].code == SCROLL_UP);
    ASSERT(d->binds[1].action_id == ACT_VIEWPORT_ZOOM);
    bind_engine_free(e);
}

static void
test_mode_block(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind Super+r -> mode resize\n"
                               "mode resize {\n"
                               "  bind h -> resize width -40\n"
                               "  bind Escape -> mode default\n"
                               "}\n"
                               "bind Super+q -> close\n", err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) return;
    BindMode *d = find_mode(e, "default");
    BindMode *r = find_mode(e, "resize");
    ASSERT(d && r);
    /* default has the two Super+ binds; resize has two. */
    ASSERT(d && d->count == 2);
    ASSERT(r && r->count == 2);
    ASSERT(d->binds[0].action_id == ACT_MODE);
    ASSERT(strcmp((const char *)d->binds[0].arg.v, "resize") == 0);
    ASSERT(r->binds[0].trig.steps[0].keysym == XKB_KEY_h);
    bind_engine_free(e);
}

static void
test_leader_sequence_parsed_inactive(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind Super+g s -> screenshot\n", err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) return;
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 1);
    ASSERT(d->binds[0].trig.nsteps == 2);
    ASSERT(d->binds[0].trig.active == 0);   /* sequences not dispatched in Stage 1 */
    bind_engine_free(e);
}

static void
test_tap_hold_and_gamepad_inactive(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind tap Super -> spawn menu\n"
                               "gamepad South -> spawn terminal\n", err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) return;
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 2);
    ASSERT(d->binds[0].trig.steps[0].edge == EDGE_TAP);
    ASSERT(d->binds[0].trig.active == 0);
    ASSERT(d->binds[1].trig.steps[0].kind == TRIG_GAMEPAD);
    ASSERT(d->binds[1].trig.active == 0);
    bind_engine_free(e);
}

static void
test_comments_and_blanks(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("# a comment\n"
                               "\n"
                               "bind Super+q -> close  # trailing comment\n", err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) return;
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 1);
    bind_engine_free(e);
}

static void
test_error_unknown_action(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind Super+q -> frobnicate\n", err, sizeof(err));
    ASSERT(e == NULL);
    ASSERT(strstr(err, "frobnicate") != NULL);
}

static void
test_error_unknown_key(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind Super+Nonsense123 -> close\n", err, sizeof(err));
    ASSERT(e == NULL);
}

static void
test_error_bad_modifier(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind Hyper+q -> close\n", err, sizeof(err));
    ASSERT(e == NULL);
}

static void
test_error_unclosed_mode(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("mode resize {\n  bind h -> close\n", err, sizeof(err));
    ASSERT(e == NULL);
    ASSERT(strstr(err, "unclosed") != NULL);
}

static void
test_error_missing_arrow(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind Super+q close\n", err, sizeof(err));
    ASSERT(e == NULL);
}

static void
test_error_bad_arg(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind Super+= -> resize sideways 40\n", err, sizeof(err));
    ASSERT(e == NULL);
}

static void
test_default_binds_full_parse(void)
{
    /* A representative slice of the shipped default set must parse cleanly. */
    char err[160] = {0};
    const char *cfg =
        "bind Super+p -> spawn fuzzel\n"
        "bind Super+o -> spawn qs ipc call windows-bar toggleOverview\n"
        "bind Super+Ctrl+equal -> viewport.zoom 1.1\n"
        "bind Ctrl+Left -> move-column left\n"
        "bind Super+Left -> focus left\n"
        "bind Super+Shift+plus -> resize height 40\n"
        "bind Ctrl+Alt+XF86Switch_VT_2 -> chvt 2\n"
        "bind Super+comma -> focus-monitor left\n"
        "bind Super+i -> layout infinite\n"
        "bind Super+d -> opacity -0.1\n";
    BindEngine *e = bind_parse(cfg, err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) { fprintf(stderr, "    parse err: %s\n", err); return; }
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 10);
    /* spawn strv should be NULL-terminated with the split argv */
    Binding *ov = &d->binds[1];
    const char *const *strv = ov->arg.v;
    ASSERT(strcmp(strv[0], "qs") == 0 && strcmp(strv[1], "ipc") == 0);
    ASSERT(strv[5] == NULL);
    bind_engine_free(e);
}

static void
test_move_window_dir(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind Super+Ctrl+Up -> move-window up\n"
                               "bind Super+Ctrl+Left -> move-window left\n", err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) return;
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 2);
    ASSERT(d->binds[0].action_id == ACT_MOVE_WINDOW);
    ASSERT(d->binds[0].arg.i == 2);   /* up  -> DIR_UP */
    ASSERT(d->binds[1].arg.i == 0);   /* left -> DIR_LEFT */
    bind_engine_free(e);
}

static void
test_move_window_bad_dir(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind Super+Ctrl+Up -> move-window sideways\n", err, sizeof(err));
    ASSERT(e == NULL);
}

static void
test_hold_bind(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind hold Super -> window-menu\n", err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) return;
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 1);
    ASSERT(d->binds[0].action_id == ACT_WINDOW_MENU);
    ASSERT(d->binds[0].hold_ms == 0);   /* default */
    ASSERT(d->binds[0].trig.steps[0].edge == EDGE_HOLD);
    ASSERT(d->binds[0].trig.steps[0].kind == TRIG_KEY);
    ASSERT(d->binds[0].trig.steps[0].keysym == XKB_KEY_NoSymbol);
    ASSERT(d->binds[0].trig.steps[0].mods == KMOD_LOGO);
    ASSERT(d->binds[0].trig.active == 0);   /* hold is not press-dispatched */
    bind_engine_free(e);
}

static void
test_hold_bind_custom_ms(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind hold 1500 Super -> window-menu\n", err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) return;
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 1);
    ASSERT(d->binds[0].hold_ms == 1500);
    ASSERT(d->binds[0].trig.steps[0].edge == EDGE_HOLD);
    bind_engine_free(e);
}

static void
test_tap_bind(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind tap Super -> toggle-launcher fuzzel\n",
                               err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) return;
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 1);
    ASSERT(d->binds[0].action_id == ACT_TOGGLE_LAUNCHER);
    ASSERT(d->binds[0].trig.steps[0].edge == EDGE_TAP);
    ASSERT(d->binds[0].trig.steps[0].kind == TRIG_KEY);
    ASSERT(d->binds[0].trig.steps[0].keysym == XKB_KEY_NoSymbol);
    ASSERT(d->binds[0].trig.steps[0].mods == KMOD_LOGO);
    ASSERT(d->binds[0].trig.active == 0);   /* tap is not press-dispatched */
    bind_engine_free(e);
}

static void
test_shipped_default_parses(void)
{
    /* The actual embedded default (written to the user's config) must parse. */
    char err[160] = {0};
    BindEngine *e = bind_parse(DEFAULT_BINDS, err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) { fprintf(stderr, "    parse err: %s\n", err); return; }
    BindMode *d = find_mode(e, "default");
    /* Every default line is a single active key/button binding (no modes). */
    ASSERT(d && d->count > 50);
    bind_engine_free(e);
}

int
main(void)
{
    printf("=== Bind DSL Tests ===\n");
    RUN_TEST(simple_chord);
    RUN_TEST(literal_keys);
    RUN_TEST(pointer_and_scroll);
    RUN_TEST(mode_block);
    RUN_TEST(leader_sequence_parsed_inactive);
    RUN_TEST(tap_hold_and_gamepad_inactive);
    RUN_TEST(comments_and_blanks);
    RUN_TEST(error_unknown_action);
    RUN_TEST(error_unknown_key);
    RUN_TEST(error_bad_modifier);
    RUN_TEST(error_unclosed_mode);
    RUN_TEST(error_missing_arrow);
    RUN_TEST(error_bad_arg);
    RUN_TEST(default_binds_full_parse);
    RUN_TEST(move_window_dir);
    RUN_TEST(move_window_bad_dir);
    RUN_TEST(hold_bind);
    RUN_TEST(hold_bind_custom_ms);
    RUN_TEST(tap_bind);
    RUN_TEST(shipped_default_parses);

    printf("\n===================================\n");
    printf("Results: %d passed, %d total\n", test_passed, test_total);
    printf("Total assertion failures: %d\n", total_failures);
    printf("===================================\n");
    return total_failures == 0 ? 0 : 1;
}
