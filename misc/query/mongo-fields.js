// __f: the fields of __docs, in how many of them and of which types
const __f = new Map();
for (const __doc of __docs)
    for (const __k of Object.keys(__doc)) {
        const __e = __f.get(__k) || { n: 0, types: [] };
        __e.n++;
        const __ty = __t(__doc[__k]);
        if (!__e.types.includes(__ty)) __e.types.push(__ty);
        __f.set(__k, __e);
    }
