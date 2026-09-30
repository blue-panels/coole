# the tests of the terminal, as they are in mc
foreach name : ['mcterm_key_encode', 'mcterm_osc7_parse', 'mcterm_protocol_parse',
                'mcterm_pty_send', 'mcterm_select']
  test(name,
    executable(name, name + '.c',
      c_args: test_c_args,
      dependencies: test_src_deps),
    workdir: meson.current_build_dir(),
    suite: 'src/mcterm')
endforeach
