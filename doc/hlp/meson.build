# The help window reads the markdown of the man page, with the screens that
# belong to the help alone (the main screen, how to use the help) appended.
cat = find_program('cat')

custom_target('coole.md',
  input: ['../man/coole.md', 'help-screens.md'],
  output: 'coole.md',
  command: [cat, '@INPUT@'],
  capture: true,
  install: true,
  install_dir: pkgdatadir / 'help',
)

install_data('gnu-gpl-v3.0.md', install_dir: pkgdatadir / 'help')

if have_nls
  foreach lang : doc_languages
    custom_target('coole.@0@.md'.format(lang),
      input: ['../man' / lang / 'coole.md', lang / 'help-screens.md'],
      output: 'coole.@0@.md'.format(lang),
      command: [cat, '@INPUT@'],
      capture: true,
      install: true,
      install_dir: pkgdatadir / 'help',
    )
  endforeach
endif
