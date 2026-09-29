# The roff under roff/ is made from the markdown by maint/update-man-in.sh
# before a commit; the build only puts the paths of this installation in.
sed = find_program('sed')

man_version = meson.project_version().split('-')[0]
man_sed_args = [
  '-e', 's|{{MAN_VERSION}}|@0@|g'.format(man_version),
  '-e', 's|{{sysconfdir}}|@0@|g'.format(sysconfdir),
  '-e', 's|{{libexecdir}}|@0@|g'.format(libexecdir),
  '-e', 's|{{pkglibexecdir}}|@0@|g'.format(pkglibexecdir),
  '-e', 's|{{pkgdatadir}}|@0@|g'.format(pkgdatadir),
  '-e', 's|{{editor_plugins_dir}}|@0@|g'.format(editor_plugins_dir),
  '-e', 's|{{bindir}}|@0@|g'.format(bindir),
]

custom_target('coole.1',
  input: 'roff/coole.1.in',
  output: 'coole.1',
  command: [sed, man_sed_args, '@INPUT@'],
  capture: true,
  install: true,
  install_dir: mandir / 'man1',
)

# the translated pages, each in a directory of its own for the one name coole.1
if have_nls
  foreach lang : doc_languages
    subdir(lang)
  endforeach
endif
