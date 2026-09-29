custom_target('coole.1-pl',
  input: 'roff/coole.1.in',
  output: 'coole.1',
  command: [sed, man_sed_args, '@INPUT@'],
  capture: true,
  install: true,
  install_dir: mandir / 'pl' / 'man1',
)
