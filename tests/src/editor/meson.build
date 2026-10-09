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

test('build_core', executable('build_core', 'build_core.c',
                              '../../../src/plugins/build/build-core.c',
                              dependencies: [glib_dep]), suite: 'src/editor')

test('git_core', executable('git_core', 'git_core.c',
                            '../../../src/plugins/git/git-core.c',
                            c_args: ['-DGIT_MENU_INI="@0@"'.format(
                              meson.project_source_root() / 'misc' / 'git-menu.ini')],
                            dependencies: [glib_dep]), suite: 'src/editor')
