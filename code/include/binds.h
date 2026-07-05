/*
 * binds.h - runtime input binding engine (Stage 1: keyboard/pointer/scroll +
 * modes, hot-reloaded from a custom DSL). Deliberately self-contained: this
 * header pulls in only <stdint.h> and <xkbcommon/xkbcommon.h> so the parser and
 * action registry can be unit-tested without wlroots. The compositor-facing
 * dispatch (bind_invoke) lives in dwl.c where the action functions are visible.
 */
#ifndef KALIN_BINDS_H
#define KALIN_BINDS_H

#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

/* Arg is also defined in kalin.h; guard so both may be included together. */
#ifndef KALIN_ARG_DEFINED
#define KALIN_ARG_DEFINED
typedef union {
    int i;
    uint32_t ui;
    float f;
    const void *v;
} Arg;
#endif

/*
 * Modifier bits. These MUST equal the wlroots WLR_MODIFIER_* values (they are
 * the standard wl_keyboard/xkb bit positions); bind_engine.c static-asserts the
 * equality against wlroots. Defined here so the pure parser needs no wlroots.
 */
#define KMOD_SHIFT (1u << 0)
#define KMOD_CAPS  (1u << 1)
#define KMOD_CTRL  (1u << 2)
#define KMOD_ALT   (1u << 3)
#define KMOD_LOGO  (1u << 6)

/* Pointer button codes (linux/input-event-codes.h values). */
#define KBTN_LEFT   0x110
#define KBTN_RIGHT  0x111
#define KBTN_MIDDLE 0x112

/* Scroll directions (TRIG_SCROLL code). */
enum { SCROLL_UP, SCROLL_DOWN, SCROLL_LEFT, SCROLL_RIGHT };

/*
 * Action identifiers. The parser resolves a DSL action name to one of these via
 * the registry in bind_actions.c; the compositor's bind_invoke() (dwl.c)
 * switches on it to call the real function. Keep in sync with action_defs[].
 */
typedef enum {
    ACT_SPAWN,             /* strv */
    ACT_TOGGLE_LAUNCHER,   /* strv: spawn the launcher, or kill it if already up */
    ACT_CLOSE,             /* none */
    ACT_RESIZE,            /* blob int[2] {dx,dy} */
    ACT_VIEWPORT_ZOOM,     /* f */
    ACT_VIEWPORT_PAN,      /* blob float[2] {dx,dy} */
    ACT_VIEWPORT_FIT,      /* none */
    ACT_VIEWPORT_RESET,    /* none */
    ACT_VIEWPORT_FOLLOW,   /* none */
    ACT_VIEWPORT_FOLLOW_NEW,/* none */
    ACT_MOVE_COLUMN,       /* i: -1/+1 */
    ACT_FOCUS_DIR,         /* i: DIR_* 0..3 */
    ACT_MOVE_WINDOW,       /* i: DIR_* 0..3 (carry window through the grid) */
    ACT_FOCUS_STACK,       /* i: -1/+1 */
    ACT_FOCUS_MONITOR,     /* i: 0=left 1=right */
    ACT_MOVE_MONITOR,      /* i: 0=left 1=right */
    ACT_TOGGLE_FLOATING,   /* none */
    ACT_TOGGLE_FULLSCREEN, /* none */
    ACT_MASTER_ZOOM,       /* none (dwl master swap) */
    ACT_LAYOUT,            /* i: 0=infinite 1=floating -1=toggle */
    ACT_OPACITY,           /* f */
    ACT_CROP,              /* none */
    ACT_CROP_CANCEL,       /* none */
    ACT_SCREENSHOT,        /* none */
    ACT_POINTER_MOVE,      /* none (drag-move, buttons) */
    ACT_POINTER_RESIZE,    /* none (drag-resize, buttons) */
    ACT_CHVT,              /* ui: vt number */
    ACT_QUIT,              /* none */
    ACT_WINDOW_MENU,       /* none; while-held: shown on hold, hidden on release */
    ACT_MODE,              /* blob char* target mode name (engine-internal) */
    ACT_COUNT
} ActionId;

/* How a binding's Arg payload was allocated (for cleanup). */
typedef enum { ARGK_NONE, ARGK_BLOB, ARGK_STRV } ArgKind;

typedef enum {
    TRIG_KEY,
    TRIG_BUTTON,
    TRIG_SCROLL,
    TRIG_GESTURE,   /* parsed, dispatched in a later stage */
    TRIG_GAMEPAD    /* parsed, dispatched in a later stage */
} TriggerKind;

typedef enum { EDGE_PRESS, EDGE_TAP, EDGE_HOLD } TriggerEdge;

typedef struct {
    TriggerKind kind;
    TriggerEdge edge;
    uint32_t mods;         /* KMOD_* bitmask */
    xkb_keysym_t keysym;   /* TRIG_KEY */
    uint32_t code;         /* TRIG_BUTTON: KBTN_*; TRIG_SCROLL: SCROLL_*; other kinds: opaque */
} TriggerStep;

typedef struct {
    TriggerStep *steps;    /* nsteps > 1 => leader sequence */
    int nsteps;
    int active;            /* 0 for trigger kinds not yet dispatched (gesture/gamepad/seq) */
} Trigger;

typedef struct {
    Trigger trig;
    int action_id;
    Arg arg;
    ArgKind arg_kind;
    void *owned;           /* payload to free (ARGK_BLOB: one block; ARGK_STRV: char** ) */
    int hold_ms;           /* EDGE_HOLD dwell in ms; 0 = engine default */
} Binding;

typedef struct {
    char *name;
    Binding *binds;
    int count, cap;
} BindMode;

typedef struct {
    BindMode *modes;
    int nmodes;
    int active_mode;       /* index into modes; 0 == "default" */
} BindEngine;

/* --- Registry (bind_actions.c) --- */
/* Returns action id or -1 if name unknown. */
int bind_action_lookup(const char *name);
/*
 * Parse the remaining tokens (argv[0..argc-1]) into out->arg / out->arg_kind /
 * out->owned for the given action. Returns 0 on success, -1 on error (with an
 * error message written to errbuf).
 */
int bind_action_parse_arg(int action_id, int argc, char **argv,
                          Binding *out, char *errbuf, size_t errlen);

/* --- Parser (bind_parser.c) --- */
/*
 * Parse DSL text into a freshly-allocated BindEngine. On error returns NULL and
 * writes a message (with line number) into errbuf. Caller owns the result and
 * frees it with bind_engine_free().
 */
BindEngine *bind_parse(const char *text, char *errbuf, size_t errlen);
void bind_engine_free(BindEngine *e);

/* --- Engine (bind_engine.c) --- */
void binds_init(void);                 /* bootstrap default file + load + watch */
int  binds_load(const char *path);     /* parse file, swap on success; 0 ok, -1 keep old */
int  binds_active(void);               /* nonzero once an engine is loaded */
int  bind_dispatch_key(uint32_t mods, xkb_keysym_t sym);
int  bind_dispatch_button(uint32_t mods, uint32_t button);
int  bind_dispatch_scroll(uint32_t mods, uint32_t dir);
/* Feed every key event so the engine can time modifier `tap`/`hold` gestures.
 * time_msec is the event timestamp (used to measure the tap duration). */
void bind_gesture_key(uint32_t mods, int is_modifier_key, int pressed,
                      uint32_t time_msec);
/* Cancel any in-progress tap/hold gesture (e.g. a pointer button was pressed
 * while a modifier was held). */
void bind_gesture_interrupt(void);

/* Implemented in dwl.c: run a resolved action (compositor side). */
void bind_invoke(int action_id, const Arg *arg);
/* Paired with bind_invoke for while-held (hold) actions, on release. */
void bind_invoke_release(int action_id, const Arg *arg);

#endif /* KALIN_BINDS_H */
