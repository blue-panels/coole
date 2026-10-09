libinternal_sources = files(
  'appearance.c',
  'args.c',
  'clipboard.c',
  'events_init.c',
  'execute.c',
  'file_history.c',
  'help.c',
  'help_md.c',
  'key_learn.c',
  'key_sniffer.c',
  'keybind_dialog.c',
  'keymap.c',
  'manage_plugins.c',
  'runtime-host.c',
  'runtime-screen.c',
  'selcodepage.c',
  'setup.c',
  'textconf.c',
  'usermenu.c',
  'util.c',

  'syntax/syntax.c',
  'syntax/tty-backend.c',


  'editor/bookmark.c',
  'editor/edit.c',
  'editor/editbuffer.c',
  'editor/editcmd.c',
  'editor/editcomplete.c',
  'editor/editdraw.c',
  'editor/editmacros.c',
  'editor/editmenu.c',
  'editor/editoptions.c',
  'editor/editsearch.c',
  'editor/editwidget.c',
  'editor/editwindow.c',
  'editor/editdock.c',
  'editor/editlayout.c',
  'editor/fold.c',
  'editor/format.c',
  'editor/syntax.c',

  # src/plugins/builtin-plugins.c is included by editor/editcmd.c
  'plugins/ctags/ctags-config.c',
  'plugins/ctags/ctags-fuzzy.c',
  'plugins/ctags/ctags-index.c',
  'plugins/ctags/ctags-parser.c',
  'plugins/ctags/ctags-plugin.c',
  'plugins/ctags/ctags-repository.c',
  'plugins/ctags/ctags-ui.c',
  'plugins/project/project.c',
  'plugins/build/build.c',
  'plugins/build/build-core.c',
  'plugins/git/git.c',
  'plugins/git/git-window.c',
  'plugins/git/git-remote.c',
  'plugins/git/git-message.c',
  'plugins/git/git-menu.c',
  'plugins/git/git-core.c',
  'plugins/project/project-core.c',
  'plugins/debugger/debugger.c',
  'plugins/debugger/gdb-mi.c',
  'plugins/etags/etags.c',
  'plugins/spell/spell.c',
)

src_c_args = [
  '-DSYSCONFDIR="@0@/"'.format(pkgsysconfdir),
  '-DLIBEXECDIR="@0@/"'.format(pkglibexecdir),
  '-DDATADIR="@0@/"'.format(pkgdatadir),
  '-DLOCALEDIR="@0@"'.format(localedir),
  '-DMC_PLUGINS_DIR="@0@"'.format(plugins_dir),
  '-DMC_RUNTIME_PLUGINS_DIR="@0@"'.format(runtime_plugins_dir),
  '-DMC_PKGDATADIR="@0@"'.format(pkgdatadir),
  '-DMC_SYSCONFDIR="@0@"'.format(sysconfdir),
  '-DMC_PKGLIBEXECDIR="@0@"'.format(pkglibexecdir),
  '-DMC_BINDIR="@0@"'.format(bindir),
]
if lua.found()
  src_c_args += [
    '-DMC_LUA_SYSTEM_SCRIPTS_DIR="@0@"'.format(lua_scripts_dir),
    '-DMC_LUA_SYSTEM_MODULES_DIR="@0@"'.format(lua_modules_dir),
  ]
endif

# Not the directory of the sources on the path of <...>: src/util.h would
# hide the <util.h> of the system, which has openpty() on macOS and NetBSD.
# The sources include what is beside them by "...", and the rest by its
# path from the top.
libinternal = static_library('internal',
  libinternal_sources,
  c_args: src_c_args,
  dependencies: [libmc_dep, intl, terminal_dep],
  implicit_include_directories: false,
)

# What the program keeps for the plugins alone: the program itself calls none of it, so it is
# linked whole, or the linker would leave it out and a module calling it would not load.
libforplugins = static_library('forplugins',
  # the ANSI colors, for the terminal and the viewer; in mc a part of the viewer
  'viewer/ansi.c',
  'viewer/ansi_color.c',
  c_args: src_c_args,
  dependencies: [libmc_dep, intl],
  implicit_include_directories: false,
)

libinternal_dep = declare_dependency(link_with: [libinternal, libforplugins],
                                     dependencies: [libmc_dep, intl, terminal_dep])

coole = executable('coole',
  'main.c',
  c_args: src_c_args,
  dependencies: libinternal_dep,
  implicit_include_directories: false,
  link_whole: libforplugins,
  # the runtime plugins call back into the program
  export_dynamic: have_gmodule,
  install: true,
)

# the help of the editor plugins, installed beside the help of the editor so
# that a dialog of a plugin can name its own file
install_data(
  'plugins/ctags/ctags.md',
  'plugins/debugger/debugger.md',
  'plugins/project/project.md',
  'plugins/build/build.md',
  'plugins/git/git.md',
  'plugins/etags/etags.md',
  'plugins/spell/spell.md',
  install_dir: pkgdatadir / 'help',
)
if have_terminal
  install_data('plugins/terminal/terminal.md', install_dir: pkgdatadir / 'help')
endif

# The headers of the screen library, S-Lang or ncurses, which lib/tty/tty.h includes; not the
# library itself: a module calls it through the program, which links it.
screen_headers = []
foreach d : screen_deps
  screen_headers += d.partial_dependency(compile_args: true, includes: true)
endforeach

if have_terminal
  subdir('plugins/terminal')
endif

if have_gmodule
  subdir('plugins/viewer')
endif

if lua.found()
  subdir('plugins/lua')
endif
