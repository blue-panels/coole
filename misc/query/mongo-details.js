// What a collection unfolded shows, printed as Extended JSON after "\x1e coole-schema ": the
// fields of a sample of its documents, its indexes, its validator, the pipeline of a view.
// {{db}}, {{collection}}: its database and its name, strings of JavaScript
{{> mongo-connect}}
{
    const __d = db.getSiblingDB({{db}});
    const __c = __d.getCollection({{collection}});
    const __out = { fields: [], indexes: [], sampled: 0, validator: null, pipeline: null };
    const __size = 100;
    {{> mongo-type}}
    {{> mongo-sample}}
    {{> mongo-fields}}
    __out.sampled = __docs.length;
    for (const [__k, __e] of __f) __out.fields.push({ name: __k, types: __e.types, n: __e.n });
    try {
        __out.indexes = __c.getIndexes().map((i) => ({
            name: i.name, key: EJSON.stringify(i.key), unique: !!i.unique, sparse: !!i.sparse,
            ttl: i.expireAfterSeconds === undefined ? null : i.expireAfterSeconds }));
    } catch (e) {}
    try {
        const __i = __d.getCollectionInfos({ name: __c.getName() })[0];
        if (__i && __i.options) {
            if (__i.options.validator) __out.validator = EJSON.stringify(__i.options.validator);
            if (__i.options.pipeline) __out.pipeline = EJSON.stringify(__i.options.pipeline);
        }
    } catch (e) {}
    print('\x1e' + 'coole-schema ' + EJSON.stringify(__out));
}
