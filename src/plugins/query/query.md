# Query <!-- help:notitle -->

**Query plugin of the editor**

Runs the queries of a file in a database and shows its answer at the bottom
of the screen: what the database said, and the documents or the rows in
columns. MongoDB is run by *mongosh*, its shell; PostgreSQL by *libpq*, its
client library, as the part **PostgreSQL** at the end tells. It works in
*coole -Q* alone. *MC_QUERY_KIND*, *mongo* or *postgres*, tells which;
without it *MC_MONGO_URI* means MongoDB, and a file *.sql* PostgreSQL.
Without a file named, the file of queries is that of the database,
*~/.local/share/coole/queries/* with the name of *MC_MONGO_DB* and *.js*:
the queries of a database stay for the next time.

**What it needs**

*mongosh*
: The shell of MongoDB, which runs the queries: the package mongodb-mongosh
of the repository of MongoDB, or its archive.

*mongolsp*
: The language server of the queries, for their completion; without it the
queries run all the same. The package *mongolsp* of its releases,
<https://github.com/blue-panels/mongolsp/releases>, put in with apt, or
*install.sh* of its archive, with Node.js 18 or later. The first lines of
Output tell whether both are found.

**What it connects to**

mongosh is told by the environment of coole, which it inherits; coole reads
none of it but the name of the server for the header of the output:

*MC_MONGO_URI*
: The connection string, *mongodb://user:password@host:port/?options*.
It is not given on the command line, where any user could read it.

*MC_MONGO_DB*
: The database; without it, the one of the connection string.

*MC_MONGOSH*
: The program; without it, *mongosh* of PATH.

A line *use("name")* runs the queries after it in that database, as
*db = db.getSiblingDB("name")*, a part between lines *// ---* or a selection
run alone too, the header of Output telling it: mongosh, with no database of its own, takes
no *use()*, and the language server of the queries, *mongolsp* of lsp.json,
knows the database of the collections it completes by it. A file without one
gets *use("name")* of *MC_MONGO_DB*, or of the database of the connection
string, at its top as -Q starts; the file is changed, to be saved or not, and
Output says so. With the server in PATH, Ctrl-Space and the dot after *db*
complete the collections, their fields, the methods, the stages and the
operators.

The query itself goes to mongosh on its command line, which any user can
read while it runs: a secret a query needs, a token or a password, is better
taken from the environment, *process.env.NAME*, which mongosh inherits too.

**The keys**

These are the default keys; they can be changed in **Options → Key
bindings → Editor → Query**, or in the *[query]* section of the keymap.

**F5**
: Run the selection, else the part of the file the cursor is in between
lines *// ---*, else the whole file. A script is run as a whole, its lines
using what the lines before them made, empty lines between them or not;
a file of many queries has a line *// ---* before each, words after the
dashes naming it if need be. The separator heads the part after it.

**Ctrl-F5**
: Run the whole file, even with a selection or separators.

```
// --- the last orders
// @table
db.orders.find({ status: 1 })
  .sort({ ts: -1 })
  .limit(20)

// --- how many
db.orders.countDocuments({ status: 1 })
```

**F5** while a query runs asks whether to stop it.

A line *// @limit 20* in a query limits its documents, to 100 without a
number: a chain of *find()* gets *.limit(20)* on a line after it, unless it
has a *.limit()* of its own, and *aggregate([...])* a stage *{ $limit: 20 }*
at the end of its pipeline, unless it has a *$limit* of its own, or a *$out*
or a *$merge*, after which no stage may come. The header of Output tells the limit put, or
that the query has nothing to limit. The file is left as it is.

**The tabs**

*Output*
: What mongosh printed, as it prints it: *ObjectId('...')*, *ISODate('...')*,
a cursor to its 1000th document. Its first line tells the lines of the
query, the time it took, the server and the exit code; an error is red. A
syntax error marks its line in the file, and the cursor goes there.

*Table*
: A table of each array or cursor *printjson()* or *print()* printed, in
their order, numbered, whether the query has a line *// @table* or not;
Output shows what they printed as it is. A cursor is read into the array of
its documents. A table is of Markdown, its columns lined up: a column for
each key of the documents, *_id* first; a document without that key has the
cell empty; an object or an array inside is shown as mongosh shows it,
*{ClientUuid: "hashed"}*, *[1, 2, 3]*; a *|* of a value is *\|*; a value longer than 40 columns is cut; the first 100 documents of
an array are shown, all of them counted. The tab comes in front when there
are documents. A number or a single document stays in Output.

A query with a line *// @table* in it has a table of the value it ends with
too, the last one: it is run with *--json=relaxed*, and Output shows that
value as JSON. A query that ends with a chain of *find()* or *aggregate()*,
its *.sort()*, *.limit()* and the like, has *.toArray()* put on a line after
it.

*History*
: The versions of the file of queries in front, its name in the title of
the tab, the newest first, a line each: the time of the version, how often
it was run and how its last run ended, the lines it put and took away since
the version before, the first line of the query it ran last. Each run keeps
the file, saved or not, as a version, unless it is as at the run before:
another run of it counts on its line. They are in the data of the user,
*~/.local/share/coole/queries/.history/*, a directory for each file, a
version named by its time, *20261024-173750.js*; the newest 200 stay. The
line of the cursor chooses a version:

**Enter**, a double click
: The file of queries as it was at that run: its text is that of the version,
in its own window, one change **Undo** takes back, the cursor on the query.
What it had, unless it is a version already, as when one goes from version
to version, is kept first as the one line *current*, in place of the one
before that was never run: **Enter** on it brings back the work of before. The versions are a store: they are
not opened, and a file of the store run gets no versions.

**F3**
: What has changed in the file since that version, *diff -u*, in Output.

**F5**
: Run the query the version ran last again as it was, the file left as it
is; its errors are not marked in the file.

Text marked in History is copied with **Ctrl-Ins**: Enter and F3 are those
of the list. Another file in front shows its own History.

**Ctrl-Alt-PgDn** and **Ctrl-Alt-PgUp** go from tab to tab.

**Ctrl-Alt-S**
: The window Schema at the right, QuerySchema, as for PostgreSQL below: the
databases, that of the connection first and unfolded, *admin*, *local* and
*config* left out unless it is one of them; their Collections, with the
documents the server counts, *~1200*, Views, *on orders*, and Time series.
A collection unfolded is read then: its Fields, of a sample of 100 of its
documents, never a value, with their types and, when not all of them have
the field, how many do, *50%*; its Indexes and their keys, *unique*,
*sparse*, *ttl*; its Validator, the Pipeline of a view. Enter folds and
unfolds, F5 puts the first 100 documents of the collection in Table, F3
puts the query of them in the file of queries, *// @table* and
*db.getSiblingDB("shop").getCollection("orders").find({}, { _id: 1, total:
1 }).limit(100)*, of its database whatever the *use()* of the file, of the
field of the line alone when it is one, Insert puts *db.orders*, a field, the key of an
index. F4 puts the DDL of the collection there, as F3 does its query: its
*drop()*, commented, its *createCollection()* with its options, the
validator, *capped*, the time series, or the *createView()* of a view, and a
*createIndex()* for each index but *_id_*; of a database, its
*dropDatabase()*, commented, and its *use()*. A value with a type of BSON
in it, a date, a Long, is put in *EJSON.deserialize()*; indexes that could
not be read are told in a comment. It is the definition, not a backup: the
users, the roles and the documents are not in it; a document whose key is
one of Extended JSON, *{ $date: "..." }*, is read back as that type, and a
Double with no fraction as a number, the types mongosh gives them already
lost. mongosh reads it,
a run of its own each time: a query that makes or drops a collection, a
view or an index, or inserts, reads it again.

**A query written by AI**

**F6**
: The query of the request written in the file, by AI: the request is the
paragraph of the cursor, its lines between empty lines, or the selection,
in the words of the user, *orders of the last week by user*. The program
of AI writes the query, which is put under the request, the request made a
comment, *//* or *--*: one change, which Undo takes back. The query is
not run: F5 runs it. **F6** on a request that is a comment over a query
writes it again, in the place of that query. A paragraph with a query of
the user and words, *count the average of ttl* under a *find()*, is all of
it the request: AI writes the query the words ask for from that one, the
words alone made a comment, and the query of AI goes after the paragraph,
an empty line between, the query of the user left as it is. *// @table*
and *// @limit* are of the query, never words of the request. An answer
with no query, a question of AI, leaves the file as it is and is told in
Output. **F6** while AI writes asks whether to stop it. Output tells the
time it took, and what AI said beside the query.

AI is told the names of the database, never a row: of MongoDB the
collections, the fields of a sample of the documents of each with their
types, their indexes, and the views, in the database of the *use()* above
the request; of PostgreSQL the relations of
its schemas, their columns with their types and their keys, those of
*MC_PG_SCHEMA* first; with *MC_QUERY_READ_ONLY=1*, that it is to change
nothing.

*MC_QUERY_AI*
: The program of AI, else the key *ai* of the section *[Query]* of
*~/.config/coole/ini*, the environment choosing another than the
configuration for a run:

```
[Query]
ai=codex
```

*claude*, without either, is run as *claude -p --tools ""
--no-session-persistence*, *codex* as *codex exec --skip-git-repo-check
--ephemeral --sandbox read-only --color never -*: the request on stdin, no
tool of theirs run. Another value is a command line run as it is, which
reads the request on stdin and prints the query, in a code block of
Markdown or alone. It runs in the directory of the temporary files, without
*PGPASSWORD* and *MC_MONGO_URI* in its environment. The first lines of
Output tell whether it is found.

The scripts the plugin runs to read the schema and what AI is told, and
the one mongosh runs before each query, are files of
*/usr/share/coole/query*: *mongo-list.js*, *mongo-details.js*,
*mongo-ai.js*, *mongo-ddl.js*, *mongo-prefix.js*, *pg-list.sql*,
*pg-details.sql*, *pg-ai.sql*, *pg-ddl-relation.sql*,
*pg-ddl-function.sql*. One of the same name in *~/.config/coole/query*, or
in */etc/coole/query*, is run in its place. In them *{{> name}}* is the
part *name* of the same extension, read from the same directories,
*{{name}}* a value the plugin puts there, the database, the collection,
the oid, and a line that is a comment alone is left out. When one is not
there, Output or Schema tell it.

A node of Schema has a mark of what it is before its name when the skin
gives one, in its section *[query]*; none gives one by default:

```
[query]
    schema-char = ◫
    table-char = ▦
    column-char = ·
```

The keys are *schema-char*, *database-char*, *table-char*,
*partitioned-char*, *view-char*, *matview-char*, *foreign-char*,
*sequence-char*, *function-char*, *procedure-char*, *aggregate-char*,
*column-char*, *key-char*, *fkey-char*, *index-char*, *reference-char*,
*trigger-char*, *collection-char*, *timeseries-char*, *field-char*,
*validator-char*, *pipeline-char*.

**PostgreSQL**

*coole -Q* with *MC_QUERY_KIND=postgres*, or a file *.sql*, runs the queries
in PostgreSQL. It keeps one connection while it runs: a transaction, the
temporary tables and what *SET* sets stay from one F5 to the next.

*libpq5*
: The client library of PostgreSQL. coole loads it as -Q starts, and needs
it for PostgreSQL alone; libpq 17 or later gives the rows by chunks and a
cancel of its own, an older one gives them one by one.

*postgres-language-server*
: The language server of SQL, for the completion of the tables and the
columns, hover and the errors as they are typed; without it the queries run
all the same. Its releases are at
<https://github.com/supabase-community/postgres-language-server/releases>.
The first lines of Output tell whether both are found.

libpq takes what to connect to from the environment, as *psql* does:
*PGHOST*, *PGPORT*, *PGUSER*, *PGDATABASE*, *PGSERVICE* and
*~/.pg_service.conf*, *PGSSLMODE*. The password is better in *~/.pgpass*,
which libpq and the language server read, than in *PGPASSWORD*. coole adds
to the connection its encoding, UTF8, and with *MC_QUERY_READ_ONLY=1*
transactions that write nothing, *[RO]* in the header: a guard against a
slip, not a right, a *SET* undoing it; what may be written is the role's.

Without a file named, the file of queries is
*~/.local/share/coole/queries/postgres/*, the name of the connection,
*MC_PG_CONNECTION* or *PGSERVICE*, and that of the database with *.sql*.

With *MC_PG_RELATION*, a table or a view, and *MC_PG_SCHEMA* its schema,
as the panel of PostgreSQL starts coole, a *SELECT* of its first 100 rows
runs once connected, the file left as it is; a file with nothing in it gets
that query, changed, not saved.

**F5**
: Run the selection, else the whole file, as it is, in one query: its
commands are one transaction unless it has its own *BEGIN*, an error
undoing those before it and the rest not run. *CREATE DATABASE*, *VACUUM*
and the like take no other command with them: select them alone. The
commands of psql, *\d*, *\copy*, are none of PostgreSQL.

**Ctrl-F5**
: Run the whole file, even with a selection.

**F5** while a query runs asks whether to stop it: the server is asked to
cancel it, and Output tells how it ended. A connection that does not come
is stopped so too.

*Output*
: Its first line tells the lines of the query, the time it took, the
server, how it ended and the transaction: *[TX]* while one is open,
*[TX FAILED: ROLLBACK required]* when a command of it failed, the server
taking no other query until *ROLLBACK*; the title of the tab tells it too.
Then what each command did, *SELECT 12*, *UPDATE 4*, the notices of the
server among them, a notice at times before the command it follows; an
error with its code, *ERROR [23505]*, its *DETAIL* and *HINT*, its line marked
in the file and the cursor there. *COPY ... TO STDOUT* prints its rows here;
*COPY ... FROM STDIN* is ended, coole having nothing to send.

*Table*
: A table of the rows of each command that has some, *SELECT* and
*... RETURNING*, numbered: the first 100 rows, all of them counted, the
memory the same for millions of them. *NULL* is told, a *bytea* by its
length and its start, a long value by its start. Rows of a command that
failed after them are told as incomplete.

*History*
: As for MongoDB; a run tells how it ended, *SELECT 12* or *ERROR 42P01*,
and its versions are *.sql*.

**QueryRollback**
: Run *ROLLBACK*. It has no key of its own: one is given it in
*~/.config/coole/keymap.ini*, section *[query]*.

**Ctrl-Alt-S**
: The window Schema at the right, QuerySchema: shown when it is not, given
the focus when it has it not, hidden when it has. It is shown once
connected: the schemas of the database, that of *MC_PG_SCHEMA* first and
unfolded, else *public*, those of the system left out. A schema has its
groups, those with something in them: Tables, Views, Materialized views,
Foreign tables, Sequences, Functions, those of extensions left out. A table
shows the rows the server reckons, *~50k*; a partitioned table its
partitions under it, in its section Partitions.

A relation unfolded has its sections, read then: Columns, their types,
*not null*, the default, *identity*, *generated*; Keys, the primary key and
the unique ones; Foreign keys; Indexes; References, the foreign keys of the
other tables to it; Triggers. A table has all of them, the empty ones too,
a view and the like those with something.

In Schema, a tree; the button bar shows F3, F4 and F5:

**Up**, **Down**, **PgUp**, **PgDn**, **Home**, **End**
: Move over the nodes.

**Enter**
: Fold or unfold the node: a schema, a group, a relation, a section. A
double click does so too; **Right** unfolds, **Left** folds or goes to the
parent. A relation unfolded and not read yet shows *...* until its sections
come.

**F10**, **Esc**
: Close the window; Ctrl-Alt-S makes it again.

**F5**
: The first 100 rows of the relation of the node, or of that it is of, in
Table: a query of no file, QuerySchemaRows, its text at the top of Table and
in the head of Output.

**F3**
: The query of the rows of the relation put in the file of queries, its
columns one a line, of the column of the node alone when it is one, read
first when they are not yet: *SELECT id, name FROM public.users LIMIT 100;*,
in the file F3 was pressed in, on the line of the cursor when it is empty, a
paragraph of its own, else after its paragraph; the cursor goes there, F5
runs it. A query that waits for its columns is put no more when Schema is
read again or another F3 comes; Output tells it, and when the columns could
not be read. QuerySchemaQuery.

**F4**
: The DDL of the relation of the node, of the function, of the schema, put
in the file of queries as F3 puts its query, QuerySchemaDdl: its *DROP*,
commented, then its *CREATE*. A table has its columns with their types,
defaults, identity, generated and *NOT NULL*, its constraints, *PARTITION
BY* or *PARTITION OF*, then its indexes that are of no constraint, its
triggers and its comments; a view its query, a foreign table its server and
options, a sequence its numbers, commented when it is that of an identity
column, a function the definition PostgreSQL gives. The names in it have
their schema, *public.users*, whatever the search path. What CREATE would
lose follows it: *NOT VALID* constraints, the defaults of the columns of a
view, those a table or a partition gives what it inherits, the mode of a
trigger. It is the definition, not *pg_dump*: the owner, the grants, row
level security and its policies, the tablespace, the rows and the value of
a sequence are not in it.

**Insert**
: Put what the node names at the cursor of the file of queries, as SQL
takes it, QuerySchemaInsert: *sales."Orders"* for a relation, the name of a
schema, a column, an index, a trigger; the columns of a key, all those of
the node Columns; the table of a reference; *sales.total(* for a function.

**QuerySchemaRefresh**
: Read the schemas again, with no key of its own, what is unfolded and the
node of the cursor staying so. They are read again by themselves after a query whose *CREATE*,
*ALTER* or *DROP* is committed.

Schema reads the catalog by a connection of its own, of the same
environment, read-only as the other is: a query running or a transaction
that failed leaves it read all the same, and it sees what is committed
alone.

A connection lost while a query runs is told so, with the transaction,
the temporary tables and *SET* of the session gone with it; F5 connects
again and tells it once more.

The language server has a connection of its own: it knows the tables of
the database, not the temporary tables of the session of F5 nor a schema a
*SET search_path* put in front; *MC_PG_SCHEMA* is the schema it knows first,
else *public*. It runs no statement: queries run by F5 alone.
