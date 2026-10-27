// __docs: the documents of a sample of __size of the collection __c
let __docs = [];
try { __docs = __c.aggregate([{ $sample: { size: __size } }]).toArray(); }
catch (e) { try { __docs = __c.find().limit(__size).toArray(); } catch (e2) {} }
