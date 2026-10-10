foreach name : ['complete_engine', 'hotkey_equal', 'group_init_destroy', 'group_set_state',
                'scrollbar', 'table_datasource', 'textarea', 'widget_find_by_id',
                'widget_make_global_local', 'paste']
  test(name,
    executable(name, name + '.c',
      c_args: test_c_args,
      dependencies: test_lib_deps),
    workdir: meson.current_build_dir(),
    suite: 'lib/widget')
endforeach
