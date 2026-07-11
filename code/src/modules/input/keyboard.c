/* Keyboard event dispatch: key press/release, modifier notify, and repeat.
 *
 * The wlroots keyboard-group lifecycle (create/destroy) and keybinding()
 * (which just resolves through the bind DSL) stay in dwl.c: they're coupled
 * to the static action functions (spawn, focusstack, zoom, ...) that only
 * dwl.c defines. This TU owns the per-event logic instead: gesture feeding,
 * crop-mode intercept, and repeat scheduling, calling keybinding() as an
 * extern dispatch.
 *
 * Separately-compiled TU: pulls the shared data model, globals, and
 * prototypes from kalin.h (without DWL_INTERNAL, so it sees the extern
 * interface that dwl.c backs) plus the bind engine's own header. */
#include "kalin.h"
#include "binds.h"

void
keypress(struct wl_listener *listener, void *data)
{
	int i;
	/* This event is raised when a key is pressed or released. */
	KeyboardGroup *group = wl_container_of(listener, group, key);
	struct wlr_keyboard_key_event *event = data;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms;
	if (!group->wlr_group->keyboard.xkb_state)
		return;
	nsyms = xkb_state_key_get_syms(
			group->wlr_group->keyboard.xkb_state, keycode, &syms);

	int handled = 0;
	uint32_t mods = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);
	int is_super_key = 0;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	for (i = 0; i < nsyms; i++) {
		if (syms[i] == XKB_KEY_Super_L || syms[i] == XKB_KEY_Super_R) {
			is_super_key = 1;
			break;
		}
	}

	/* Feed the bind engine so it can time `hold` (while-held) gestures. A key
	 * counts as a modifier only if every produced keysym is a modifier. Compute
	 * an "effective" mod mask that reflects the post-event state: on a modifier
	 * key event, wlr_keyboard_get_modifiers() may not yet include/exclude the
	 * just-(un)pressed bit, so fold it in explicitly. */
	{
		int is_modifier_key = nsyms > 0;
		int pressed = event->state == WL_KEYBOARD_KEY_STATE_PRESSED;
		uint32_t mod_bits = 0, eff_mods;
		for (i = 0; i < nsyms; i++) {
			switch (syms[i]) {
			case XKB_KEY_Super_L: case XKB_KEY_Super_R:
				mod_bits |= WLR_MODIFIER_LOGO; break;
			case XKB_KEY_Control_L: case XKB_KEY_Control_R:
				mod_bits |= WLR_MODIFIER_CTRL; break;
			case XKB_KEY_Shift_L: case XKB_KEY_Shift_R:
				mod_bits |= WLR_MODIFIER_SHIFT; break;
			case XKB_KEY_Alt_L: case XKB_KEY_Alt_R:
			case XKB_KEY_Meta_L: case XKB_KEY_Meta_R:
				mod_bits |= WLR_MODIFIER_ALT; break;
			case XKB_KEY_Hyper_L: case XKB_KEY_Hyper_R:
			case XKB_KEY_ISO_Level3_Shift:
				break;
			default:
				is_modifier_key = 0;
				break;
			}
		}
		eff_mods = pressed ? (mods | mod_bits) : (mods & ~mod_bits);
		/* The engine times modifier tap/hold gestures (e.g. tap Super -> launcher,
		 * hold Super -> window menu). A locked session suppresses them. */
		if (!locked)
			bind_gesture_key(eff_mods, is_modifier_key, pressed, event->time_msec);
	}

	/* Surface Super-held to the shell so it can raise the hold-Super
	 * window-actions overlay. */
	if (is_super_key) {
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			if (!super_held) {
				super_held = 1;
				ipc_broadcast_state();
			}
		} else if (super_held) {
			super_held = 0;
			/* A pending menu-armed connection (Super+L) only makes sense
			 * while Super is still down — releasing it without clicking a
			 * target cancels rather than leaving it silently armed for a
			 * later, unrelated click. */
			connect_pick_cancel();
			ipc_broadcast_state();
		}
	}

	/* While crop mode is active, an unmodified 'r' resets the target window to
	 * its uncropped size and leaves crop mode. Handled here (not via a normal
	 * bind) so a bare 'r' is only captured during crop mode and types
	 * normally otherwise. */
	if (!locked && event->state == WL_KEYBOARD_KEY_STATE_PRESSED
			&& crop_editor.active && CLEANMASK(mods) == 0) {
		for (i = 0; i < nsyms; i++) {
			if (xkb_keysym_to_lower(syms[i]) == XKB_KEY_r) {
				cropreset(NULL);
				handled = 1;
				break;
			}
		}
	}

	/* Same pattern: while the overview is open, a bare Escape closes it
	 * (restoring the camera) instead of reaching whatever client is focused;
	 * otherwise Escape types normally. */
	if (!handled && !locked && event->state == WL_KEYBOARD_KEY_STATE_PRESSED
			&& overview_is_active() && CLEANMASK(mods) == 0) {
		for (i = 0; i < nsyms; i++) {
			if (syms[i] == XKB_KEY_Escape) {
				overview_exit();
				handled = 1;
				break;
			}
		}
	}

	/* Screenshot UI intercepts its own key scheme (matching niri exactly)
	 * ahead of the normal bind dispatch, the same way crop-mode and overview
	 * grab their bare keys above: Escape cancels; Space/Enter confirms with
	 * a disk write; Ctrl+C confirms clipboard-only; P toggles pointer
	 * visibility. Anything else passes through unhandled (falls through to
	 * normal bind dispatch, e.g. arrow keys still do nothing special here). */
	if (!handled && !locked && event->state == WL_KEYBOARD_KEY_STATE_PRESSED
			&& screenshot_ui.active) {
		uint32_t clean = CLEANMASK(mods);
		for (i = 0; i < nsyms; i++) {
			if (syms[i] == XKB_KEY_Escape) {
				screenshotui_cancel(NULL);
				handled = 1;
				break;
			}
			if (clean == 0 && (syms[i] == XKB_KEY_space || syms[i] == XKB_KEY_Return)) {
				screenshotui_confirm(true);
				handled = 1;
				break;
			}
			if (clean == WLR_MODIFIER_CTRL
					&& xkb_keysym_to_lower(syms[i]) == XKB_KEY_c) {
				screenshotui_confirm(false);
				handled = 1;
				break;
			}
			if (clean == 0 && xkb_keysym_to_lower(syms[i]) == XKB_KEY_p) {
				screenshotui_toggle_pointer();
				handled = 1;
				break;
			}
		}
	}

	/* On _press_ if there is no active screen locker,
	 * attempt to process a compositor keybinding. */
	if (!handled && !locked && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (i = 0; i < nsyms; i++)
			handled = keybinding(mods, syms[i]) || handled;
	}

	/* A non-repeatable action (any one-shot toggle) must never arm the repeat
	 * timer at all — re-firing it on every repeat tick while the key happens
	 * to be held a little too long flips it back and forth, and releasing on
	 * the wrong parity leaves it in the wrong state. See keybinding_repeatable(). */
	if (handled && keybinding_repeatable() && group->wlr_group->keyboard.repeat_info.delay > 0) {
		xkb_keysym_t *repeat_syms = NULL;
		if (nsyms > 0) {
			repeat_syms = ecalloc((size_t)nsyms, sizeof(*repeat_syms));
			memcpy(repeat_syms, syms, (size_t)nsyms * sizeof(*repeat_syms));
		}
		free(group->keysyms);
		group->keysyms = repeat_syms;
		group->mods = mods;
		group->nsyms = nsyms;
		wl_event_source_timer_update(group->key_repeat_source,
				group->wlr_group->keyboard.repeat_info.delay);
	} else {
		free(group->keysyms);
		group->keysyms = NULL;
		group->nsyms = 0;
		wl_event_source_timer_update(group->key_repeat_source, 0);
	}

	if (handled)
		return;

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Pass unhandled keycodes along to the client. */
	wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
}

void
keypressmod(struct wl_listener *listener, void *data)
{
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	KeyboardGroup *group = wl_container_of(listener, group, modifiers);

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(seat,
			&group->wlr_group->keyboard.modifiers);
}

int
keyrepeat(void *data)
{
	KeyboardGroup *group = data;
	int i;
	if (!group->nsyms || !group->keysyms || group->wlr_group->keyboard.repeat_info.rate <= 0)
		return 0;

	wl_event_source_timer_update(group->key_repeat_source,
			1000 / group->wlr_group->keyboard.repeat_info.rate);

	for (i = 0; i < group->nsyms; i++)
		keybinding(group->mods, group->keysyms[i]);

	return 0;
}
