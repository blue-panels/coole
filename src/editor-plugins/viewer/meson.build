# The viewer plugin: a module the editor loads at run time from editor_plugins_dir, as any other
# editor plugin.  It offers the service "viewer", the windows that show the text a script or
# another plugin gives them.
viewer_module = shared_module('viewer',
  'textwindow.c',
  'viewer.c',
  name_prefix: '',
  c_args: src_c_args,
  dependencies: [glib_dep, intl, screen_headers],
  install: true,
  install_dir: editor_plugins_dir,
)
