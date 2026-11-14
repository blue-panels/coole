// Run after mongo-prefix.js for QueryExport: the query of the user is given ".forEach(__bluerigRow)"
// after it, which prints each document of its cursor, or of its array, as relaxed Extended JSON
// on a line of its own after "\x1e bluerig-row " (QUERY_EXPORT_LINE); bluerig writes them to the
// file as they come, the other lines going to Output.  Relaxed JSON makes a Long a number of
// JavaScript, which has 53 bits: a bigger one stays {"$numberLong": "..."}, as it is
{
    const exact = (v) => {
        if (v === null || typeof v !== 'object' || v instanceof Date) return v;
        if (v._bsontype === 'Long')
            return Number.isSafeInteger(v.toNumber()) ? v : { $numberLong: v.toString() };
        if (v._bsontype !== undefined) return v;
        if (Array.isArray(v)) return v.map(exact);
        const o = {};
        for (const k of Object.keys(v)) o[k] = exact(v[k]);
        return o;
    };
    globalThis.__bluerigRow = (d) =>
        print('\x1e' + 'bluerig-row ' + EJSON.stringify(exact(d), { relaxed: true }));
}
