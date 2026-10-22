# Query <!-- help:notitle -->

**Query plugin of the editor**

Runs the queries of a file with *mongosh*, the shell of MongoDB, and shows
its answer at the bottom of the screen: what mongosh printed, and the
documents in columns. It works in *coole -Q* alone. Without a file named,
the file of queries is that of the database, *~/.local/share/coole/queries/*
with the name of *MC_MONGO_DB* and *.js*: the queries of a database stay
for the next time.

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
knows the database of the collections it completes by it. With the server in
PATH, Ctrl-Space and the dot after *db* complete the collections, their
fields, the methods, the stages and the operators.

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
: The documents of a query with the line *// @table* in it, as a table of
Markdown, its columns lined up: a column for each key of them, *_id*
first; a document without that key has the cell empty; an object inside
is *{2 keys}*, an array *[3]*; a *|* of a value is *\|*; a value longer
than 40 columns is cut. The table comes in front when there are documents.
Such a query is run once, with *--json=relaxed*: Output shows its answer as
JSON, what *print()* printed before it too, and the table is made of the
value it ends with. A query that ends with a chain of *find()* or
*aggregate()*, its *.sort()*, *.limit()* and the like, has *.toArray()* put
on a line after it.

*History*
: The queries run, the last one first: the time, how long it took, the exit
code, the line of the file, the first line of the query. It is kept for the
file in the data of the user, *~/.local/share/coole/query*, the connection
string left out. A query of it is there as it was run, though the file has
changed since; the line of the cursor chooses one:

**Enter**
: Put the query into the file as a part of its own, a line *// ---* before
it, after the part the cursor of the file is in; into that part when it is
empty. The cursor goes to it, to be changed and run with **F5**.

**F3**
: Show all the lines of the query under its line, or no more.

**F5**
: Run the query again as it was, the file left as it is; its errors are not
marked in the file.

Text marked in History is copied with **Ctrl-Ins**: Enter and F3 are those
of the list. A query run from another file goes into the History of that file.

**Ctrl-Alt-PgDn** and **Ctrl-Alt-PgUp** go from tab to tab.
