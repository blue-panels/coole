-- The relations of the schemas, a schema with none having one row with no relation, a partition
-- with its table; the functions, those of extensions left out, and their oid for F4
SELECT n.nspname, c.oid, c.relname, c.relkind, c.reltuples::bigint, c.relispartition,
       (SELECT i.inhparent FROM pg_catalog.pg_inherits i WHERE i.inhrelid = c.oid LIMIT 1)
  FROM pg_catalog.pg_namespace n
  LEFT JOIN pg_catalog.pg_class c
    ON c.relnamespace = n.oid AND c.relkind IN ('r', 'p', 'v', 'm', 'f', 'S')
 WHERE {{> pg-user-nsp}}
 ORDER BY n.nspname, c.relname;
SELECT n.nspname, p.proname, pg_catalog.pg_get_function_identity_arguments (p.oid),
       pg_catalog.pg_get_function_result (p.oid), p.prokind, p.oid
  FROM pg_catalog.pg_proc p
  JOIN pg_catalog.pg_namespace n ON n.oid = p.pronamespace
 WHERE {{> pg-user-nsp}}
   AND NOT EXISTS (SELECT 1 FROM pg_catalog.pg_depend d
                    WHERE d.classid = 'pg_catalog.pg_proc'::pg_catalog.regclass
                      AND d.objid = p.oid AND d.deptype = 'e')
 ORDER BY 1, 2, 3
