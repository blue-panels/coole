-- What AI is told of a relation, a row each, partitions left out: its columns and keys, the names
-- as SQL takes them; of PostgreSQL 12 and later as the columns
SELECT n.nspname, c.relname, c.relkind,
       (SELECT string_agg (pg_catalog.quote_ident (a.attname) || ' '
                           || pg_catalog.format_type (a.atttypid, a.atttypmod)
                           || CASE WHEN a.attnotnull THEN ' not null' ELSE '' END,
                           ', ' ORDER BY a.attnum)
          FROM pg_catalog.pg_attribute a
         WHERE a.attrelid = c.oid AND a.attnum > 0 AND NOT a.attisdropped),
       (SELECT string_agg (pg_catalog.pg_get_constraintdef (o.oid), ', '
                           ORDER BY o.contype <> 'p', o.conname)
          FROM pg_catalog.pg_constraint o
         WHERE o.conrelid = c.oid AND o.contype IN ('p', 'u', 'f'))
  FROM pg_catalog.pg_namespace n
  JOIN pg_catalog.pg_class c ON c.relnamespace = n.oid
 WHERE c.relkind IN ('r', 'p', 'v', 'm', 'f') AND NOT c.relispartition
   AND {{> pg-user-nsp}}
 ORDER BY n.nspname, c.relname
 LIMIT 5000
