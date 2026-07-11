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
        "bind Super+Left -> focus left\n"
        "bind Super+Shift+plus -> resize height 40\n"
        "bind Ctrl+Alt+XF86Switch_VT_2 -> chvt 2\n"
        "bind Super+comma -> focus-monitor left\n"
        "bind Super+Ctrl+BTN_LEFT -> viewport.pan-grab\n"
        "bind Super+d -> opacity -0.1\n";
    BindEngine *e = bind_parse(cfg, err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) { fprintf(stderr, "    parse err: %s\n", err); return; }
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 9);
    /* spawn strv should be NULL-terminated with the split argv */
    Binding *ov = &d->binds[1];
    const char *const *strv = ov->arg.v;
    ASSERT(strcmp(strv[0], "qs") == 0 && strcmp(strv[1], "ipc") == 0);
    ASSERT(strv[5] == NULL);
    bind_engine_free(e);
}

static void
test_toggle_minimized_bind(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("bind Super+n -> toggle-minimized\n", err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) { fprintf(stderr, "    parse err: %s\n", err); return; }
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 1);
    ASSERT(d->binds[0].action_id == ACT_TOGGLE_MINIMIZED);
    ASSERT(d->binds[0].arg_kind == ARGK_NONE);
    bind_engine_free(e);
}

static void
test_action_repeatable(void)
{
    ASSERT(bind_action_is_repeatable(ACT_RESIZE));
    ASSERT(bind_action_is_repeatable(ACT_VIEWPORT_PAN));
    ASSERT(bind_action_is_repeatable(ACT_VIEWPORT_ZOOM));
    ASSERT(bind_action_is_repeatable(ACT_FOCUS_STACK));
    ASSERT(!bind_action_is_repeatable(ACT_TOGGLE_MAXIMIZED));
    ASSERT(!bind_action_is_repeatable(ACT_TOGGLE_FULLSCREEN));
    ASSERT(!bind_action_is_repeatable(ACT_TOGGLE_MINIMIZED));
    ASSERT(!bind_action_is_repeatable(ACT_TOGGLE_ONTOP));
}

static void
test_toggle_scratchpad_bind(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse(
        "bind Super+grave -> toggle-scratchpad foot --app-id=kalin-scratchpad\n",
        err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) { fprintf(stderr, "    parse err: %s\n", err); return; }
    BindMode *d = find_mode(e, "default");
    ASSERT(d && d->count == 1);
    ASSERT(d->binds[0].action_id == ACT_TOGGLE_SCRATCHPAD);
    ASSERT(d->binds[0].arg_kind == ARGK_STRV);
    {
        const char *const *strv = d->binds[0].arg.v;
        ASSERT(strcmp(strv[0], "foot") == 0);
        ASSERT(strcmp(strv[1], "--app-id=kalin-scratchpad") == 0);
        ASSERT(strv[2] == NULL);
    }
    bind_engine_free(e);
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
    /* The actual embedded default (written to the user's config) must parse
     * AND cover every action (bind_check_coverage()) — it's what a fresh
     * install boots with, so it has to be a genuinely complete config on its
     * own, not just syntactically valid. Catches a new ACT_* added to
     * bind_actions.c without a matching bind/unbind line added here. */
    char err[1024] = {0};
    BindEngine *e = bind_parse(DEFAULT_BINDS, err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) { fprintf(stderr, "    parse err: %s\n", err); return; }
    BindMode *d = find_mode(e, "default");
    /* Every default line is a single active key/button binding (no modes). */
    ASSERT(d && d->count > 50);
    ASSERT(bind_check_coverage(e, err, sizeof(err)) == 0);
    if (err[0]) fprintf(stderr, "    coverage err: %s\n", err);
    bind_engine_free(e);
}

static void
test_unbind_directive(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("unbind toggle-overlap\n", err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) { fprintf(stderr, "    parse err: %s\n", err); return; }
    ASSERT(e->unbound[ACT_TOGGLE_OVERLAP] == 1);
    ASSERT(e->unbound[ACT_LINK_PICK] == 0);
    bind_engine_free(e);
}

static void
test_unbind_unknown_action(void)
{
    char err[160] = {0};
    BindEngine *e = bind_parse("unbind not-a-real-action\n", err, sizeof(err));
    ASSERT(e == NULL);
}

static void
test_coverage_catches_missing_action(void)
{
    /* A single bind covers exactly one action; everything else is missing —
     * bind_check_coverage() must fail and name at least one of them. */
    char err[1024] = {0};
    BindEngine *e = bind_parse("bind Super+q -> close\n", err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) { fprintf(stderr, "    parse err: %s\n", err); return; }
    ASSERT(bind_check_coverage(e, err, sizeof(err)) != 0);
    ASSERT(strstr(err, "no bind or unbind") != NULL);
    bind_engine_free(e);
}

static void
test_coverage_bind_or_unbind_satisfies(void)
{
    /* Every action either bound or explicitly unbound: coverage passes even
     * though most of them are "unbind". */
    char cfg[4096] = "bind Super+q -> close\n";
    size_t off = strlen(cfg);
    char err[1024] = {0};
    BindEngine *e;
    int i;

    for (i = 0; i < ACT_COUNT; i++) {
        if (i == ACT_CLOSE)
            continue;
        off += (size_t)snprintf(cfg + off, sizeof(cfg) - off,
                "unbind %s\n", bind_action_name(i));
    }
    e = bind_parse(cfg, err, sizeof(err));
    ASSERT(e != NULL);
    if (!e) { fprintf(stderr, "    parse err: %s\n", err); return; }
    ASSERT(bind_check_coverage(e, err, sizeof(err)) == 0);
    if (err[0]) fprintf(stderr, "    coverage err: %s\n", err);
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
    RUN_TEST(toggle_minimized_bind);
    RUN_TEST(action_repeatable);
    RUN_TEST(toggle_scratchpad_bind);
    RUN_TEST(hold_bind);
    RUN_TEST(hold_bind_custom_ms);
    RUN_TEST(tap_bind);
    RUN_TEST(shipped_default_parses);
    RUN_TEST(unbind_directive);
    RUN_TEST(unbind_unknown_action);
    RUN_TEST(coverage_catches_missing_action);
    RUN_TEST(coverage_bind_or_unbind_satisfies);

    printf("\n===================================\n");
    printf("Results: %d passed, %d total\n", test_passed, test_total);
    printf("Total assertion failures: %d\n", total_failures);
    printf("===================================\n");
    return total_failures == 0 ? 0 : 1;
}
