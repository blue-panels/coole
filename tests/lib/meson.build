lib_tests = {
  'key': 'key.c',
  'library_independ': 'library_independ.c',
  'mc_build_filename': 'mc_build_filename.c',
  'mc_realpath': 'mc_realpath.c',
  'name_quote': 'name_quote.c',
  'serialize': 'serialize.c',
  'runtime_events': 'runtime_events.c',
  'terminal': 'terminal.c',
  'tty': 'tty.c',
  'tty_color': 'tty_color.c',
  'utilunix__mc_pread_signal': 'utilunix__mc_pread_signal.c',
  'utilunix__mc_pstream_get_string': 'utilunix__mc_pstream_get_string.c',
  'utilunix__my_system_fork_fail': 'utilunix__my_system-fork_fail.c',
  'utilunix__my_system_fork_child_shell': 'utilunix__my_system-fork_child_shell.c',
  'utilunix__my_system_fork_child': 'utilunix__my_system-fork_child.c',
  'x_basename': 'x_basename.c',
}

foreach name, source : lib_tests
  test(name,
    executable(name, source, c_args: test_c_args, dependencies: test_lib_deps),
    workdir: meson.current_build_dir(),
    suite: 'lib')
endforeach

if have_gmodule
  # a runtime plugin for the loader to find: the only module in this directory
  fixture = shared_module('runtime-plugin-fixture',
    'runtime_plugin_fixture.c',
    name_prefix: '',
    dependencies: glib)

  test('extension_runtime',
    executable('extension_runtime', 'extension_runtime.c',
      c_args: test_c_args + [
        '-DTEST_RUNTIME_PLUGIN_DIR="@0@"'.format(meson.current_build_dir()),
      ],
      dependencies: test_lib_deps),
    depends: fixture,
    workdir: meson.current_build_dir(),
    suite: 'lib')

  if lua.found()
    test('lua_runtime',
      executable('lua_runtime', 'lua_runtime.c',
        c_args: test_c_args + [
          '-DTEST_LUA_RUNTIME_DIR="@0@"'.format(meson.project_build_root() / 'src' / 'lua'),
          '-DTEST_LUA_EDITOR_SCRIPTS_DIR="@0@"'.format(
            meson.project_source_root() / 'src' / 'lua' / 'scripts' / 'editor'),
          '-DTEST_LUA_EDITOR_EXAMPLES_DIR="@0@"'.format(
            meson.project_source_root() / 'src' / 'lua' / 'examples' / 'editor'),
        ],
        dependencies: test_lib_deps),
      depends: mc_lua,
      workdir: meson.current_build_dir(),
      suite: 'lib')
  endif
endif

subdir('mcconfig')
subdir('search')
subdir('strutil')
subdir('vfs')
subdir('widget')
