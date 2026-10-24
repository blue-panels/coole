# Query <!-- help:notitle -->

**Query plugin of the editor**

Runs the queries of a file with *mongosh*, the shell of MongoDB, and shows
its answer at the bottom of the screen: what mongosh printed, and the
documents in columns. It works in *coole -Q* alone. Without a file named,
the file of queries is that of the database, *~/.local/share/coole/queries/*
with the name of *MC_MONGO_DB* and *.js*: the queries of a database stay
for the next time.

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
