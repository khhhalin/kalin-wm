/*
 * bind_actions.c - the action registry for the bind DSL.
 *
 * Maps DSL action names to ActionId and knows how to turn the trailing DSL
 * tokens into a compositor Arg. Deliberately free of wlroots and of the real
 * action functions: it produces plain ints/floats/blobs, and dwl.c's
 * bind_invoke() translates any semantic ints (directions, layout indices) into
 * the concrete wlroots enums/pointers. This keeps the parser unit-testable.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binds.h"

static const struct { const char *name; int id; } action_names[] = {
    { "spawn",                ACT_SPAWN },
    { "toggle-launcher",      ACT_TOGGLE_LAUNCHER },
    { "close",                ACT_CLOSE },
    { "resize",               ACT_RESIZE },
    { "viewport.zoom",        ACT_VIEWPORT_ZOOM },
    { "viewport.pan",         ACT_VIEWPORT_PAN },
    { "viewport.fit",         ACT_VIEWPORT_FIT },
    { "viewport.reset",       ACT_VIEWPORT_RESET },
    { "viewport.follow",      ACT_VIEWPORT_FOLLOW },
    { "viewport.follow-new",  ACT_VIEWPORT_FOLLOW_NEW },
    { "move-column",          ACT_MOVE_COLUMN },
    { "focus",                ACT_FOCUS_DIR },
    { "move-window",          ACT_MOVE_WINDOW },
    { "focus-stack",          ACT_FOCUS_STACK },
    { "focus-monitor",        ACT_FOCUS_MONITOR },
    { "move-monitor",         ACT_MOVE_MONITOR },
    { "toggle-floating",      ACT_TOGGLE_FLOATING },
    { "toggle-fullscreen",    ACT_TOGGLE_FULLSCREEN },
    { "master-zoom",          ACT_MASTER_ZOOM },
    { "layout",               ACT_LAYOUT },
    { "opacity",              ACT_OPACITY },
    { "crop",                 ACT_CROP },
    { "crop-cancel",          ACT_CROP_CANCEL },
    { "screenshot",           ACT_SCREENSHOT },
    { "pointer-move",         ACT_POINTER_MOVE },
    { "pointer-resize",       ACT_POINTER_RESIZE },
    { "chvt",                 ACT_CHVT },
    { "quit",                 ACT_QUIT },
    { "window-menu",          ACT_WINDOW_MENU },
    { "mode",                 ACT_MODE },
};

int
bind_action_lookup(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof(action_names) / sizeof(action_names[0]); i++)
        if (strcmp(action_names[i].name, name) == 0)
            return action_names[i].id;
    return -1;
}

static int
parse_signed(const char *s, int *out)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0')
        return -1;
    *out = (int)v;
    return 0;
}

static int
parse_float_tok(const char *s, float *out)
{
    char *end;
    float v = strtof(s, &end);
    if (end == s || *end != '\0')
        return -1;
    *out = v;
    return 0;
}

/* Accept a signed number or a left/right word, yielding -1 or +1. */
static int
parse_step_dir(const char *s, int *out)
{
    if (strcmp(s, "left") == 0 || strcmp(s, "prev") == 0) { *out = -1; return 0; }
    if (strcmp(s, "right") == 0 || strcmp(s, "next") == 0) { *out = +1; return 0; }
    return parse_signed(s, out);
}

static void *
xmemdup(const void *p, size_t n)
{
    void *d = malloc(n);
    if (d)
        memcpy(d, p, n);
    return d;
}

static int
parse_spawn(int argc, char **argv, Binding *out, char *errbuf, size_t errlen)
{
    char **strv;
    int i, j;

    if (argc < 1) {
        snprintf(errbuf, errlen, "spawn needs a command");
        return -1;
    }
    strv = calloc((size_t)argc + 1, sizeof(char *));
    if (!strv)
        return -1;
    for (i = 0; i < argc; i++) {
        strv[i] = strdup(argv[i]);
        if (!strv[i]) {
            for (j = 0; j < i; j++)
                free(strv[j]);
            free(strv);
            return -1;
        }
    }
    out->arg.v = strv;
    out->arg_kind = ARGK_STRV;
    out->owned = strv;
    return 0;
}

static int
parse_resize(int argc, char **argv, Binding *out, char *errbuf, size_t errlen)
{
    int delta;
    int pair[2];
    int *blob;

    if (argc != 2 || parse_signed(argv[1], &delta) != 0) {
        snprintf(errbuf, errlen, "resize needs 'width|height <delta>'");
        return -1;
    }
    if (strcmp(argv[0], "width") == 0) { pair[0] = delta; pair[1] = 0; }
    else if (strcmp(argv[0], "height") == 0) { pair[0] = 0; pair[1] = delta; }
    else { snprintf(errbuf, errlen, "resize dim must be width|height"); return -1; }
    blob = xmemdup(pair, sizeof(pair));
    if (!blob)
        return -1;
    out->arg.v = blob;
    out->arg_kind = ARGK_BLOB;
    out->owned = blob;
    return 0;
}

static int
parse_panvec(int argc, char **argv, Binding *out, char *errbuf, size_t errlen)
{
    float pair[2];
    float *blob;

    if (argc != 2 || parse_float_tok(argv[0], &pair[0]) != 0
            || parse_float_tok(argv[1], &pair[1]) != 0) {
        snprintf(errbuf, errlen, "viewport.pan needs '<dx> <dy>'");
        return -1;
    }
    blob = xmemdup(pair, sizeof(pair));
    if (!blob)
        return -1;
    out->arg.v = blob;
    out->arg_kind = ARGK_BLOB;
    out->owned = blob;
    return 0;
}

int
bind_action_parse_arg(int action_id, int argc, char **argv,
                      Binding *out, char *errbuf, size_t errlen)
{
    int v;
    float f;

    out->arg.v = NULL;
    out->arg_kind = ARGK_NONE;
    out->owned = NULL;

    switch (action_id) {
    case ACT_SPAWN:
    case ACT_TOGGLE_LAUNCHER:
        return parse_spawn(argc, argv, out, errbuf, errlen);
    case ACT_RESIZE:
        return parse_resize(argc, argv, out, errbuf, errlen);
    case ACT_VIEWPORT_PAN:
        return parse_panvec(argc, argv, out, errbuf, errlen);
    case ACT_VIEWPORT_ZOOM:
    case ACT_OPACITY:
        if (argc != 1 || parse_float_tok(argv[0], &f) != 0) {
            snprintf(errbuf, errlen, "action needs a numeric argument");
            return -1;
        }
        out->arg.f = f;
        return 0;
    case ACT_MOVE_COLUMN:
        if (argc != 1 || parse_step_dir(argv[0], &v) != 0) {
            snprintf(errbuf, errlen, "move-column needs left|right|<n>");
            return -1;
        }
        out->arg.i = v;
        return 0;
    case ACT_FOCUS_STACK:
        if (argc != 1 || parse_step_dir(argv[0], &v) != 0) {
            snprintf(errbuf, errlen, "focus-stack needs next|prev|<n>");
            return -1;
        }
        out->arg.i = v;
        return 0;
    case ACT_FOCUS_DIR:
    case ACT_MOVE_WINDOW:
        v = -1;
        if (argc == 1) {
            if (strcmp(argv[0], "left") == 0) v = 0;
            else if (strcmp(argv[0], "right") == 0) v = 1;
            else if (strcmp(argv[0], "up") == 0) v = 2;
            else if (strcmp(argv[0], "down") == 0) v = 3;
        }
        if (v < 0) {
            snprintf(errbuf, errlen, "direction must be left|right|up|down");
            return -1;
        }
        out->arg.i = v;
        return 0;
    case ACT_FOCUS_MONITOR:
    case ACT_MOVE_MONITOR:
        v = -1;
        if (argc == 1) {
            if (strcmp(argv[0], "left") == 0) v = 0;
            else if (strcmp(argv[0], "right") == 0) v = 1;
        }
        if (v < 0) {
            snprintf(errbuf, errlen, "monitor action needs left|right");
            return -1;
        }
        out->arg.i = v;
        return 0;
    case ACT_LAYOUT:
        v = -2;
        if (argc == 1) {
            if (strcmp(argv[0], "infinite") == 0) v = 0;
            else if (strcmp(argv[0], "floating") == 0) v = 1;
            else if (strcmp(argv[0], "toggle") == 0) v = -1;
        }
        if (v == -2) {
            snprintf(errbuf, errlen, "layout needs infinite|floating|toggle");
            return -1;
        }
        out->arg.i = v;
        return 0;
    case ACT_CHVT:
        if (argc != 1 || parse_signed(argv[0], &v) != 0 || v < 1) {
            snprintf(errbuf, errlen, "chvt needs a positive VT number");
            return -1;
        }
        out->arg.ui = (uint32_t)v;
        return 0;
    case ACT_MODE: {
        char *name;
        if (argc != 1) {
            snprintf(errbuf, errlen, "mode needs a target mode name");
            return -1;
        }
        name = strdup(argv[0]);
        if (!name)
            return -1;
        out->arg.v = name;
        out->arg_kind = ARGK_BLOB;
        out->owned = name;
        return 0;
    }
    default:
        /* No-argument actions. */
        if (argc != 0) {
            snprintf(errbuf, errlen, "action takes no arguments");
            return -1;
        }
        return 0;
    }
}
