i18n = import('i18n')

# The template and the catalogues: "ninja coole-pot" remakes po/coole.pot from
# the sources listed in POTFILES.in, "ninja coole-update-po" merges the .po files.
i18n.gettext(package,
  args: [
    '--keyword=_', '--keyword=N_', '--keyword=Q_',
    '--from-code=UTF-8',
    '--msgid-bugs-address=' + bugreport,
    '--copyright-holder=Free Software Foundation, Inc.',
  ],
)
