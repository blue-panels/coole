# The viewer plugin: a module the editor loads at run time from plugins_dir, as any other
# editor plugin.  It offers the service "viewer", the windows that show the text a script or
# another plugin gives them.
viewer_module = shared_module('viewer',
  'textwindow.c',
  'viewer.c',
  name_prefix: '',
  c_args: src_c_args,
  dependencies: [glib_dep, intl, screen_headers],
  install: true,
  install_dir: plugins_dir,
)
plugin_build_dirs += meson.current_build_dir()
