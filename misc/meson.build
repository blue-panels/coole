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
  input: 'coole.menu.in',
  output: 'coole.menu',
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
