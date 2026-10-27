// __t (v): the type of a value, as mongosh names it, never the value
const __t = (v) => v === null ? 'null' : v === undefined ? 'undefined'
    : v instanceof Date ? 'Date' : Array.isArray(v) ? 'Array'
    : (v && v._bsontype) ? String(v._bsontype) : typeof v === 'number' ? 'Number'
    : typeof v === 'string' ? 'String' : typeof v === 'boolean' ? 'Boolean'
    : typeof v === 'object' ? 'Object' : typeof v;
