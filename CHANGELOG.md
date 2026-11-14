# Changelog

The releases of bluerig, newest first.

## Unreleased

- bluerig starts as a console text editor of its own: a continuation of
  cooledit, with the fixes and features its terminal version gained over the
  years (code folding, the undo history browser, the macro explorer, the
  editor plugin framework, ctags and etags navigation, spell checking and Lua
  scripts).
- The program is a single binary, `bluerig`.
- The packages are `bluerig` and `bluerig-lua`.
- Configuration, data and cache live in `~/.config/bluerig`,
  `~/.local/share/bluerig` and `~/.cache/bluerig`; the system files are in
  `$(sysconfdir)/bluerig` and `$(datadir)/bluerig`.
- The user menu file is `bluerig.menu` in the system configuration and
  `.bluerig.menu` in the current directory.
- The manual page is `bluerig(1)`.
- bluerig is built with meson (`meson setup build`); the build needs only GLib,
  S-Lang or ncurses, and optionally gpm, X11 and Lua.
- Releases are tagged `bluerig-X.Y.Z`.
