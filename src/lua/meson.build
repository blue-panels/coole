# The Lua runtime plugin: loaded by the program at run time from runtime_plugins_dir.
mc_lua = shared_module('mc-lua',
  'mc-lua.c',
  name_prefix: '',
  c_args: [
    '-DMC_LUA_SYSTEM_SCRIPTS_DIR="@0@"'.format(lua_scripts_dir),
    '-DMC_LUA_SYSTEM_MODULES_DIR="@0@"'.format(lua_modules_dir),
  ],
  dependencies: [glib, lua],
  install: true,
  install_dir: runtime_plugins_dir,
)

install_subdir('scripts/editor', install_dir: lua_scripts_dir)

# teaching examples: installed for reference only, never loaded by the engine
install_subdir('examples/editor', install_dir: pkgdatadir / 'lua' / 'examples')
