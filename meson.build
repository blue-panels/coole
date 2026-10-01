project('coole', 'c',
  version: run_command('version.sh', meson.project_source_root(), '--short',
                       check: true).stdout().strip(),
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.61',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
    'buildtype=debugoptimized',
  ],
)

cc = meson.get_compiler('c')
fs = import('fs')

package = meson.project_name()
bugreport = 'https://github.com/blue-panels/coole/issues'

# ---------------------------------------------------------------------------------------------
# Directories

prefix = get_option('prefix')
bindir = prefix / get_option('bindir')
libdir = prefix / get_option('libdir')
datadir = prefix / get_option('datadir')
sysconfdir = prefix / get_option('sysconfdir')
libexecdir = prefix / get_option('libexecdir')
localedir = prefix / get_option('localedir')
mandir = prefix / get_option('mandir')
docdir = datadir / 'doc' / package

pkgdatadir = datadir / package
pkgsysconfdir = sysconfdir / package
pkglibexecdir = libexecdir / package

editor_plugins_dir = get_option('editor_plugins_dir')
if editor_plugins_dir == ''
  editor_plugins_dir = libdir / package / 'editor-plugins'
endif
runtime_plugins_dir = get_option('runtime_plugins_dir')
if runtime_plugins_dir == ''
  runtime_plugins_dir = libdir / package / 'runtime-plugins'
endif
lua_scripts_dir = get_option('lua_scripts_dir')
if lua_scripts_dir == ''
  lua_scripts_dir = pkgdatadir / 'lua' / 'scripts'
endif
lua_modules_dir = get_option('lua_modules_dir')
if lua_modules_dir == ''
  lua_modules_dir = pkgdatadir / 'lua' / 'lib'
endif

# ---------------------------------------------------------------------------------------------
# Compiler flags

add_project_arguments(cc.get_supported_arguments([
    '-fdiagnostics-show-option',
    '-Wattributes',
    '-Wimplicit-function-declaration',
    '-Wimplicit-int',
    '-Wno-declaration-after-statement',
    '-Wno-long-long',
    '-Wno-vla',
    '-Wincompatible-pointer-types',
    '-Wint-conversion',
    '-Wbad-function-cast',
    '-Wconditional-uninitialized',
    '-Wfloat-conversion',
    '-Wfloat-equal',
    '-Wformat-security',
    '-Wformat-signedness',
    '-Wimplicit-fallthrough',
    '-Wmissing-declarations',
    '-Wmissing-format-attribute',
    '-Wmissing-prototypes',
    '-Wmissing-variable-declarations',
    '-Wnested-externs',
    '-Wpointer-arith',
    '-Wredundant-decls',
    '-Wshadow',
    '-Wstrict-prototypes',
    '-Wswitch-default',
    '-Wundef',
    '-Wunreachable-code',
    '-Wunused-result',
    '-Wwrite-strings',
    # casts like (GCompareDataFunc) with a missing parameter
    '-Wno-cast-function-type',
    # https://github.com/llvm/llvm-project/issues/20574
    '-Wno-assign-enum',
  ]),
  language: 'c')

# the sources include "lib/...", "src/..." and <config.h> from the top of the tree
add_project_arguments('-I' + meson.project_build_root(), '-I' + meson.project_source_root(),
                      language: 'c')

# ---------------------------------------------------------------------------------------------
# Dependencies

conf = configuration_data()

glib = dependency('glib-2.0', version: '>= 2.58')

gmodule = dependency('gmodule-no-export-2.0', version: '>= 2.58', required: false)
if not gmodule.found()
  gmodule = dependency('gmodule-2.0', version: '>= 2.58', required: false)
endif
have_gmodule = gmodule.found() and gmodule.get_variable('gmodule_supported', default_value: 'false') == 'true'
conf.set('HAVE_GMODULE', have_gmodule ? 1 : false)
glib_dep = have_gmodule ? [glib, gmodule] : [glib]

screen_deps = []
if get_option('screen') == 'slang'
  slang = dependency('slang', version: '>= 2.0')
  screen_deps += slang
  conf.set('HAVE_SLANG', 1)
  screen_msg = 'S-Lang'
  # a S-Lang built for termcap needs a library that provides tgoto
  if not cc.links('''#include <slang.h>
                     int main (void) { SLtt_get_terminfo (); SLtt_tgetflag ((char *) ""); return 0; }''',
                  dependencies: slang, name: 'S-Lang uses terminfo')
    termcap = cc.find_library('termcap', required: false)
    if not termcap.found()
      termcap = cc.find_library('curses', required: true)
    endif
    screen_deps += termcap
    conf.set('USE_TERMCAP', 1)
    screen_msg += ' with termcap database'
  endif
else
  ncurses = dependency('ncursesw', required: false)
  screen_msg = 'NCursesw'
  if not ncurses.found()
    ncurses = dependency('ncurses')
    screen_msg = 'NCurses'
  endif
  screen_deps += ncurses
  if not cc.has_function('stdscr', dependencies: ncurses, prefix: '#include <curses.h>')
    tinfo = dependency('tinfow', required: false)
    if not tinfo.found()
      tinfo = dependency('tinfo')
    endif
    screen_deps += tinfo
  endif
  conf.set('HAVE_NCURSES', 1)
  conf.set_quoted('NCURSES_LIB_DISPLAYNAME', screen_msg)
  conf.set('HAVE_NCURSES_WIDECHAR', cc.has_function('addwstr', dependencies: screen_deps) ? 1 : false)
  found = false
  foreach h : ['ncursesw/ncurses.h', 'ncursesw/curses.h', 'ncurses/ncurses.h', 'ncurses/curses.h',
               'ncurses.h', 'curses.h']
    if not found and cc.has_header(h, dependencies: screen_deps)
      conf.set('HAVE_' + h.underscorify().to_upper(), 1)
      found = true
    endif
  endforeach
  if not found
    error('NCurses(w) header file not found')
  endif
  found = false
  foreach h : ['ncursesw/term.h', 'ncurses/term.h', 'term.h']
    if not found and cc.has_header(h, dependencies: screen_deps)
      conf.set('HAVE_' + h.underscorify().to_upper(), 1)
      found = true
    endif
  endforeach
  if not found
    error('NCurses(w) term.h header file not found')
  endif
  # if ncurses exports ESCDELAY, it is set to 0: without it Esc has to be pressed three times
  conf.set('HAVE_ESCDELAY', cc.links('extern int ESCDELAY; int main (void) { ESCDELAY = 0; return 0; }',
                                     dependencies: screen_deps, name: 'ESCDELAY variable') ? 1 : false)
  conf.set('HAVE_RESIZETERM', cc.has_function('resizeterm', dependencies: screen_deps) ? 1 : false)
endif

gpm = dependency('', required: false)
if host_machine.system() == 'linux' and not get_option('gpm').disabled()
  gpm = cc.find_library('gpm', required: get_option('gpm'))
  if gpm.found() and not cc.has_function('Gpm_Repeat', dependencies: gpm)
    if get_option('gpm').enabled()
      error('libgpm is missing or older than 0.18')
    endif
    gpm = dependency('', required: false)
  endif
endif
conf.set('HAVE_LIBGPM', gpm.found() ? 1 : false)

# the X server is only asked for the keyboard modifiers; with gmodule libX11 is opened at run time
x11 = dependency('x11', required: get_option('x11'))
x11_dep = []
if x11.found()
  conf.set('HAVE_TEXTMODE_X11_SUPPORT', 1)
  x11_dep = have_gmodule ? x11.partial_dependency(compile_args: true, includes: true) : x11
endif

have_spell = have_gmodule and not get_option('spell').disabled()
if get_option('spell').enabled() and not have_gmodule
  error('spell checking needs gmodule')
endif
conf.set('HAVE_ASPELL', have_spell ? 1 : false)

# the terminal plugin runs a shell on a pty; openpty() is in libc or in libutil. It is a module
# the editor loads, so it needs gmodule.
terminal_dep = []
have_terminal = false
if have_gmodule and not get_option('terminal').disabled()
  if cc.has_function('openpty')
    have_terminal = true
  else
    libutil = cc.find_library('util', required: false)
    if libutil.found() and cc.has_function('openpty', dependencies: libutil)
      have_terminal = true
      terminal_dep = libutil
    endif
  endif
endif
if get_option('terminal').enabled() and not have_terminal
  error(have_gmodule ? 'the terminal needs openpty()' : 'the terminal plugin needs gmodule')
endif
conf.set('ENABLE_MCTERM', have_terminal ? 1 : false)
if have_terminal
  foreach h : ['pty.h', 'util.h', 'libutil.h']
    conf.set('HAVE_' + h.underscorify().to_upper(), cc.has_header(h) ? 1 : false)
  endforeach
  conf.set('HAVE_CLOSEFROM', cc.has_function('closefrom') ? 1 : false)
endif

lua = dependency('', required: false)
if have_gmodule and not get_option('lua').disabled()
  foreach name : ['lua5.4', 'lua-5.4', 'lua54', 'lua5.3', 'lua-5.3', 'lua53', 'lua']
    if not lua.found()
      lua = dependency(name, version: '>= 5.3', required: false)
    endif
  endforeach
endif
if get_option('lua').enabled() and not lua.found()
  error(have_gmodule ? 'Lua 5.3 or newer was not found' : 'the Lua plugin needs gmodule')
endif
conf.set('ENABLE_LUA_PLUGIN', lua.found() ? 1 : false)

# unit tests: the test programs run on the build machine
check = dependency('check', version: '>= 0.9.10',
                   required: get_option('tests').disable_auto_if(meson.is_cross_build()))
conf.set('HAVE_TESTS', check.found() ? 1 : false)

intl = dependency('intl', required: get_option('nls'))
msgfmt = find_program('msgfmt', required: get_option('nls'))
have_nls = intl.found() and msgfmt.found()
conf.set('ENABLE_NLS', have_nls ? 1 : false)

# ---------------------------------------------------------------------------------------------
# config.h

conf.set_quoted('PACKAGE', package)
conf.set_quoted('PACKAGE_NAME', package)
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('PACKAGE_BUGREPORT', bugreport)

# AC_USE_SYSTEM_EXTENSIONS
foreach m : ['_GNU_SOURCE', '_ALL_SOURCE', '_DARWIN_C_SOURCE', '__EXTENSIONS__', '_NETBSD_SOURCE',
             '_OPENBSD_SOURCE', '_POSIX_PTHREAD_SEMANTICS', '_TANDEM_SOURCE',
             '_HPUX_ALT_XOPEN_SOCKET_API']
  conf.set(m, 1)
endforeach

if not get_option('assert')
  conf.set('G_DISABLE_ASSERT', 1)
endif
if get_option('debug')
  conf.set('USE_MAINTAINER_MODE', 1)
endif

foreach h : ['string.h', 'memory.h', 'utime.h', 'sys/param.h', 'sys/select.h', 'sys/ioctl.h']
  conf.set('HAVE_' + h.underscorify().to_upper(), cc.has_header(h) ? 1 : false)
endforeach

# in the order of AC_HEADER_MAJOR: Solaris has a sys/sysmacros.h too, but
# makedev() is in sys/mkdev.h there
if cc.has_header_symbol('sys/mkdev.h', 'makedev')
  conf.set('MAJOR_IN_MKDEV', 1)
elif cc.has_header_symbol('sys/sysmacros.h', 'makedev')
  conf.set('MAJOR_IN_SYSMACROS', 1)
endif

foreach f : ['strverscmp', 'strncasecmp', 'strcasecmp', 'realpath', 'utimensat', 'setlocale',
             'statlstat']
  conf.set('HAVE_' + f.to_upper(), cc.has_function(f) ? 1 : false)
endforeach

foreach m : ['st_blksize', 'st_blocks', 'st_rdev', 'st_mtim', 'st_mtimespec', 'st_mtimensec']
  conf.set('HAVE_STRUCT_STAT_' + m.to_upper(),
           cc.has_member('struct stat', m, prefix: '#include <sys/stat.h>') ? 1 : false)
endforeach
conf.set('HAVE_STRUCT_DIRENT_D_TYPE', cc.has_member('struct dirent', 'd_type', prefix: '#include <dirent.h>') ? 1 : false)

conf.set('HAVE_SIGHANDLER_T', cc.has_type('sighandler_t', prefix: '#define _GNU_SOURCE\n#include <signal.h>') ? 1 : false)
conf.set('SIG_ATOMIC_VOLATILE_T', 'volatile sig_atomic_t')
conf.set10('HAVE_DECL_ENVIRON', cc.has_header_symbol('unistd.h', 'environ', prefix: '#define _GNU_SOURCE'))

# the type a mode_t becomes when it is passed through "..."
if cc.sizeof('mode_t', prefix: '#include <sys/types.h>') < cc.sizeof('int')
  conf.set('PROMOTED_MODE_T', 'int')
else
  conf.set('PROMOTED_MODE_T', 'mode_t')
endif

conf.set('ENABLE_CONFIGURE_ARGS', 1)
conf.set_quoted('MC_CONFIGURE_ARGS', ' '.join([
  'buildtype=' + get_option('buildtype'),
  'screen=' + get_option('screen'),
  'gpm=' + (gpm.found() ? 'yes' : 'no'),
  'x11=' + (x11.found() ? 'yes' : 'no'),
  'lua=' + (lua.found() ? 'yes' : 'no'),
  'spell=' + (have_spell ? 'yes' : 'no'),
  'terminal=' + (have_terminal ? 'yes' : 'no'),
  'nls=' + (have_nls ? 'yes' : 'no'),
]))

configure_file(output: 'config.h', configuration: conf)

# the full version, "git describe", made at build time so that a new commit rebuilds one file
version_h = vcs_tag(
  command: [meson.project_source_root() / 'version.sh', meson.project_source_root()],
  input: 'mc-version.h.in',
  output: 'mc-version.h',
  fallback: meson.project_version(),
)

# names of the encodings in the iconv of the system
if host_machine.system() in ['sunos', 'solaris']
  encoding_cp1251 = 'ANSI-1251'
else
  encoding_cp1251 = 'CP1251'
endif
if cc.has_header('gnu/libc-version.h')
  encoding_cp866 = 'IBM866'
  encoding_iso8859 = 'ISO-8859'
else
  encoding_cp866 = 'CP866'
  encoding_iso8859 = 'ISO8859'
endif
encoding_conf = configuration_data({
  'ENCODING_CP1251': encoding_cp1251,
  'ENCODING_CP866': encoding_cp866,
  'ENCODING_ISO8859': encoding_iso8859,
})

top_inc = include_directories('.')

# the languages the man page and the help are translated to
doc_languages = ['es', 'hu', 'it', 'pl', 'ru', 'sr']

# ---------------------------------------------------------------------------------------------

subdir('lib')
subdir('src')
subdir('misc')
subdir('doc')
if have_nls
  subdir('po')
endif
if check.found()
  subdir('tests')
endif

meson.add_dist_script('maint/meson-dist.sh')

# maintainer targets: "meson compile -C <builddir> <name>"
run_target('indent', command: ['maint/indent.sh', meson.project_source_root()])
# the roff of the man pages, made from their markdown before a commit; needs go-md2man
run_target('man-in', command: ['maint/update-man-in.sh', meson.project_source_root()])
run_target('check-glib-api', command: ['sh', meson.project_source_root() / 'maint/check-glib-api.sh'])
run_target('buildcheck', command: [meson.project_source_root() / 'maint/buildcheck.sh'])

summary({
  'Screen library': screen_msg,
  'Mouse': gpm.found() ? 'gpm and xterm' : 'xterm only',
  'X11 events': x11.found(),
  'Spell checking': have_spell,
  'Terminal': have_terminal,
  'Lua runtime plugin': lua.found() ? lua.version() : false,
  'Translations': have_nls,
  'Unit tests': check.found(),
  'Assertions': get_option('assert'),
}, bool_yn: true)
