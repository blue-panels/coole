// db of the connection, as the queries have it
db = connect(process.env.MC_MONGO_URI);
if (process.env.MC_MONGO_DB) db = db.getSiblingDB(process.env.MC_MONGO_DB);
