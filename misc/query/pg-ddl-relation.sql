-- F4: the DDL of a relation, read with no schema of the user in the search path: the names in
-- it, of a query of a view, of a foreign key, of the table of a partition, have their schema.
-- What it is, the partition it is and of what, the query of a view, the server of a foreign
-- table, its comment, the tables it inherits from, as SQL takes them, whether it is populated;
-- its columns, of PostgreSQL 12 and later (attgenerated), with the numbers and the name of the
-- sequence of an identity, the default and the NOT NULL its parents give it, how many defaults
-- they give it, the options of the column of a foreign table; its constraints of its own,
-- whether they are validated, the storage of the index of a key; its indexes that are of no
-- constraint and of no index of a partitioned table; its triggers and their mode; a sequence,
-- and the column of its identity or that owns it; the sequences its serial columns own.
-- {{oid}}: its oid
BEGIN;
SET LOCAL search_path = pg_catalog;
SELECT c.relkind, c.relpersistence, c.relispartition,
       (SELECT i.inhparent::pg_catalog.regclass::text FROM pg_catalog.pg_inherits i
         WHERE i.inhrelid = c.oid LIMIT 1),
       pg_catalog.pg_get_expr (c.relpartbound, c.oid),
       CASE WHEN c.relkind = 'p' THEN pg_catalog.pg_get_partkeydef (c.oid) END,
       CASE WHEN c.relkind IN ('v', 'm') THEN pg_catalog.pg_get_viewdef (c.oid, true) END,
       (SELECT pg_catalog.quote_ident (v.srvname) FROM pg_catalog.pg_foreign_table f
          JOIN pg_catalog.pg_foreign_server v ON v.oid = f.ftserver
         WHERE f.ftrelid = c.oid),
       (SELECT string_agg (pg_catalog.quote_ident (split_part (o, '=', 1)) || ' '
                           || pg_catalog.quote_literal (substr (o, strpos (o, '=') + 1)), ', ')
          FROM pg_catalog.pg_foreign_table f, unnest (f.ftoptions) o
         WHERE f.ftrelid = c.oid),
       pg_catalog.array_to_string (c.reloptions, ', '),
       pg_catalog.quote_literal (pg_catalog.obj_description (c.oid, 'pg_class')),
       (SELECT string_agg (i.inhparent::pg_catalog.regclass::text, ', ' ORDER BY i.inhseqno)
          FROM pg_catalog.pg_inherits i WHERE i.inhrelid = c.oid),
       c.relispopulated
  FROM pg_catalog.pg_class c
 WHERE c.oid = {{oid}};
SELECT pg_catalog.quote_ident (a.attname), pg_catalog.format_type (a.atttypid, a.atttypmod),
       a.attnotnull, a.attidentity, a.attgenerated, pg_catalog.pg_get_expr (d.adbin, d.adrelid),
       CASE WHEN a.attcollation <> t.typcollation
            THEN (SELECT pg_catalog.quote_ident (cn.nspname) || '.'
                         || pg_catalog.quote_ident (co.collname)
                    FROM pg_catalog.pg_collation co
                    JOIN pg_catalog.pg_namespace cn ON cn.oid = co.collnamespace
                   WHERE co.oid = a.attcollation) END,
       a.attislocal, pg_catalog.quote_literal (pg_catalog.col_description (a.attrelid, a.attnum)),
       pg_catalog.format_type (q.seqtypid, NULL), q.seqstart, q.seqincrement, q.seqmin, q.seqmax,
       q.seqcache, q.seqcycle,
       (SELECT pg_catalog.pg_get_expr (pd.adbin, pd.adrelid)
          FROM pg_catalog.pg_inherits h
          JOIN pg_catalog.pg_attribute pa ON pa.attrelid = h.inhparent AND pa.attname = a.attname
          JOIN pg_catalog.pg_attrdef pd ON pd.adrelid = pa.attrelid AND pd.adnum = pa.attnum
         WHERE h.inhrelid = a.attrelid
         ORDER BY h.inhseqno LIMIT 1),
       (SELECT bool_or (pa.attnotnull)
          FROM pg_catalog.pg_inherits h
          JOIN pg_catalog.pg_attribute pa ON pa.attrelid = h.inhparent AND pa.attname = a.attname
         WHERE h.inhrelid = a.attrelid),
       (SELECT string_agg (pg_catalog.quote_ident (split_part (o, '=', 1)) || ' '
                           || pg_catalog.quote_literal (substr (o, strpos (o, '=') + 1)), ', ')
          FROM unnest (a.attfdwoptions) o),
       q.seqrelid::pg_catalog.regclass::text,
       (SELECT pg_catalog.quote_ident (rn.nspname) || '.'
               || pg_catalog.quote_ident (rc.relname || '_' || a.attname || '_seq')
          FROM pg_catalog.pg_class rc
          JOIN pg_catalog.pg_namespace rn ON rn.oid = rc.relnamespace
         WHERE rc.oid = a.attrelid),
       (SELECT count (DISTINCT pg_catalog.pg_get_expr (pd.adbin, pd.adrelid))
          FROM pg_catalog.pg_inherits h
          JOIN pg_catalog.pg_attribute pa ON pa.attrelid = h.inhparent AND pa.attname = a.attname
          JOIN pg_catalog.pg_attrdef pd ON pd.adrelid = pa.attrelid AND pd.adnum = pa.attnum
         WHERE h.inhrelid = a.attrelid)
  FROM pg_catalog.pg_attribute a
  JOIN pg_catalog.pg_type t ON t.oid = a.atttypid
  LEFT JOIN pg_catalog.pg_attrdef d ON d.adrelid = a.attrelid AND d.adnum = a.attnum
  LEFT JOIN pg_catalog.pg_depend sd
    ON sd.classid = 'pg_catalog.pg_class'::pg_catalog.regclass
   AND sd.refclassid = 'pg_catalog.pg_class'::pg_catalog.regclass
   AND sd.refobjid = a.attrelid AND sd.refobjsubid = a.attnum AND sd.deptype = 'i'
  LEFT JOIN pg_catalog.pg_sequence q ON q.seqrelid = sd.objid
 WHERE a.attrelid = {{oid}} AND a.attnum > 0 AND NOT a.attisdropped
 ORDER BY a.attnum;
SELECT pg_catalog.quote_ident (o.conname), pg_catalog.pg_get_constraintdef (o.oid, true),
       o.convalidated,
       (SELECT pg_catalog.array_to_string (ic.reloptions, ', ') FROM pg_catalog.pg_class ic
         WHERE ic.oid = o.conindid AND o.contype IN ('p', 'u'))
  FROM pg_catalog.pg_constraint o
 WHERE o.conrelid = {{oid}} AND o.conislocal AND o.contype IN ('p', 'u', 'f', 'c', 'x')
 ORDER BY o.contype <> 'p', o.contype <> 'u', o.contype <> 'c', o.conname;
SELECT pg_catalog.pg_get_indexdef (i.indexrelid)
  FROM pg_catalog.pg_index i
 WHERE i.indrelid = {{oid}}
   AND NOT EXISTS (SELECT 1 FROM pg_catalog.pg_constraint o
                    WHERE o.conindid = i.indexrelid AND o.conrelid = i.indrelid)
   AND NOT EXISTS (SELECT 1 FROM pg_catalog.pg_inherits h WHERE h.inhrelid = i.indexrelid)
 ORDER BY 1;
-- its triggers, their mode, and the mode of the trigger of the table of a partition a trigger is
-- the clone of: PARTITION OF makes the clone, its mode may be another
SELECT pg_catalog.pg_get_triggerdef (t.oid, true), pg_catalog.quote_ident (t.tgname),
       t.tgenabled, p.tgenabled
  FROM pg_catalog.pg_trigger t
  LEFT JOIN pg_catalog.pg_depend d
    ON d.classid = 'pg_catalog.pg_trigger'::pg_catalog.regclass AND d.objid = t.oid
   AND d.refclassid = 'pg_catalog.pg_trigger'::pg_catalog.regclass AND d.deptype = 'P'
  LEFT JOIN pg_catalog.pg_trigger p ON p.oid = d.refobjid
 WHERE t.tgrelid = {{oid}} AND NOT t.tgisinternal
 ORDER BY t.tgname;
SELECT pg_catalog.format_type (q.seqtypid, NULL), q.seqstart, q.seqincrement, q.seqmin,
       q.seqmax, q.seqcache, q.seqcycle, d.deptype,
       d.refobjid::pg_catalog.regclass::text || '.' || pg_catalog.quote_ident (a.attname),
       (SELECT sc.relpersistence FROM pg_catalog.pg_class sc WHERE sc.oid = q.seqrelid)
  FROM pg_catalog.pg_sequence q
  LEFT JOIN pg_catalog.pg_depend d
    ON d.classid = 'pg_catalog.pg_class'::pg_catalog.regclass AND d.objid = q.seqrelid
   AND d.refclassid = 'pg_catalog.pg_class'::pg_catalog.regclass AND d.deptype IN ('a', 'i')
  LEFT JOIN pg_catalog.pg_attribute a ON a.attrelid = d.refobjid AND a.attnum = d.refobjsubid
 WHERE q.seqrelid = {{oid}};
SELECT q.seqrelid::pg_catalog.regclass::text, pg_catalog.format_type (q.seqtypid, NULL),
       q.seqstart, q.seqincrement, q.seqmin, q.seqmax, q.seqcache, q.seqcycle,
       pg_catalog.quote_ident (a.attname),
       (SELECT sc.relpersistence FROM pg_catalog.pg_class sc WHERE sc.oid = q.seqrelid)
  FROM pg_catalog.pg_depend d
  JOIN pg_catalog.pg_sequence q ON q.seqrelid = d.objid
  JOIN pg_catalog.pg_attribute a ON a.attrelid = d.refobjid AND a.attnum = d.refobjsubid
 WHERE d.classid = 'pg_catalog.pg_class'::pg_catalog.regclass
   AND d.refclassid = 'pg_catalog.pg_class'::pg_catalog.regclass
   AND d.refobjid = {{oid}} AND d.deptype = 'a'
 ORDER BY a.attnum;
COMMIT
