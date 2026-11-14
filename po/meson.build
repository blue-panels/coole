i18n = import('i18n')

# The template and the catalogues: "ninja bluerig-pot" remakes po/bluerig.pot from
# the sources listed in POTFILES.in, "ninja bluerig-update-po" merges the .po files.
i18n.gettext(package,
  args: [
    '--keyword=_', '--keyword=N_', '--keyword=Q_',
    '--from-code=UTF-8',
    '--msgid-bugs-address=' + bugreport,
    '--copyright-holder=Free Software Foundation, Inc.',
  ],
)
