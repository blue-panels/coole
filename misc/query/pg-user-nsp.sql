-- the schemas of the user: those of the system left out
n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
