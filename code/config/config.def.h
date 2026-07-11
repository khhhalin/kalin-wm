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

/* logging. __attribute__((unused)): config.h is now included by more than
 * just dwl.c (e.g. modules/layout/client_anim.c, for anim_stiffness/
 * anim_damping) — those TUs don't reference log_level, which would
 * otherwise warn. */
static int log_level __attribute__((unused)) = WLR_ERROR;

static const Rule rules[] = {
	/* app_id             title       monitor */
	{ "kalin-scratchpad", NULL,       -1 }, /* scratchpad terminal (toggle-scratchpad) */
	{ "firefox_EXAMPLE",  NULL,       -1 }, /* example: fixed-monitor rule */
    /* default/example rule: can be changed but cannot be eliminated; at least one rule must exist */
};

/* monitors */
/* (x=-1, y=-1) is reserved as an "autoconfigure" monitor position indicator */
static const MonitorRule monrules[] = {
   /* name        scale rotate/reflect                x    y
    * example of a HiDPI laptop monitor:
    { "eDP-1",    2,    WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 }, */
	{ NULL,       1,    WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
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

/* All keyboard/pointer bindings now live exclusively in the bind DSL —
 * see default_binds.h (embedded default, bootstrapped to
 * ~/.config/kalin-wm/binds.conf on first run) and binds.conf itself for the
 * live, hot-reloaded set. The compiled keys[]/buttons[] tables that used to
 * live here were removed once the DSL reached full parity with them; keeping
 * two parallel binding systems in sync was pure upkeep cost for no benefit. */
