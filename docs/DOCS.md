# PolyStore — Technical Documentation

---

## Table of contents

1. [Architecture](#architecture)
2. [Type system](#type-system)
3. [Storage engine](#storage-engine)
4. [Engines](#engines)
5. [Auth system](#auth-system)
6. [HTTP server](#http-server)
7. [CLI](#cli)
8. [Building](#building)
9. [Error reference](#error-reference)

---

## Architecture

PolyStore is structured in four layers, each sitting cleanly on top of the one below it.

```
CLI / HTTP server
      ↓
  Database class  (preset factory, engine lifecycle)
      ↓
  Engine classes  (KV, Document, Graph, TimeSeries)
      ↓
  Storage layer   (BinFile, WAL, Serializer)
```

Everything lives inside the `polystore` namespace. The header `database.h` declares all types, classes, and methods in this order: constants, types and aliases, base classes, engine classes, Database class, PolyServer class, Cli class.

The implementation in `database.cpp` follows the same order.

---

## Type system

All data in PolyStore is represented as a `Value`. A `Value` has a `Type` tag and the corresponding data field.

```
Type::Null     — no value
Type::Bool     — true or false
Type::Int64    — 64-bit signed integer
Type::Double   — 64-bit float
Type::String   — UTF-8 string
Type::Array    — ordered list of Values
Type::Object   — map of string keys to Values
```

This means any JSON-compatible structure can be stored natively. Nested objects, arrays of objects, mixed-type arrays — all supported out of the box.

### Binary wire format

On disk, a `Value` is serialized as a type byte followed by its data:

```
Null    →  [0x00]
Bool    →  [0x01][0x00 or 0x01]
Int64   →  [0x02][8 bytes little-endian]
Double  →  [0x03][8 bytes IEEE 754]
String  →  [0x04][4 bytes: length][N bytes: UTF-8 data]
Array   →  [0x05][4 bytes: count][each element serialized recursively]
Object  →  [0x06][4 bytes: field count][each field: key as String, then Value]
```

Reading is the mirror of writing — read one type byte, switch on it, read exactly what you know comes next. No pointers, no gaps, no ambiguity.

---

## Storage engine

### BinFile

Each database gets one `.bin` file per engine, named `<dbname>_<engine>.bin`. The file starts growing from byte 0. Every record is written as a 4-byte length prefix followed by the record bytes. To read record at offset N: seek to N, read 4 bytes for length, read that many bytes.

An in-memory index maps record IDs to byte offsets so any record is one seek, no scanning.

### WAL (Write-Ahead Log)

Every mutation (set, insert, update, delete) is appended to a `.wal` file before it touches in-memory state. Each WAL entry contains an opcode byte, the key as a length-prefixed string, and the value serialized as a `Value`.

On startup the WAL is replayed on top of the last good checkpoint to recover any mutations that happened after the last flush.

On checkpoint: the full in-memory state is written to the `.bin` file and the WAL is truncated to zero.

### Checkpoint frequency

A checkpoint is triggered automatically every `kCheckpointFreq` writes (default 100). You can also trigger one manually via the `checkpoint` CLI command or the HTTP route.

### Lazy RAM loading

Only the database you open is loaded into RAM. All others stay as files on disk. When you call `db.close()` or the server shuts down cleanly, each engine calls `unload()` which checkpoints and clears its in-memory structures.

---

## Engines

### KeyValue

In-memory structure: `std::unordered_map<std::string, Value>`

Operations:
- `set(key, value)` — O(1) write, appends to WAL
- `get(key)` — O(1) read
- `has(key)` — O(1) existence check
- `remove(key)` — O(1) delete, tombstone in WAL
- `keys()` — O(n) full key list

WAL opcodes: `0x01` set, `0x02` delete

### Document

In-memory structure: `std::unordered_map<uint64_t, Value>` plus an offset index `std::unordered_map<uint64_t, Offset>`

Documents are assigned auto-incrementing uint64 IDs starting from 0. The offset index maps each ID to its position in the bin file for fast single-document retrieval.

Operations:
- `insert(doc)` — assigns ID, stores in memory and WAL, returns ID
- `findById(id)` — O(1) hash lookup
- `findWhere(field, value)` — O(n) scan, checks each document's field
- `update(id, doc)` — replaces in memory, new offset in bin, WAL entry
- `remove(id)` — erases from memory and index, tombstone in WAL

WAL opcodes: `0x01` insert, `0x02` delete, `0x03` update

### Graph

In-memory structure: `std::unordered_map<NodeId, Node>` where each `Node` contains its data `Value` and an `EdgeList` (vector of `{NodeId, Value}` pairs).

Node IDs are auto-incrementing uint64. Edge data is any `Value` — use an object to store weight, label, timestamp, or anything else.

Operations:
- `addNode(data)` — assigns ID, stores node, returns ID
- `addEdge(src, dst, data)` — appends to src's edge list
- `getNode(id)` — O(1) hash lookup
- `getEdges(id)` — O(1) returns edge list for node
- `bfs(start, maxDepth)` — breadth-first traversal, returns visited node IDs in order
- `dfs(start, maxDepth)` — depth-first traversal, returns visited node IDs in order
- `removeNode(id)` — removes node and all edges pointing to it
- `removeEdge(src, dst)` — removes first edge from src to dst

Both BFS and DFS use a visited set (`std::unordered_map<NodeId, bool>`) to handle cycles correctly.

WAL opcodes: `0x01` add node, `0x02` remove node, `0x04` add edge, `0x05` remove edge

### TimeSeries

In-memory structure: `std::unordered_map<string, vector<TimeEntry>>` where each `TimeEntry` is a `{TimeStamp, Value}` pair. The vector is kept sorted by timestamp at all times using binary search insertion.

`TimeStamp` is `int64_t` — use Unix epoch seconds or milliseconds, your choice, as long as you're consistent within a key.

Operations:
- `insert(key, time, value)` — binary search insert to maintain sort order
- `range(key, from, to)` — returns all entries where `from <= time <= to`
- `latest(key)` — returns the last entry in the sorted vector, O(1)
- `last(key, n)` — returns last N entries
- `purge(key, olderThan)` — removes all entries with time < olderThan

WAL opcodes: `0x01` insert, `0x02` purge

---

## Auth system

### Users and roles

Every user has a username, a salted hash of their password, an `isAdmin` flag, and a list of per-database permissions.

The `sys` user is created on first run with password `sys`. It cannot be deleted. Its username and password can be changed.

Admin users can create other users, delete users (except sys), and grant or revoke permissions on any database.

Regular users can only access databases they have been explicitly granted access to, at the level they were granted.

Permission levels:
- `Read` — GET operations only
- `Write` — GET + POST + PUT + DELETE on data
- `Admin` — full access to that database, can grant others access to it

### Password storage

Passwords are never stored in plain text. On user creation:
1. A 16-byte random salt is generated
2. The password is hashed together with the salt using a djb2-derived 256-bit hash
3. Only the salt and hash are stored

On login the same process is repeated and the hashes are compared with `memcmp`.

### Auth file format

The auth file is stored at `data/.auth.bin` as a binary file. Each user record is written as:

```
[4 bytes: username length][username bytes]
[16 bytes: salt]
[32 bytes: hash]
[1 byte: isAdmin]
[4 bytes: permission count]
  for each permission:
    [4 bytes: dbName length][dbName bytes]
    [1 byte: permission level]
```

Opening this file in a text editor gives you nothing readable.

---

## HTTP server

The HTTP server is a minimal raw socket implementation with no external dependencies. It supports GET, POST, PUT, DELETE. Each request is handled in its own thread.

### Auth on every route

Every route — without exception — runs the same auth check first:

```
extract Authorization header
→ must be Basic auth
→ decode base64 credentials
→ hash the password, compare against stored hash
→ if ok, check per-db permission for the requested database
→ if all pass, execute the operation
→ if any fail, return 401 or 403 with no data leaked
```

### Response format

All responses are JSON. Success responses always contain `"ok": true`. Error responses always contain `"ok": false` and `"error": "<reason>"`.

```json
{"ok": true, "value": "Alice"}
{"ok": false, "error": "invalid password"}
```

### Route reference

See README.md for the full route table. All routes follow the pattern `/<engine>/<dbname>/<operation>`.

---

## CLI

The CLI runs as an interactive terminal session. It shares the same `AuthStore` as the HTTP server — the same users, the same permissions, the same auth file.

On startup it asks for username and password. If login fails it exits. If login succeeds it drops you into a command prompt showing your username.

All commands that write data require the current user to have at least Write permission on the target database. Creating a database requires admin.

The CLI supports the same four engines as the HTTP server. For key-value operations values are always treated as strings when typed in the terminal. For documents, pass a raw JSON string.

---

## Building

Single command:

```bash
g++ -std=c++17 -pthread -I include -o polystore src/main.cpp src/database.cpp
```

For debug build with address sanitizer:

```bash
g++ -std=c++17 -pthread -g -fsanitize=address -I include -o polystore_debug src/main.cpp src/database.cpp
```

No external dependencies. `httplib.h` is included in the repo.

---

## Error reference

| HTTP status | Meaning |
|---|---|
| 400 | Missing or invalid field in request body |
| 401 | Missing, malformed, or wrong credentials |
| 403 | Valid credentials but insufficient permission |
| 404 | Database, key, document, or node not found |
| 409 | Database with that name already exists |
| 500 | Internal error — check server logs |

| CLI message | Meaning |
|---|---|
| `error: admin required` | Only admins can do this |
| `error: db not found` | No database with that name is loaded |
| `error: engine required` | Must specify kv, doc, graph, or ts |
| `(not found)` | Key does not exist in the store |
| `login failed` | Wrong username or password |
