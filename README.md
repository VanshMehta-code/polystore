# PolyStore

A self-hosted, multi-model NoSQL database written in C++. Pick the data structure that fits your use case — key-value, document, graph, or time series — and run it on your own machine. No cloud, no subscription, no extra servers.

---

## Why PolyStore

Most databases make you pick one model and stick with it. PolyStore lets you spin up whichever engine fits the job, keep it in RAM while you need it, and unload it when you don't. Everything runs on a single binary.

---

## Engines

| Engine | Best for |
|---|---|
| `kv` | Caching, session data, fast lookups |
| `doc` | Flexible records, nested data, content |
| `graph` | Relationships, traversals, social networks |
| `ts` | Metrics, logs, sensor data, activity feeds |

---

## Getting started

**Requirements:** C++17, any modern compiler (g++ or clang++)

```bash
# clone and build
git clone https://github.com/yourname/polystore
cd polystore
mkdir build && cd build
g++ -std=c++17 -pthread -I ../include -o polystore ../src/main.cpp ../src/database.cpp

# start the server (port 7700)
./polystore --server

# or start the interactive CLI
./polystore
```

On first run it creates a default admin account:

```
username: sys
password: sys
```

Change it immediately after login.

---

## HTTP API

Every request needs Basic Auth. In Insomnia or curl, set username and password on every call.

### Databases

```
POST  /db/create        body: {"name":"mydb","engine":"kv"}
GET   /db/list
```

### Key-Value

```
POST    /kv/:db/set          body: {"key":"hello","value":"world"}
GET     /kv/:db/get/:key
DELETE  /kv/:db/remove/:key
GET     /kv/:db/keys
```

### Document

```
POST    /doc/:db/insert          body: {"name":"Alice","age":25}
GET     /doc/:db/get/:id
POST    /doc/:db/find            body: {"field":"name","value":"Alice"}
PUT     /doc/:db/update/:id      body: {"name":"Alice","age":26}
DELETE  /doc/:db/remove/:id
```

### Graph

```
POST    /graph/:db/node/add          body: {"label":"Alice"}
POST    /graph/:db/edge/add          body: {"src":0,"dst":1,"data":{}}
GET     /graph/:db/node/:id
GET     /graph/:db/bfs/:id/:depth
GET     /graph/:db/dfs/:id/:depth
DELETE  /graph/:db/node/:id
DELETE  /graph/:db/edge/:src/:dst
```

### Time Series

```
POST    /ts/:db/insert       body: {"key":"cpu","time":1700000000,"value":42.5}
GET     /ts/:db/latest/:key
POST    /ts/:db/range        body: {"key":"cpu","from":1700000000,"to":1700001000}
DELETE  /ts/:db/purge        body: {"key":"cpu","olderThan":1700000000}
```

### Users

```
POST  /auth/user/create    body: {"username":"bob","password":"secret","isAdmin":false}
POST  /auth/user/delete    body: {"username":"bob"}
POST  /auth/user/passwd    body: {"username":"bob","password":"newpass"}
POST  /auth/user/grant     body: {"username":"bob","db":"mydb","level":"read"}
GET   /auth/users
```

Permission levels: `read`, `write`, `admin`

---

## CLI

```bash
./polystore
```

```
db create <name> <engine>       create a database
db list                         list all databases
kv <db> set <key> <value>       set a key
kv <db> get <key>               get a value
kv <db> del <key>               delete a key
doc <db> insert <json>          insert a document
doc <db> get <id>               get document by id
doc <db> find <field> <value>   find documents
graph <db> addnode <json>       add a node
graph <db> addedge <src> <dst>  add an edge
graph <db> bfs <id> <depth>     BFS traversal
ts <db> insert <k> <t> <v>      insert a time series entry
ts <db> latest <key>            get latest entry
user create <u> <p> [admin]     create a user
user passwd <u> <p>             change password
user grant <u> <db> <level>     grant access
user list                       list all users
checkpoint                      force checkpoint all databases
help                            show commands
exit                            quit
```

---

## How data is stored

Each database gets its own `.bin` file on disk. Writes go to a WAL (write-ahead log) first, so nothing is lost if the process crashes. On a configurable interval the WAL is flushed to the bin file and truncated. When you open a database it loads into RAM. When you close it, it unloads. Other databases stay on disk untouched.

The auth file is stored as binary — not plain text, not JSON. Passwords are salted and hashed. Opening the file in a text editor gives you nothing useful.

---

## Security

- Every HTTP route requires valid credentials before doing anything
- Wrong password returns `401`, insufficient permission returns `403`
- Regular users only see databases they have been explicitly granted access to
- The `sys` account can be renamed and its password changed but never deleted
- Admins can create users and grant access — but only to databases they own

---

## Project layout

```
polystore/
├── include/
│   ├── database.h       — everything: types, engines, server, CLI
│   └── httplib.h        — embedded HTTP server
├── src/
│   ├── database.cpp     — full implementation
│   └── main.cpp         — entry point
├── data/                — bin and wal files live here at runtime
└── docs/
    └── DOCS.md          — full technical documentation
```

---

## Roadmap

- WideColumn engine — sparse rows, column families
- Custom engine profiles via JSON config
- Multi-language SDKs — Python, JavaScript, Rust
- Scaling and replication

---

## License

MIT
