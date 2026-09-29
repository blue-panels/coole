custom_target('coole.1-ru',
  input: 'roff/coole.1.in',
  output: 'coole.1',
  command: [sed, man_sed_args, '@INPUT@'],
  capture: true,
  install: true,
  install_dir: mandir / 'ru' / 'man1',
)
