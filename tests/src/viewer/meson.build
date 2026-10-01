# the viewer plugin loads only when the program gives it every function it calls
if module_symbols_sh.found()
  test('module_symbols', module_symbols_sh, args: [coole, viewer_module], suite: 'src/viewer')
endif
