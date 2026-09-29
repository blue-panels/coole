foreach name : ['complete_engine', 'hotkey_equal', 'group_init_destroy', 'group_set_state',
                'table_datasource', 'textarea', 'widget_find_by_id', 'widget_make_global_local']
  test(name,
    executable(name, name + '.c',
      c_args: test_c_args,
      include_directories: include_directories('../../../lib/vfs'),
      dependencies: test_lib_deps),
    workdir: meson.current_build_dir(),
    suite: 'lib/widget')
endforeach
