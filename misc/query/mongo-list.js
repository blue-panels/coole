// The databases and their collections, printed as Extended JSON after "\x1e coole-schema ":
// those of the system left out, but that of the connection; listDatabases not allowed, that alone
{{> mongo-connect}}
{
    const __cur = db.getName();
    let __names = [];
    try {
        __names = db.adminCommand({ listDatabases: 1, nameOnly: true }).databases.map((d) => d.name);
    } catch (e) {}
    if (!__names.includes(__cur)) __names.push(__cur);
    const __out = { current: __cur, dbs: [] };
    for (const __n of __names.sort()) {
        if (['admin', 'local', 'config'].includes(__n) && __n !== __cur) continue;
        const __d = db.getSiblingDB(__n);
        const __c = [];
        let __infos = [];
        try { __infos = __d.getCollectionInfos(); } catch (e) {}
        for (const __i of __infos) {
            if (__i.name.startsWith('system.')) continue;
            let __count = null;
            if (__i.type === 'collection') {
                try { __count = __d.getCollection(__i.name).estimatedDocumentCount(); } catch (e) {}
            }
            __c.push({ name: __i.name, type: __i.type || 'collection', count: __count,
                       viewOn: (__i.options && __i.options.viewOn) || null });
        }
        __c.sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
        __out.dbs.push({ name: __n, colls: __c });
    }
    print('\x1e' + 'coole-schema ' + EJSON.stringify(__out));
}
