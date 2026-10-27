// What AI is told of the database: its collections, the fields of a sample of each with their
// types, never a value, their indexes; its views.
// {{use}}: "db = db.getSiblingDB(...);" of the use () of the request, or nothing;
// {{max}}: the collections told at most
{{> mongo-connect}}
{{use}}
{
    {{> mongo-type}}
    print('The database "' + db.getName() + '" of MongoDB ' + db.version() + '.');
    print('Its collections, a line each: the name: the fields of a sample of its documents, '
          + 'with their types; its indexes.');
    let __infos = [];
    try { __infos = db.getCollectionInfos(); } catch (e) {}
    const __n = __infos.filter((i) => !i.name.startsWith('system.'))
        .sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
    const __size = 20;
    for (const __i of __n.slice(0, {{max}})) {
        if (__i.type === 'view') {
            print(__i.name + ': a view on ' + __i.options.viewOn + ', its pipeline '
                  + EJSON.stringify(__i.options.pipeline));
            continue;
        }
        const __c = db.getCollection(__i.name);
        {{> mongo-sample}}
        {{> mongo-fields}}
        let __ix = [];
        try {
            __ix = __c.getIndexes().filter((i) => i.name !== '_id_')
                .map((i) => EJSON.stringify(i.key) + (i.unique ? ' unique' : ''));
        } catch (e) {}
        print(__i.name + ': '
              + (__docs.length ? [...__f].map(([k, e]) => k + ' ' + e.types.join('|')).join(', ')
                               : 'no document')
              + (__ix.length ? '; indexes ' + __ix.join(', ') : ''));
    }
    if (__n.length > {{max}}) print('And ' + (__n.length - {{max}}) + ' collections more, not told.');
}
