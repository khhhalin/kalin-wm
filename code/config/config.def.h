/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
/* appearance */
static const int sloppyfocus               = 1;  /* focus follows mouse */
static const int bypass_surface_visibility = 0;  /* 1 means idle inhibitors will disable idle tracking even if it's surface isn't visible  */
static const unsigned int borderpx         = 3;  /* border pixel of windows */
static const unsigned int focusringpx      = 2;  /* focus ring thickness (0 disables) */
static const float rootcolor[]             = COLOR(0x222222ff);
static const float bordercolor[]           = COLOR(0x444444ff);
static const float focuscolor[]            = COLOR(0x005577ff);
static const float urgentcolor[]           = COLOR(0xff0000ff);
static const int offscreen_indicator_enabled = 1;
static const int offscreen_indicator_size    = 10;
static const int offscreen_indicator_margin  = 8;
static const float offscreen_indicator_color[] = COLOR(0xffffffff);
/* Window spring-glide animation (Niri-style column sliding). The rendered world
 * position springs toward the layout target: x'' = -stiffness*(x-target) -
 * damping*x'. Critical damping is ~2*sqrt(stiffness); a lower damping gives a
 * livelier overshoot. Set anim_stiffness = 0 to disable and snap instantly. */
static const float anim_stiffness = 250.0f;
static const float anim_damping    = 26.0f;

/* Hold-Super spotlight: opacity applied to non-focused windows while the
 * radial menu is up (the focused window stays at 1.0). */
static const float spotlight_dim = 0.35f;

/* Crisp zoom: cap on the render scale (output_scale * zoom) the compositor asks
 * clients to render at when zoomed in. Higher = crisper deep zoom, more GPU. */
static const float zoom_render_max = 3.0f;

/* This conforms to the xdg-protocol. Set the alpha to zero to restore the old behavior */
static const float fullscreen_bg[]         = {0.0f, 0.0f, 0.0f, 1.0f}; /* You can also use glsl colors */

/* Overlay clock (bottom-right, HH:MM, minute updates for low CPU use) */
static const int overlay_clock_enabled      = 1;
static const int overlay_clock_margin_px    = 16;
static const int overlay_clock_padding_px   = 8;
static const int overlay_clock_digit_w      = 20;
static const int overlay_clock_digit_h      = 36;
static const int overlay_clock_segment_px   = 4;
static const int overlay_clock_digit_gap_px = 6;
static const float overlay_clock_fg[]       = COLOR(0xffffffff);
static const float overlay_clock_bg[]       = COLOR(0x000000a6);

/* exit confirmation - require double-press within this many seconds */
#define EXIT_CONFIRMATION_SECONDS 2

/* logging */
static int log_level = WLR_ERROR;

static const Rule rules[] = {
	/* app_id             title       isfloating   monitor */
	{ "vesktop",          NULL,       1,           -1 }, /* float so client can restore its preferred size */
	{ "Gimp_EXAMPLE",     NULL,       1,           -1 }, /* example: start floating */
	{ "firefox_EXAMPLE",  NULL,       0,           -1 }, /* example: tiled on the canvas */
    /* default/example rule: can be changed but cannot be eliminated; at least one rule must exist */
};

/* layout(s) - only infinite scrolling layout */
static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[∞]",      infinite }, /* infinite scrollable layout (like Niri but 2D) */
	{ "><>",      NULL },    /* floating behavior */
};

/* monitors */
/* (x=-1, y=-1) is reserved as an "autoconfigure" monitor position indicator */
static const MonitorRule monrules[] = {
   /* name        scale layout       rotate/reflect                x    y
    * example of a HiDPI laptop monitor:
    { "eDP-1",    2,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 }, */
	{ NULL,       1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 }, /* infinite layout */
	/* default monitor rule: can be changed but cannot be eliminated; at least one monitor rule must exist */
};

/* keyboard */
static const struct xkb_rule_names xkb_rules = {
	/* can specify fields: rules, model, layout, variant, options */
	/* example:
	.options = "ctrl:nocaps",
	*/
	.options = NULL,
};

static const int repeat_rate = 25;
static const int repeat_delay = 600;

/* Trackpad */
static const int tap_to_click = 1;
static const int tap_and_drag = 1;
static const int drag_lock = 1;
static const int natural_scrolling = 0;
static const int disable_while_typing = 1;
static const int left_handed = 0;
static const int middle_button_emulation = 0;
/* You can choose between:
LIBINPUT_CONFIG_SCROLL_NO_SCROLL
LIBINPUT_CONFIG_SCROLL_2FG
LIBINPUT_CONFIG_SCROLL_EDGE
LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN
*/
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;

/* You can choose between:
LIBINPUT_CONFIG_CLICK_METHOD_NONE
LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS
LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER
*/
static const enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

/* You can choose between:
LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE
*/
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

/* You can choose between:
LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
*/
static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static const double accel_speed = 0.0;

/* You can choose between:
LIBINPUT_CONFIG_TAP_MAP_LRM -- 1/2/3 finger tap maps to left/right/middle
LIBINPUT_CONFIG_TAP_MAP_LMR -- 1/2/3 finger tap maps to left/middle/right
*/
static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* Use Super (Windows) key as MODKEY */
#define MODKEY WLR_MODIFIER_LOGO


/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
/* Bare command names so the default resolves via $PATH on any distro. Copy
 * this file to config.h to override with absolute paths if desired. */
static const char *termcmd[] = { "foot", NULL };
static const char *menucmd[] = { "fuzzel", NULL };
/* Toggle the shell's exposé/overview overlay (quickshell). No-op if no shell. */
static const char *overviewcmd[] = { "qs", "ipc", "call", "windows-bar", "toggleOverview", NULL };

/* Viewport pan direction arrays - passed to viewport_pan */
static const float pan_left[]  = {-50, 0};
static const float pan_right[] = {50, 0};
static const float pan_up[]    = {0, -50};
static const float pan_down[]  = {0, 50};

/* Keyboard resize deltas: [ ] adjust width, { } adjust height */
static const int resize_narrow[] = {-40, 0};
static const int resize_wide[]   = {40, 0};
static const int resize_short[]  = {0, -40};
static const int resize_tall[]   = {0, 40};

/* Pan speed configuration */
#define PAN_SPEED 50.0f
#define PAN_SPEED_FAST 200.0f

static const Key keys[] = {
	/* Note that Shift changes certain key codes: 2 -> at, etc. */
	/* modifier                  key                  function          argument */
	{ MODKEY,                    XKB_KEY_p,           spawn,            {.v = menucmd} },
	{ MODKEY,                    XKB_KEY_Print,       capture_screenshot, {0} }, /* Super+PrtSc = 2x hi-res PNG */
	{ MODKEY,                    XKB_KEY_t,           spawn,            {.v = termcmd} }, /* Super+T = terminal */
	{ MODKEY,                    XKB_KEY_o,           spawn,            {.v = overviewcmd} }, /* Super+O = overview */
	{ MODKEY,                    XKB_KEY_j,           focusstack,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_k,           focusstack,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_Return,      zoom,             {0} },
	{ MODKEY,                    XKB_KEY_q,           killclient,       {0} }, /* Super+Q = close window */
	{ MODKEY,                    XKB_KEY_c,           cropbegin,        {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_r,           cropcancel,       {0} },
	{ MODKEY,                    XKB_KEY_i,           setlayout,        {.v = &layouts[0]} }, /* infinite layout (default) */
	{ MODKEY,                    XKB_KEY_f,           setlayout,        {.v = &layouts[1]} }, /* floating */
	{ MODKEY,                    XKB_KEY_space,       setlayout,        {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_space,       togglefloating,   {0} },
	{ MODKEY,                    XKB_KEY_e,           togglefullscreen, {0} },
	{ MODKEY,                    XKB_KEY_d,           opacityadjust,    {.f = -0.1f} }, /* dim focused window */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_D,           opacityadjust,    {.f = +0.1f} }, /* brighten focused window */
	{ MODKEY,                    XKB_KEY_comma,       focusmon,         {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,                    XKB_KEY_period,      focusmon,         {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_less,        tagmon,           {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_greater,     tagmon,           {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY,                    XKB_KEY_Escape,      quit,             {0} }, /* Super+Esc = quit wm */
	
	/* Camera pan (Super+Shift+Arrows) */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Left,        viewport_pan,     {.v = pan_left} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Right,       viewport_pan,     {.v = pan_right} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Up,          viewport_pan,     {.v = pan_up} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Down,        viewport_pan,     {.v = pan_down} },

	/* Camera pan Vim keys (Super+Shift+H/J/K/L) */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_H,           viewport_pan,     {.v = pan_left} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_L,           viewport_pan,     {.v = pan_right} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_K,           viewport_pan,     {.v = pan_up} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_J,           viewport_pan,     {.v = pan_down} },
	{ MODKEY,                    XKB_KEY_equal,       viewport_zoom,    {.f = 1.1f} },
	{ MODKEY,                    XKB_KEY_minus,       viewport_zoom,    {.f = 0.9f} },
	{ MODKEY,                    XKB_KEY_0,           viewport_fit_all, {0} }, /* Super+0 = fit all windows */
	{ MODKEY,                    XKB_KEY_BackSpace,   viewport_reset,   {0} },
	{ MODKEY,                    XKB_KEY_z,           viewport_toggle_follow, {0} }, /* toggle camera follow */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Z,           viewport_toggle_follow_new, {0} }, /* toggle auto-pan to new windows */
	
	/* Move focused window between columns (Ctrl+Left/Right, Niri-style) */
	{ WLR_MODIFIER_CTRL,         XKB_KEY_Left,        move_column,      {.i = -1} },
	{ WLR_MODIFIER_CTRL,         XKB_KEY_Right,       move_column,      {.i = +1} },
	
	/* Directional focus navigation (Super + Arrow Keys) */
	{ MODKEY,                    XKB_KEY_Left,        focus_directional, {.i = DIR_LEFT} },
	{ MODKEY,                    XKB_KEY_Right,       focus_directional, {.i = DIR_RIGHT} },
	{ MODKEY,                    XKB_KEY_Up,          focus_directional, {.i = DIR_UP} },
	{ MODKEY,                    XKB_KEY_Down,        focus_directional, {.i = DIR_DOWN} },



	/* Resize focused window (Super+[ / Super+] / Super+{ / Super+}) */
	{ MODKEY,                    XKB_KEY_bracketleft, resizefocused,    {.v = resize_narrow} },
	{ MODKEY,                    XKB_KEY_bracketright,resizefocused,    {.v = resize_wide} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_braceleft,   resizefocused,    {.v = resize_short} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_braceright,  resizefocused,    {.v = resize_tall} },

	/* Ctrl-Alt-Backspace and Ctrl-Alt-Fx used to be handled by X server */
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {0} },
	/* Ctrl-Alt-Fx is used to switch to another VT, if you don't know what a VT is
	 * do not remove them.
	 */
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

static const Button buttons[] = {
	{ MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ MODKEY, BTN_MIDDLE, togglefloating, {0} },
	{ MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
	{ MODKEY|WLR_MODIFIER_CTRL, BTN_RIGHT, moveresize, {.ui = CurResize} },
};
