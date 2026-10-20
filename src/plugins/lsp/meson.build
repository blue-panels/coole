# The language servers: a module the editor loads at run time from plugins_dir, as the debugger.
# It speaks JSON, so it is there when json-glib is.
lsp_module = shared_module('lsp',
  'lsp-plugin.c', 'lsp-client.c', 'lsp-config.c', 'lsp-settings.c', 'lsp-text.c',
  'lsp-popup.c', 'lsp-setup.c', 'lsp-setup-core.c', 'lsp-wizard.c',
  name_prefix: '',
  c_args: src_c_args,
  dependencies: [glib_dep, intl, screen_headers, json_glib],
  install: true,
  install_dir: plugins_dir,
)
plugin_build_dirs += meson.current_build_dir()
