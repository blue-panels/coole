foreach name : ['clipboard_info', 'input_clip_text', 'file_history', 'keymap_reload',
                'runtime_host__syntax', 'learn_modifiers', 'term_key_merge',
                'usermenu__test_condition', 'usermenu__test_expand_format']
  test(name,
    executable(name, name + '.c',
      c_args: test_c_args + [
        '-DTEST_SHARE_DIR="@0@"'.format(meson.current_source_dir() / 'fixtures'),
      ],
      dependencies: test_src_deps),
    workdir: meson.current_build_dir(),
    suite: 'src')
endforeach

subdir('syntax')
subdir('editor')
if have_terminal
  subdir('mcterm')
endif
if have_gmodule
  subdir('viewer')
endif
