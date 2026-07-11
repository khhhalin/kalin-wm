/*
 * bind_parser.c - lexer + parser for the kalin bind DSL.
 *
 * Grammar (one directive per line, '#' starts a comment):
 *   bind [tap|hold] <trigger-steps...> -> <action> [args...]
 *   unbind <action-name>                         # explicitly no key for this
 *   gamepad <trigger> -> <action> [args...]      # parsed, dispatched later
 *   mode <name> { ... }                          # block of bind lines
 *
 * A trigger step is `Mod+Mod+Key`; multiple space-separated steps form a leader
 * sequence. Key is an xkb keysym name, a punctuation glyph, a mouse button
 * (BTN_LEFT / Mouse1..3), or ScrollUp/Down/Left/Right. See default binds.
 *
 * Coverage: every action in the registry (bind_actions.c) should be covered
 * by at least one "bind" line (any mode) or by "unbind" — see
 * bind_check_coverage() below, called separately from bind_parse() itself
 * (not baked into parsing — see that function's comment for why) by the real
 * runtime loader. This exists so a config can't silently drift out of sync
 * with the compositor's own evolving action set: a config written against an
 * older binary just keeps compiling and running, quietly missing whatever
 * actions got added since, with no signal that anything's wrong. "unbind" is
 * the escape hatch for "I know about this action, I deliberately don't want
 * it on a key" — the point isn't that everything must be bound, it's that
 * every action must be a *conscious* decision, not an oversight.
 *
 * Pure C + xkbcommon only (unit-testable). Builds a heap BindEngine.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "binds.h"

#define MAX_TOKENS 64

/* ---- engine construction ---- */

static BindEngine *
engine_new(void)
{
    BindEngine *e = calloc(1, sizeof(*e));
    if (!e)
        return NULL;
    e->modes = calloc(1, sizeof(BindMode));
    if (!e->modes) {
        free(e);
        return NULL;
    }
    e->nmodes = 1;
    e->modes[0].name = strdup("default");
    if (!e->modes[0].name) {
        free(e->modes);
        free(e);
        return NULL;
    }
    return e;
}

void
bind_engine_free(BindEngine *e)
{
    int m, b, i;

    if (!e)
        return;
    for (m = 0; m < e->nmodes; m++) {
        BindMode *mode = &e->modes[m];
        for (b = 0; b < mode->count; b++) {
            Binding *bd = &mode->binds[b];
            free(bd->trig.steps);
            if (bd->arg_kind == ARGK_STRV && bd->owned) {
                char **strv = bd->owned;
                for (i = 0; strv[i]; i++)
                    free(strv[i]);
            }
            free(bd->owned);
        }
        free(mode->binds);
        free(mode->name);
    }
    free(e->modes);
    free(e);
}

static int
mode_find_or_add(BindEngine *e, const char *name)
{
    BindMode *grown, *nm;
    int i;

    for (i = 0; i < e->nmodes; i++)
        if (strcmp(e->modes[i].name, name) == 0)
            return i;
    grown = realloc(e->modes, sizeof(BindMode) * (size_t)(e->nmodes + 1));
    if (!grown)
        return -1;
    e->modes = grown;
    nm = &e->modes[e->nmodes];
    memset(nm, 0, sizeof(*nm));
    nm->name = strdup(name);
    if (!nm->name)
        return -1;
    return e->nmodes++;
}

static Binding *
mode_add_binding(BindMode *mode)
{
    Binding *b;

    if (mode->count == mode->cap) {
        int ncap = mode->cap ? mode->cap * 2 : 8;
        Binding *grown = realloc(mode->binds, sizeof(Binding) * (size_t)ncap);
        if (!grown)
            return NULL;
        mode->binds = grown;
        mode->cap = ncap;
    }
    b = &mode->binds[mode->count];
    memset(b, 0, sizeof(*b));
    return b;
}

/* ---- token helpers ---- */

static xkb_keysym_t
literal_keysym(const char *tok)
{
    /* Single punctuation glyphs -> their xkb keysym names. */
    if (strlen(tok) == 1) {
        switch (tok[0]) {
        case '=': return XKB_KEY_equal;
        case '-': return XKB_KEY_minus;
        case '+': return XKB_KEY_plus;
        case '_': return XKB_KEY_underscore;
        case '[': return XKB_KEY_bracketleft;
        case ']': return XKB_KEY_bracketright;
        case '{': return XKB_KEY_braceleft;
        case '}': return XKB_KEY_braceright;
        case ',': return XKB_KEY_comma;
        case '.': return XKB_KEY_period;
        case '<': return XKB_KEY_less;
        case '>': return XKB_KEY_greater;
        case '/': return XKB_KEY_slash;
        case ';': return XKB_KEY_semicolon;
        case '\'': return XKB_KEY_apostrophe;
        case '`': return XKB_KEY_grave;
        default: break;
        }
    }
    return xkb_keysym_from_name(tok, XKB_KEYSYM_CASE_INSENSITIVE);
}

static int
parse_mod(const char *s, uint32_t *mods)
{
    if (!strcasecmp(s, "super") || !strcasecmp(s, "mod") || !strcasecmp(s, "logo"))
        *mods |= KMOD_LOGO;
    else if (!strcasecmp(s, "shift"))
        *mods |= KMOD_SHIFT;
    else if (!strcasecmp(s, "ctrl") || !strcasecmp(s, "control"))
        *mods |= KMOD_CTRL;
    else if (!strcasecmp(s, "alt") || !strcasecmp(s, "mod1"))
        *mods |= KMOD_ALT;
    else
        return -1;
    return 0;
}

/*
 * Parse one `Mod+Mod+Key` step token into *step. `force_gamepad` marks the step
 * TRIG_GAMEPAD regardless of contents (for the `gamepad` directive).
 */
static int
parse_step_token(char *tok, int force_gamepad, TriggerStep *step,
                 char *errbuf, size_t errlen)
{
    char *lastplus, *key, *save, *m;

    memset(step, 0, sizeof(*step));

    /* Split trailing key from leading mods on the last '+'. A token that ends
     * with '+' means the key itself is '+'. */
    lastplus = strrchr(tok, '+');
    if (!lastplus) {
        key = tok;
    } else if (lastplus[1] == '\0') {
        key = (char *)"+";
        *lastplus = '\0';
    } else {
        key = lastplus + 1;
        *lastplus = '\0';
    }

    if (lastplus) {
        save = NULL;
        for (m = strtok_r(tok, "+", &save); m; m = strtok_r(NULL, "+", &save)) {
            if (parse_mod(m, &step->mods) != 0) {
                snprintf(errbuf, errlen, "unknown modifier '%s'", m);
                return -1;
            }
        }
    }

    if (force_gamepad) {
        step->kind = TRIG_GAMEPAD;
        return 0;
    }

    if (!strncmp(key, "BTN_", 4) || !strncasecmp(key, "Mouse", 5)) {
        step->kind = TRIG_BUTTON;
        if (!strcasecmp(key, "BTN_LEFT") || !strcasecmp(key, "Mouse1"))
            step->code = KBTN_LEFT;
        else if (!strcasecmp(key, "BTN_RIGHT") || !strcasecmp(key, "Mouse2"))
            step->code = KBTN_RIGHT;
        else if (!strcasecmp(key, "BTN_MIDDLE") || !strcasecmp(key, "Mouse3"))
            step->code = KBTN_MIDDLE;
        else {
            snprintf(errbuf, errlen, "unknown button '%s'", key);
            return -1;
        }
        return 0;
    }
    if (!strcasecmp(key, "ScrollUp")) { step->kind = TRIG_SCROLL; step->code = SCROLL_UP; return 0; }
    if (!strcasecmp(key, "ScrollDown")) { step->kind = TRIG_SCROLL; step->code = SCROLL_DOWN; return 0; }
    if (!strcasecmp(key, "ScrollLeft")) { step->kind = TRIG_SCROLL; step->code = SCROLL_LEFT; return 0; }
    if (!strcasecmp(key, "ScrollRight")) { step->kind = TRIG_SCROLL; step->code = SCROLL_RIGHT; return 0; }
    if (!strncmp(key, "swipe.", 6) || !strncmp(key, "pinch.", 6)) {
        step->kind = TRIG_GESTURE;
        return 0;
    }

    /* A bare modifier as the "key" (e.g. `tap Super`) is a modifier-only
     * trigger: no keysym, used by tap/hold binds (dispatched in a later stage). */
    if (parse_mod(key, &step->mods) == 0) {
        step->kind = TRIG_KEY;
        step->keysym = XKB_KEY_NoSymbol;
        return 0;
    }

    step->kind = TRIG_KEY;
    step->keysym = literal_keysym(key);
    if (step->keysym == XKB_KEY_NoSymbol) {
        snprintf(errbuf, errlen, "unknown key '%s'", key);
        return -1;
    }
    return 0;
}

/* ---- line parsing ---- */

/* Split a mutable line into whitespace tokens. Returns token count. */
static int
tokenize(char *line, char **tokens, int max)
{
    int n = 0;
    char *save = NULL;
    char *t;
    for (t = strtok_r(line, " \t\r", &save); t && n < max;
         t = strtok_r(NULL, " \t\r", &save))
        tokens[n++] = t;
    return n;
}

/* Parse a `bind`/`gamepad` directive whose tokens are tokens[start..ntok). */
static int
parse_bind_line(BindEngine *e, int mode_idx, int is_gamepad,
                char **tokens, int start, int ntok, int lineno,
                char *errbuf, size_t errlen)
{
    TriggerEdge edge = EDGE_PRESS;
    int i = start;
    int arrow = -1;
    int nsteps, s, action_id;
    int hold_ms = 0;
    TriggerStep *steps;
    Binding *b;
    char sb[128];
    char ab[128];
    int j;

    if (!is_gamepad && i < ntok
            && (!strcmp(tokens[i], "tap") || !strcmp(tokens[i], "hold"))) {
        edge = (tokens[i][0] == 't') ? EDGE_TAP : EDGE_HOLD;
        i++;
        /* Optional dwell in ms right after `hold`, e.g. `hold 1500 Super`. */
        if (edge == EDGE_HOLD && i < ntok && tokens[i][0] >= '0' && tokens[i][0] <= '9') {
            hold_ms = atoi(tokens[i]);
            i++;
        }
    }

    /* Collect trigger step tokens up to "->". */
    for (j = i; j < ntok; j++)
        if (strcmp(tokens[j], "->") == 0) { arrow = j; break; }
    if (arrow < 0) {
        snprintf(errbuf, errlen, "line %d: missing '->'", lineno);
        return -1;
    }
    nsteps = arrow - i;
    if (nsteps < 1) {
        snprintf(errbuf, errlen, "line %d: missing trigger", lineno);
        return -1;
    }
    if (arrow + 1 >= ntok) {
        snprintf(errbuf, errlen, "line %d: missing action", lineno);
        return -1;
    }

    steps = calloc((size_t)nsteps, sizeof(TriggerStep));
    if (!steps)
        return -1;
    for (s = 0; s < nsteps; s++) {
        if (parse_step_token(tokens[i + s], is_gamepad, &steps[s], sb, sizeof(sb)) != 0) {
            snprintf(errbuf, errlen, "line %d: %s", lineno, sb);
            free(steps);
            return -1;
        }
        steps[s].edge = edge;
    }

    action_id = bind_action_lookup(tokens[arrow + 1]);
    if (action_id < 0) {
        snprintf(errbuf, errlen, "line %d: unknown action '%s'", lineno, tokens[arrow + 1]);
        free(steps);
        return -1;
    }

    b = mode_add_binding(&e->modes[mode_idx]);
    if (!b) {
        free(steps);
        return -1;
    }
    ab[0] = '\0';
    /* Invariant this relies on: bind_action_parse_arg() must not allocate
     * (set b->action_arg.owned) until it's sure it will succeed — this
     * failure path returns without freeing b->action_arg or incrementing
     * modes[mode_idx].count, so an action type that allocated earlier and
     * then failed on a later argument would leak silently (mode_add_binding()
     * already grew the array; not incrementing count just makes this slot
     * unreachable to any cleanup pass that iterates 0..count). True for
     * every action type today — keep it true for any new one. */
    if (bind_action_parse_arg(action_id, ntok - (arrow + 2), &tokens[arrow + 2],
                              b, ab, sizeof(ab)) != 0) {
        snprintf(errbuf, errlen, "line %d: %s", lineno, ab);
        free(steps);
        return -1;
    }

    b->action_id = action_id;
    b->hold_ms = hold_ms;
    b->trig.steps = steps;
    b->trig.nsteps = nsteps;
    /* Stage 1 dispatches single-step, press-edge key/button/scroll triggers.
     * A key step with no keysym (a bare modifier, e.g. `tap Super`) needs
     * tap/hold handling and stays inactive, as do sequences and gestures. */
    b->trig.active = 0;
    if (nsteps == 1 && edge == EDGE_PRESS) {
        if (steps[0].kind == TRIG_BUTTON || steps[0].kind == TRIG_SCROLL)
            b->trig.active = 1;
        else if (steps[0].kind == TRIG_KEY && steps[0].keysym != XKB_KEY_NoSymbol)
            b->trig.active = 1;
    }
    e->modes[mode_idx].count++;
    return 0;
}

BindEngine *
bind_parse(const char *text, char *errbuf, size_t errlen)
{
    BindEngine *e;
    char *buf;
    int mode_idx = 0;   /* current mode; 0 = default */
    int in_mode = 0;
    int lineno = 0;
    char *save = NULL;
    char *line;
    char *hash;
    char *tokens[MAX_TOKENS];
    int ntok;

    e = engine_new();
    if (!e) {
        snprintf(errbuf, errlen, "out of memory");
        return NULL;
    }
    buf = strdup(text);
    if (!buf) {
        bind_engine_free(e);
        snprintf(errbuf, errlen, "out of memory");
        return NULL;
    }

    for (line = strtok_r(buf, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        lineno++;

        hash = strchr(line, '#');
        if (hash)
            *hash = '\0';

        ntok = tokenize(line, tokens, MAX_TOKENS);
        if (ntok == 0)
            continue;

        if (strcmp(tokens[0], "}") == 0) {
            if (!in_mode) {
                snprintf(errbuf, errlen, "line %d: unexpected '}'", lineno);
                goto fail;
            }
            in_mode = 0;
            mode_idx = 0;
            continue;
        }

        if (strcmp(tokens[0], "mode") == 0) {
            if (in_mode) {
                snprintf(errbuf, errlen, "line %d: nested modes not allowed", lineno);
                goto fail;
            }
            if (ntok != 3 || strcmp(tokens[2], "{") != 0) {
                snprintf(errbuf, errlen, "line %d: expected 'mode <name> {'", lineno);
                goto fail;
            }
            mode_idx = mode_find_or_add(e, tokens[1]);
            if (mode_idx < 0) {
                snprintf(errbuf, errlen, "line %d: out of memory", lineno);
                goto fail;
            }
            in_mode = 1;
            continue;
        }

        if (strcmp(tokens[0], "bind") == 0) {
            if (parse_bind_line(e, mode_idx, 0, tokens, 1, ntok, lineno, errbuf, errlen) != 0)
                goto fail;
            continue;
        }
        if (strcmp(tokens[0], "gamepad") == 0) {
            if (parse_bind_line(e, mode_idx, 1, tokens, 1, ntok, lineno, errbuf, errlen) != 0)
                goto fail;
            continue;
        }
        if (strcmp(tokens[0], "unbind") == 0) {
            int action_id;
            if (ntok != 2) {
                snprintf(errbuf, errlen, "line %d: expected 'unbind <action-name>'", lineno);
                goto fail;
            }
            action_id = bind_action_lookup(tokens[1]);
            if (action_id < 0) {
                snprintf(errbuf, errlen, "line %d: unbind: unknown action '%s'", lineno, tokens[1]);
                goto fail;
            }
            e->unbound[action_id] = 1;
            continue;
        }

        snprintf(errbuf, errlen, "line %d: unknown directive '%s'", lineno, tokens[0]);
        goto fail;
    }

    if (in_mode) {
        snprintf(errbuf, errlen, "unclosed mode block");
        goto fail;
    }

    free(buf);
    return e;

fail:
    free(buf);
    bind_engine_free(e);
    return NULL;
}

/* Every action must be either bound somewhere (any mode) or explicitly
 * "unbind"-declared — see this file's top comment for why. Separate from
 * bind_parse() itself (which stays purely about DSL grammar, no opinion on
 * whether a *particular* parsed result is a complete runtime config) so
 * targeted parser unit tests can keep constructing small, partial fixtures
 * without also having to enumerate every action via "unbind" just to
 * satisfy a check that isn't what they're testing. The real runtime loader
 * (bind_engine.c's binds_load()) calls this after a successful bind_parse()
 * — that's the actual "is this a valid config to run with" gate.
 *
 * Collects *every* uncovered action into one message rather than failing on
 * the first, so fixing a stale config is one edit instead of a
 * fail/fix/reload loop per missing action. Returns 0 if fully covered, -1
 * with errbuf set otherwise. */
int
bind_check_coverage(const BindEngine *e, char *errbuf, size_t errlen)
{
    unsigned char covered[ACT_COUNT] = {0};
    int m, b, a;
    size_t off = 0;
    int nmissing = 0;

    for (m = 0; m < e->nmodes; m++)
        for (b = 0; b < e->modes[m].count; b++)
            covered[e->modes[m].binds[b].action_id] = 1;

    for (a = 0; a < ACT_COUNT; a++) {
        if (covered[a] || e->unbound[a])
            continue;
        off += (size_t)snprintf(errbuf + off, off < errlen ? errlen - off : 0,
                "%s%s", nmissing ? ", " : "action(s) with no bind or unbind: ",
                bind_action_name(a));
        nmissing++;
    }
    return nmissing > 0 ? -1 : 0;
}
