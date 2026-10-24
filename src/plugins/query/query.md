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
: In the menu Query, with no key of its own: run *ROLLBACK*.

A connection lost while a query runs is told so, with the transaction,
the temporary tables and *SET* of the session gone with it; F5 connects
again and tells it once more.

The language server has a connection of its own: it knows the tables of
the database, not the temporary tables of the session of F5 nor a schema a
*SET search_path* put in front; *MC_PG_SCHEMA* is the schema it knows first,
else *public*. It runs no statement: queries run by F5 alone.
