-- the columns of the constraint o, as SQL takes them, in its order
(SELECT string_agg (pg_catalog.quote_ident (a.attname), ', '
                    ORDER BY pg_catalog.array_position (o.conkey, a.attnum))
   FROM pg_catalog.pg_attribute a
  WHERE a.attrelid = o.conrelid AND a.attnum = ANY (o.conkey))
