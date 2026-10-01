# Unit tests on the check framework: "meson test -C <builddir>".
# Every test runs in the build directory of its own subdirectory, where the
# files it reads are made.
test_c_args = []
test_lib_deps = [libmc_dep, check]
test_src_deps = [libinternal_dep, check]

# a plugin module loads only when the program gives it every function it calls: the check needs
# nm -D and ldd
module_symbols_sh = disabler()
if host_machine.system() == 'linux' and find_program('nm', required: false).found() and find_program('ldd', required: false).found()
  module_symbols_sh = find_program('module_symbols.sh')
endif

subdir('lib')
subdir('src')
