-- What a relation unfolded shows: its columns, of PostgreSQL 12 and later (attgenerated); its
-- keys; its indexes; the foreign keys of the others to it; its triggers.  {{oid}}: its oid
SELECT a.attname, pg_catalog.format_type (a.atttypid, a.atttypmod), a.attnotnull,
       a.attidentity, a.attgenerated, pg_catalog.pg_get_expr (d.adbin, d.adrelid)
  FROM pg_catalog.pg_attribute a
  LEFT JOIN pg_catalog.pg_attrdef d ON d.adrelid = a.attrelid AND d.adnum = a.attnum
 WHERE a.attrelid = {{oid}} AND a.attnum > 0 AND NOT a.attisdropped
 ORDER BY a.attnum;
SELECT o.conname, o.contype, pg_catalog.pg_get_constraintdef (o.oid), {{> pg-key-columns}}
  FROM pg_catalog.pg_constraint o
 WHERE o.conrelid = {{oid}} AND o.contype IN ('p', 'u', 'f')
 ORDER BY o.contype <> 'p', o.conname;
SELECT c.relname, pg_catalog.pg_get_indexdef (i.indexrelid), i.indisprimary, i.indisunique
  FROM pg_catalog.pg_index i
  JOIN pg_catalog.pg_class c ON c.oid = i.indexrelid
 WHERE i.indrelid = {{oid}}
 ORDER BY 1;
SELECT o.conname, o.conrelid::pg_catalog.regclass::text, {{> pg-key-columns}}
  FROM pg_catalog.pg_constraint o
 WHERE o.confrelid = {{oid}} AND o.contype = 'f'
 ORDER BY 2, 1;
SELECT t.tgname, pg_catalog.pg_get_triggerdef (t.oid)
  FROM pg_catalog.pg_trigger t
 WHERE t.tgrelid = {{oid}} AND NOT t.tgisinternal
 ORDER BY 1
