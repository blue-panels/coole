# the tests of the terminal, as they are in mc
foreach name : ['mcterm_key_encode', 'mcterm_osc7_parse', 'mcterm_protocol_parse',
                'mcterm_pty_send', 'mcterm_select']
  test(name,
    executable(name, name + '.c',
      c_args: test_c_args,
      dependencies: test_src_deps),
    workdir: meson.current_build_dir(),
    # Solaris closes a pty with bytes nobody read in a STREAMS queue only
    # after a wait: the paste test takes some 30 s there
    timeout: name == 'mcterm_pty_send' ? 120 : 30,
    suite: 'src/mcterm')
endforeach
