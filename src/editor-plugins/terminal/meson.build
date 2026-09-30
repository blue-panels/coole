# The terminal plugin: a module the editor loads at run time from editor_plugins_dir, as any other
# editor plugin.  It calls the functions of the editor, which the program exports.

# The headers of the screen library, S-Lang or ncurses, which lib/tty/tty.h includes; not the
# library itself: the module calls it through the program, which links it.
screen_headers = []
foreach d : screen_deps
  screen_headers += d.partial_dependency(compile_args: true, includes: true)
endforeach

# the terminal of mc, as it is there: the shell on a pty, and the emulator of the terminal, which
# in mc is a part of the viewer.  The tests of the terminal link it too.  The ANSI colors it shares
# with the windows of text are in the program.
libmcterm = static_library('mcterm',
  '../../mcterm/mcterm.c',
  '../../mcterm/mcterm_cwd.c',
  '../../mcterm/mcterm_filter.c',
  '../../mcterm/mcterm_key.c',
  '../../mcterm/mcterm_proto.c',
  '../../mcterm/mcterm_select.c',
  '../../mcterm/mcterm_setup.c',
  '../../viewer/canvas.c',
  '../../viewer/terminal_buffer.c',
  '../../viewer/vterm.c',
  c_args: src_c_args,
  dependencies: [glib_dep, intl, screen_headers],
  pic: true,
)

libmcterm_dep = declare_dependency(link_with: libmcterm, dependencies: terminal_dep)

terminal_module = shared_module('terminal',
  'terminal.c',
  name_prefix: '',
  c_args: src_c_args,
  dependencies: [glib_dep, intl, terminal_dep, screen_headers],
  link_whole: libmcterm,
  install: true,
  install_dir: editor_plugins_dir,
)
