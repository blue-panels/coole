subdir('macros.d')
subdir('skins')
subdir('syntax')

install_data('defaults.ini', install_dir: pkgdatadir)

# the codepages the program offers, under the names the iconv of this system knows
configure_file(
  input: 'charsets.in',
  output: 'charsets',
  configuration: encoding_conf,
  install_dir: pkgdatadir,
)

# nroff option that turns the ANSI colour of man off, for the man entry of the user menu
nroff = find_program('nroff', required: false)
man_flags = ''
if nroff.found() and run_command('sh', '-c', '"$0" -c < /dev/null', nroff.full_path(),
                                 check: false).returncode() == 0
  man_flags = '-c'
endif
configure_file(
  input: 'bluerig.menu.in',
  output: 'bluerig.menu',
  configuration: {'MAN_FLAGS': man_flags},
  install_dir: pkgsysconfdir,
)

install_data(
  'keymap.default.ini',
  'keymap.emacs.ini',
  'keymap.vim.ini',
  'spell.keymap',
  'ctags.keymap',
  install_dir: pkgsysconfdir,
)
install_symlink('keymap.ini', pointing_to: 'keymap.default.ini', install_dir: pkgsysconfdir)

install_data('edit.indent.rc', install_dir: pkgsysconfdir, install_mode: 'rwxr-xr-x')
# the menu of a message of the git plugin and what it asks the AI
install_data('git-menu.ini', 'ai.ini', install_dir: pkgsysconfdir)
# how a project is built and debugged by its language; ~/.config/bluerig/presets.ini adds and
# replaces
install_data('presets.ini', install_dir: pkgsysconfdir)
# what the marks of the tree of the project stand for; ~/.config/bluerig/project-marks.ini adds
# and replaces
install_data('project-marks.ini', install_dir: pkgsysconfdir)

# the JavaScript of mongosh and the SQL of PostgreSQL the query plugin runs, read when it runs;
# ~/.config/bluerig/query/ and /etc/bluerig/query/ have the user's and the system's in their place
install_subdir('query', install_dir: pkgdatadir)

# the language servers the LSP plugin knows; ~/.config/bluerig/lsp.json adds and replaces
if have_gmodule and json_glib.found()
  install_data('lsp.json', install_dir: pkgsysconfdir)
endif
