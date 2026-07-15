# Crop mode

- Crop mode clips a window to a user-selected sub-region.
- It is entered by keybind (`Super+C`).
- The user drags to select the crop region with the mouse.
- `Escape` cancels.

- Crop mode draws its selection rectangle on an overlay layer.
- It is implemented in the `crop/crop_mode.c` runtime module.

- Crop mode was a focus of the [[stability]] audit: it had division-by-zero on zero-size windows, NULL monitor access, double-free on cancel, and use-after-free in its draw path, all since fixed.

- The plan for true pixel clipping is in [[research/active-design/crop-true-clipping-plan|the crop clipping plan]].
