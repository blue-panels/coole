// F4: the DDL of a collection or a view, as the queries write it, printed as Extended JSON after
// "\x1e coole-schema ": its drop, commented, its create with its options, its indexes.  A value
// with a type of BSON in it, a date, an ObjectId, a Long, is put in EJSON.deserialize () as
// canonical Extended JSON, so that it is that value again when it runs: not an object of "$date",
// nor a Long rounded to a number.
// {{db}}, {{collection}}: its database and its name, strings of JavaScript
{{> mongo-connect}}
{
    const __d = db.getSiblingDB({{db}});
    const __n = {{collection}};
    let __i = null;
    try { __i = __d.getCollectionInfos({ name: __n })[0]; } catch (e) {}
    // __v (v, space): v as the queries write it: plain JSON when it has no type of BSON in it, else
    // the canonical Extended JSON in EJSON.deserialize (), which keeps a Long, a Decimal, a Double;
    // a number JSON has not, NaN, Infinity, is one of those too.  JSON with a key "__proto__" is
    // read by JSON.parse (): a literal of JavaScript takes it for the prototype, not a field
    const __e = /"\$(date|oid|numberLong|numberDecimal|binary|uuid|regularExpression|timestamp|minKey|maxKey|symbol|code|dbPointer|undefined)"/;
    const __v = (v, space) => {
        const __c = EJSON.stringify(v, null, space, { relaxed: false });
        const __x = EJSON.stringify(v, null, space, { relaxed: true });
        const __typed = __e.test(__c) || /"\$number(Double|Int)"/.test(__x);
        const __t = __typed ? __c : __x;
        const __s = /"__proto__"\s*:/.test(__t) ? 'JSON.parse(' + JSON.stringify(__t) + ')' : __t;
        return __typed ? 'EJSON.deserialize(' + __s + ', { relaxed: false })' : __s;
    };
    const __j = (v) => __v(v, 2);
    const __l = (v) => __v(v, 0);
    const __r = 'db.getSiblingDB(' + JSON.stringify(__d.getName()) + ')';
    const __c = __r + '.getCollection(' + JSON.stringify(__n) + ')';
    const __out = {};
    if (!__i)
        __out.error = 'no such collection';
    else {
        const __o = Object.assign({}, __i.options || {});
        let __s = '// ' + __c + '.drop()\n';
        if (__i.type === 'view') {
            const __on = __o.viewOn;
            const __p = __o.pipeline || [];
            delete __o.viewOn;
            delete __o.pipeline;
            __s += __r + '.createView(' + JSON.stringify(__n) + ', ' + JSON.stringify(__on) + ', '
                + __j(__p) + (Object.keys(__o).length ? ', ' + __j(__o) : '') + ')\n';
        } else {
            __s += __r + '.createCollection(' + JSON.stringify(__n)
                + (Object.keys(__o).length ? ', ' + __j(__o) : '') + ')\n';
            let __x = [];
            // indexes that could not be read are told, not taken for none
            try { __x = __d.getCollection(__n).getIndexes(); }
            catch (e) { __s += '// the indexes could not be read: '
                             + String(e.message).replace(/[\r\n]+/g, ' ') + '\n'; }
            for (const __k of __x) {
                if (__k.name === '_id_') continue;
                const __a = Object.assign({}, __k);
                delete __a.v;
                delete __a.key;
                delete __a.ns;
                __s += __c + '.createIndex(' + __l(__k.key) + ', ' + __l(__a) + ')\n';
            }
        }
        __out.ddl = __s;
    }
    print('\x1e' + 'coole-schema ' + EJSON.stringify(__out));
}
