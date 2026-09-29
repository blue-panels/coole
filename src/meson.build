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

  'vfs/plugins_init.c',
  'vfs/local/local.c',

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
  'editor/fold.c',
  'editor/format.c',
  'editor/syntax.c',

  # src/editor-plugins/builtin-plugins.c is included by editor/editcmd.c
  'editor-plugins/ctags/ctags-config.c',
  'editor-plugins/ctags/ctags-fuzzy.c',
  'editor-plugins/ctags/ctags-index.c',
  'editor-plugins/ctags/ctags-parser.c',
  'editor-plugins/ctags/ctags-plugin.c',
  'editor-plugins/ctags/ctags-repository.c',
  'editor-plugins/ctags/ctags-ui.c',
  'editor-plugins/etags/etags.c',
  'editor-plugins/spell/spell.c',
)

src_c_args = [
  '-DSYSCONFDIR="@0@/"'.format(pkgsysconfdir),
  '-DLIBEXECDIR="@0@/"'.format(pkglibexecdir),
  '-DDATADIR="@0@/"'.format(pkgdatadir),
  '-DLOCALEDIR="@0@"'.format(localedir),
  '-DMC_EDITOR_PLUGINS_DIR="@0@"'.format(editor_plugins_dir),
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

libinternal = static_library('internal',
  libinternal_sources,
  c_args: src_c_args,
  dependencies: [libmc_dep, intl],
)

libinternal_dep = declare_dependency(link_with: libinternal, dependencies: [libmc_dep, intl])

coole = executable('coole',
  'main.c',
  c_args: src_c_args,
  dependencies: libinternal_dep,
  # the runtime plugins call back into the program
  export_dynamic: have_gmodule,
  install: true,
)

# the help of the editor plugins, installed beside the help of the editor so
# that a dialog of a plugin can name its own file
install_data(
  'editor-plugins/ctags/ctags.md',
  'editor-plugins/etags/etags.md',
  'editor-plugins/spell/spell.md',
  install_dir: pkgdatadir / 'help',
)

if lua.found()
  subdir('lua')
endif
