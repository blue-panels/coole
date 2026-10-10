# The debugger plugin: a module the editor loads at run time from plugins_dir, as any other
# editor plugin.  It calls the functions of the editor, which the program exports.  The debug
# adapters are there when json-glib is.
debugger_sources = ['debugger.c', 'debug-disasm.c', 'debug-program.c', 'debug-panel.c', 'debug-values.c', 'debug-breakpoints.c', 'debug-backend.c', 'debug-gdb-mi.c', 'gdb-mi.c']
if json_glib.found()
  debugger_sources += ['dap.c', 'debug-dap.c']
endif

debugger_module = shared_module('debugger',
  debugger_sources,
  name_prefix: '',
  c_args: src_c_args,
  # openpty () for the terminal of the program
  dependencies: [glib_dep, intl, screen_headers, json_glib, terminal_dep],
  install: true,
  install_dir: plugins_dir,
)
plugin_build_dirs += meson.current_build_dir()
