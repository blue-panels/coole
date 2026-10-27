// Run before each query of the user, in an --eval of its own: the lines of a SyntaxError stay
// those of the query.  printjson () and print () of an array or a cursor also print a copy of it
// as Extended JSON on a line of its own, after "\x1e coole-table " (QUERY_TABLE_LINE), for the
// tab Table: what they show is as it was; a cursor is made the array of its documents first,
// which it is read as once
DBQuery.shellBatchSize = 1000;
{{> mongo-connect}}
{
    const __p = print, __pj = printjson;
    const __t = (v) => (v && !Array.isArray(v) && typeof v.toArray === 'function') ? v.toArray() : v;
    const __e = (v) => {
        if (Array.isArray(v)) __p('\x1e' + 'coole-table ' + EJSON.stringify(v, { relaxed: true }));
    };
    globalThis.printjson = (x, ...r) => { const v = __t(x); __pj(v, ...r); __e(v); };
    globalThis.print = (...a) => {
        if (a.length === 1) {
            const v = __t(a[0]);
            if (Array.isArray(v)) { __p(v); __e(v); return; }
        }
        __p(...a);
    };
}
