# edit_complete_word_cmd opens this file from its working directory
configure_file(input: 'edit_complete_word_cmd_test_data.txt.in',
  output: 'edit_complete_word_cmd_test_data.txt',
  copy: true)

foreach name : ['edit_buffer_layout', 'edit_clip_codeset', 'edit_complete_word_cmd',
                'edit_control_chars', 'edit_fold', 'edit_marker', 'edit_insert_column_of_text',
                'edit_plugin_etags', 'edit_undo_history', 'edit_undo_modified', 'edit_save_sudo',
                'edit_replace_cmd', 'edit_window', 'nav_probe', 'ctags_parser_tests', 'ctags_index_tests',
                'ctags_fuzzy_tests']
  test(name,
    executable(name, name + '.c',
      c_args: test_c_args + [
        '-DTEST_SHARE_DIR="@0@"'.format(meson.current_source_dir() / '..' / 'fixtures'),
      ],
      dependencies: test_src_deps),
    workdir: meson.current_build_dir(),
    suite: 'src/editor')
endforeach

test('project_core', executable('project_core', 'project_core.c',
                                '../../../src/plugins/project/project-core.c',
                                dependencies: [glib_dep]), suite: 'src/editor')

test('gdb_mi', executable('gdb_mi', 'gdb_mi.c',
                         '../../../src/plugins/debugger/gdb-mi.c',
                         dependencies: [glib_dep, screen_deps]), suite: 'src/editor')

if json_glib.found()
  # the servers of the tests are scripts: run them with the python meson runs with, which is
  # there wherever meson is, when no python3 is in PATH
  test_python = import('python').find_installation(required: false)
  test_python_arg = '-DTEST_PYTHON="@0@"'.format(test_python.found() ? test_python.full_path()
                                                                     : 'python3')
  test('dap', executable('dap', 'dap.c',
                         '../../../src/plugins/debugger/dap.c', '../../../src/content-frame.c',
                         c_args: ['-DMOCK_DAP="@0@"'.format(meson.current_source_dir() / 'mock_dap.py'),
                                 test_python_arg],
                         dependencies: [glib_dep, screen_deps, json_glib]), suite: 'src/editor')
  test('debug_dap', executable('debug_dap', 'debug_dap.c',
                               '../../../src/plugins/debugger/debug-dap.c',
                               '../../../src/plugins/debugger/debug-backend.c',
                               '../../../src/plugins/debugger/dap.c',
                               '../../../src/content-frame.c',
                               c_args: ['-DMOCK_DAP="@0@"'.format(meson.current_source_dir() / 'mock_dap.py'),
                                 test_python_arg],
                               dependencies: [glib_dep, screen_deps, json_glib]), suite: 'src/editor')
  lsp_test_sources = ['../../../src/plugins/lsp/lsp-text.c',
                      '../../../src/plugins/lsp/lsp-config.c']
  test('lsp_text', executable('lsp_text', 'lsp_text.c', lsp_test_sources,
                              dependencies: [glib_dep, json_glib]), suite: 'src/editor')
  test('lsp_config', executable('lsp_config', 'lsp_config.c', lsp_test_sources,
                                dependencies: [glib_dep, json_glib]), suite: 'src/editor')
  test('lsp_settings', executable('lsp_settings', 'lsp_settings.c', lsp_test_sources,
                                  '../../../src/plugins/lsp/lsp-settings.c',
                                  dependencies: [glib_dep, json_glib]), suite: 'src/editor')
  test('lsp_markdown', executable('lsp_markdown', 'lsp_markdown.c',
                                  '../../../src/plugins/lsp/lsp-markdown.c',
                                  dependencies: [glib_dep]), suite: 'src/editor')
  test('lsp_setup', executable('lsp_setup', 'lsp_setup.c',
                               '../../../src/plugins/lsp/lsp-setup-core.c',
                               dependencies: [glib_dep, json_glib]), suite: 'src/editor')
  test('query_core', executable('query_core', 'query_core.c',
                                '../../../src/plugins/query/query-core.c',
                                dependencies: [glib_dep, json_glib]), suite: 'src/editor')
  test('lsp_client', executable('lsp_client', 'lsp_client.c', lsp_test_sources,
                                '../../../src/plugins/lsp/lsp-client.c',
                                '../../../src/content-frame.c',
                                c_args: ['-DMOCK_LSP="@0@"'.format(meson.current_source_dir() / 'mock_lsp.py'),
                                        test_python_arg],
                                dependencies: [glib_dep, screen_deps, json_glib]),
       suite: 'src/editor', timeout: 120)
endif

test('fuzzy', executable('fuzzy', 'fuzzy.c', '../../../src/fuzzy.c', dependencies: [glib_dep]),
     suite: 'src/editor')

test('content_frame', executable('content_frame', 'content_frame.c',
                                 '../../../src/content-frame.c',
                                 dependencies: [glib_dep]), suite: 'src/editor')

test('build_core', executable('build_core', 'build_core.c',
                              '../../../src/plugins/build/build-core.c',
                              '../../../src/plugins/build/build-presets.c',
                              c_args: ['-DPRESETS_INI="@0@"'.format(
                                meson.project_source_root() / 'misc' / 'presets.ini')],
                              dependencies: [glib_dep]), suite: 'src/editor')

test('git_core', executable('git_core', 'git_core.c',
                            '../../../src/plugins/git/git-core.c',
                            c_args: ['-DGIT_MENU_INI="@0@"'.format(
                              meson.project_source_root() / 'misc' / 'git-menu.ini')],
                            dependencies: [glib_dep]), suite: 'src/editor')

# the debugger plugin loads only when the program gives it every function it calls
if have_gmodule and module_symbols_sh.found()
  test('debugger_module_symbols', module_symbols_sh, args: [coole, debugger_module],
       suite: 'src/editor')
  if json_glib.found()
    test('lsp_module_symbols', module_symbols_sh, args: [coole, lsp_module], suite: 'src/editor')
    test('query_module_symbols', module_symbols_sh, args: [coole, query_module],
         suite: 'src/editor')
  endif
endif
