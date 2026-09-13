# Windhawk mods

Windhawk mods for window management on Windows. Each `.cpp` file is a
standalone mod injected into `explorer.exe`.

## Mods

### animated-window-monitor-switch.cpp

Moves the active window to the next monitor with a smooth animation.

- Hotkey, default **Alt+`** (configurable).
- Preserves the window's relative position and size (clamped to the
  destination monitor's work area).
- Maximized windows move to the next monitor and stay maximized.

### quake-mode-window-switch.cpp

Slides a chosen app down from off-screen into a docked strip at the top of
the screen, Quake-console style. Press the hotkey again (or focus another
window, if enabled) to slide it back up.

- Hotkey, default **`` ` ``** (configurable).
- Target process, dock height, and monitor (primary or under cursor) are
  configurable.
- Optional: hide from taskbar, hide title bar, auto-hide on focus loss.

<img width="1898" height="963" alt="quake-mode" src="quake-mode.gif" />

## Installing

1. Install [Windhawk](https://windhawk.net/).
2. Create a new mod, paste the contents of the `.cpp` file, and save.
3. Adjust settings (hotkey, target process, etc.) from the mod's settings
   panel in Windhawk.

## Development

Each mod is a single `.cpp` file compiled by Windhawk itself (MSVC, via the
Windhawk mod editor) — no separate build step or project file. Metadata,
readme text, and the settings schema live in the `==WindhawkMod==`,
`==WindhawkModReadme==`, and `==WindhawkModSettings==` comment blocks at the
top of each file.
