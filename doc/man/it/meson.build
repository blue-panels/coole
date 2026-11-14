custom_target('bluerig.1-it',
  input: 'roff/bluerig.1.in',
  output: 'bluerig.1',
  command: [sed, man_sed_args, '@INPUT@'],
  capture: true,
  install: true,
  install_dir: mandir / 'it' / 'man1',
)
