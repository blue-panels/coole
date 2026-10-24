# The query plugin, coole -Q: a module the editor loads at run time from plugins_dir, as the
# debugger.  It reads the documents of mongosh --json, so it is there when json-glib is.
query_module = shared_module('query',
  'query.c', 'query-core.c', 'query-mongo.c', 'query-postgres.c', 'query-pq.c', 'query-pg-schema.c',
  name_prefix: '',
  c_args: src_c_args,
  dependencies: [glib_dep, intl, screen_headers, json_glib],
  install: true,
  install_dir: plugins_dir,
)
plugin_build_dirs += meson.current_build_dir()
