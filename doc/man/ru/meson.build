custom_target('bluerig.1-ru',
  input: 'roff/bluerig.1.in',
  output: 'bluerig.1',
  command: [sed, man_sed_args, '@INPUT@'],
  capture: true,
  install: true,
  install_dir: mandir / 'ru' / 'man1',
)
