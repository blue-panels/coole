# Changelog

The releases of coole, newest first.

## Unreleased

- coole starts as a console text editor of its own: a continuation of
  cooledit, with the fixes and features its terminal version gained over the
  years (code folding, the undo history browser, the macro explorer, the
  editor plugin framework, ctags and etags navigation, spell checking and Lua
  scripts).
- The program is a single binary, `coole`.
- The packages are `coole` and `coole-lua`.
- Configuration, data and cache live in `~/.config/coole`,
  `~/.local/share/coole` and `~/.cache/coole`; the system files are in
  `$(sysconfdir)/coole` and `$(datadir)/coole`.
- The user menu file is `coole.menu` in the system configuration and
  `.coole.menu` in the current directory.
- The manual page is `coole(1)`.
- coole is built with meson (`meson setup build`); the build needs only GLib,
  S-Lang or ncurses, and optionally gpm, X11 and Lua.
- Releases are tagged `coole-X.Y.Z`.
