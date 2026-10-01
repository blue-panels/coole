libmc_sources = files(
  'charsets.c',
  'editor-plugin.c',
  'extension-runtime.c',
  'glibcompat.c',
  'global.c',
  'hook.c',
  'keybind.c',
  'lock.c',
  'plugin-prefs.c',
  'plugin-service.c',
  'runtime-events.c',
  'serialize.c',
  'shell.c',
  'terminal.c',
  'timefmt.c',
  'util.c',
  'utilunix.c',

  'event/event.c',
  'event/manage.c',
  'event/raise.c',

  'mcconfig/common.c',
  'mcconfig/get.c',
  'mcconfig/history.c',
  'mcconfig/paths.c',
  'mcconfig/set.c',

  'search/glob.c',
  'search/hex.c',
  'search/lib.c',
  'search/normal.c',
  'search/regex.c',
  'search/search.c',

  'skin/colors.c',
  'skin/common.c',
  'skin/hc-skins.c',
  'skin/ini-file.c',
  'skin/lines.c',

  'strutil/filevercmp.c',
  'strutil/replace.c',
  'strutil/strescape.c',
  'strutil/strutil.c',
  'strutil/strutil8bit.c',
  'strutil/strutilascii.c',
  'strutil/strutilutf8.c',
  'strutil/strverscmp.c',
  'strutil/tokenize.c',
  'strutil/xstrtol.c',

  'tty/color-internal.c',
  'tty/color.c',
  'tty/key.c',
  'tty/keyxdef.c',
  'tty/mouse.c',
  'tty/tty-internal.c',
  'tty/tty.c',
  'tty/win.c',


  'widget/background.c',
  'widget/button.c',
  'widget/buttonbar.c',
  'widget/check.c',
  'widget/dialog-switch.c',
  'widget/dialog.c',
  'widget/frame.c',
  'widget/gauge.c',
  'widget/group.c',
  'widget/groupbox.c',
  'widget/history.c',
  'widget/hline.c',
  'widget/input.c',
  'widget/input_complete.c',
  'widget/label.c',
  'widget/listbox-window.c',
  'widget/listbox.c',
  'widget/menu.c',
  'widget/mouse.c',
  'widget/quick.c',
  'widget/radio.c',
  'widget/rect.c',
  'widget/table.c',
  'widget/textarea.c',
  'widget/widget-common.c',
  'widget/wtools.c',
)

if get_option('screen') == 'slang'
  libmc_sources += files('tty/color-slang.c', 'tty/tty-slang.c')
else
  libmc_sources += files('tty/color-ncurses.c', 'tty/tty-ncurses.c')
endif
if x11.found()
  libmc_sources += files('tty/x11conn.c')
endif
if get_option('debug')
  libmc_sources += files('logging.c')
endif

libmc_deps = [glib_dep, screen_deps, gpm, x11_dep]

libmc = static_library('mc',
  libmc_sources, version_h,
  c_args: [
    '-DMC_EDITOR_PLUGINS_DIR="@0@"'.format(editor_plugins_dir),
    '-DMC_RUNTIME_PLUGINS_DIR="@0@"'.format(runtime_plugins_dir),
  ],
  dependencies: libmc_deps,
)

libmc_dep = declare_dependency(link_with: libmc, dependencies: libmc_deps)
