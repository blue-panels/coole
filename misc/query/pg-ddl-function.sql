-- F4: the DDL of a function, read with no schema of the user in the search path: its definition,
-- none for an aggregate, which has none; its kind; its name and its arguments as SQL takes them.
-- {{oid}}: its oid
BEGIN;
SET LOCAL search_path = pg_catalog;
SELECT CASE WHEN p.prokind <> 'a' THEN pg_catalog.pg_get_functiondef (p.oid) END,
       p.prokind,
       pg_catalog.quote_ident (n.nspname) || '.' || pg_catalog.quote_ident (p.proname) || '('
       || pg_catalog.pg_get_function_identity_arguments (p.oid) || ')'
  FROM pg_catalog.pg_proc p
  JOIN pg_catalog.pg_namespace n ON n.oid = p.pronamespace
 WHERE p.oid = {{oid}};
COMMIT
