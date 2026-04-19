# PolyStore — Tutorial

This tutorial is for everyone. Whether you have never touched a database before or you have been writing backend code for years, this guide will get you from zero to a fully working database in under 10 minutes.

---

## Part 1 — For everyone: what is PolyStore and why should you care

A database is just a place where your app stores things so they don't disappear when you close it. Your notes app stores notes. Your chat app stores messages. Your user profile lives in a database somewhere.

Most databases are hosted on someone else's server. You pay for them, you trust them with your data, and if they go down, your app goes down.

PolyStore runs on your own machine. Your data never leaves. No account, no subscription, no cloud. Just a program you run yourself.

It also gives you four different ways to store data depending on what makes sense for your situation:

- **Key-Value** — like a dictionary. You give it a name, it gives you back a value. Fast and simple.
- **Document** — like a folder of forms. Each record can have different fields. Good for user profiles, blog posts, anything with structure.
- **Graph** — like a map of connections. Who follows who, what relates to what. Good for social features and recommendations.
- **Time Series** — like a logbook with timestamps. Temperature every minute, page views every hour, anything that happens over time.

---

## Part 2 — Getting it running

### Step 1: Build it

Open a terminal and run:

```bash
git clone https://github.com/yourname/polystore
cd polystore
mkdir build && cd build
g++ -std=c++17 -pthread -I ../include -o polystore ../src/main.cpp ../src/database.cpp
```

If you get an error saying `g++` is not found, install it:
- On Ubuntu/Debian: `sudo apt install g++`
- On Mac: `xcode-select --install`
- On Windows: install WSL, then follow the Ubuntu steps

### Step 2: Start the server

```bash
./polystore --server
```

You should see:

```
[polystore] first run: sys user created with password 'sys'
[polystore] change it immediately with: user passwd sys <newpassword>
[polystore] server running on port 7700
[polystore] default login: sys / sys
```

The server is now running. Leave this terminal open. Open a new one for everything else.

### Step 3: Change your password

In the new terminal:

```bash
curl -X POST http://localhost:7700/auth/user/passwd \
  -H "Authorization: Basic $(echo -n 'sys:sys' | base64)" \
  -H "Content-Type: application/json" \
  -d '{"username":"sys","password":"mynewpassword"}'
```

You should get back:

```json
{"ok": true, "message": "password changed"}
```

From now on use your new password everywhere.

---

## Part 3 — Your first database (non-programmer path)

Let's say you want to store a list of books you have read. We will use the document engine because each book has multiple fields — title, author, rating.

### Create the database

```bash
curl -X POST http://localhost:7700/db/create \
  -H "Authorization: Basic $(echo -n 'sys:mynewpassword' | base64)" \
  -H "Content-Type: application/json" \
  -d '{"name":"books","engine":"doc"}'
```

Response:
```json
{"ok": true, "message": "database books created"}
```

### Add a book

```bash
curl -X POST http://localhost:7700/doc/books/insert \
  -H "Authorization: Basic $(echo -n 'sys:mynewpassword' | base64)" \
  -H "Content-Type: application/json" \
  -d '{"title":"Dune","author":"Frank Herbert","rating":5}'
```

Response:
```json
{"ok": true, "id": 0}
```

The `id` is how you find this book later. The first one is always 0.

### Add another book

```bash
curl -X POST http://localhost:7700/doc/books/insert \
  -H "Authorization: Basic $(echo -n 'sys:mynewpassword' | base64)" \
  -H "Content-Type: application/json" \
  -d '{"title":"Foundation","author":"Isaac Asimov","rating":4}'
```

Response:
```json
{"ok": true, "id": 1}
```

### Get a book by id

```bash
curl http://localhost:7700/doc/books/get/0 \
  -H "Authorization: Basic $(echo -n 'sys:mynewpassword' | base64)"
```

Response:
```json
{"ok": true, "document": {"title": "Dune", "author": "Frank Herbert", "rating": 5}}
```

### Find all books by author

```bash
curl -X POST http://localhost:7700/doc/books/find \
  -H "Authorization: Basic $(echo -n 'sys:mynewpassword' | base64)" \
  -H "Content-Type: application/json" \
  -d '{"field":"author","value":"Frank Herbert"}'
```

### Update a book

```bash
curl -X PUT http://localhost:7700/doc/books/update/0 \
  -H "Authorization: Basic $(echo -n 'sys:mynewpassword' | base64)" \
  -H "Content-Type: application/json" \
  -d '{"title":"Dune","author":"Frank Herbert","rating":5,"notes":"masterpiece"}'
```

### Delete a book

```bash
curl -X DELETE http://localhost:7700/doc/books/remove/1 \
  -H "Authorization: Basic $(echo -n 'sys:mynewpassword' | base64)"
```

That is the full lifecycle. Create, read, find, update, delete. Everything else in PolyStore works the same way — just swap the engine name in the URL.

---

## Part 4 — Using Insomnia (easier than curl)

If typing curl commands feels annoying, use Insomnia. It is a free app that gives you a nice interface for sending HTTP requests.

1. Download Insomnia from insomnia.rest
2. Create a new request collection called PolyStore
3. For every request set Auth to **Basic Auth**, username `sys`, password your password
4. Set the URL to `http://localhost:7700/...`
5. For POST and PUT requests set Body to **JSON** and paste your data

You can save each request and reuse it. Much easier than curl for day to day use.

---

## Part 5 — Using the CLI (even easier for quick things)

Instead of HTTP you can use the built-in terminal interface:

```bash
./polystore
```

It will ask for your username and password. After you log in:

```
sys> db create books doc
database 'books' created

sys> doc books insert {"title":"Dune","author":"Frank Herbert"}
inserted — id: 0

sys> doc books get 0
{"title":"Dune","author":"Frank Herbert"}

sys> kv books set theme dark
ok

sys> kv books get theme
dark

sys> help
```

The CLI is good for quick checks, creating databases, and managing users. For building an app you will want the HTTP API.

---

## Part 6 — For programmers: using it from your code

### Python

```python
import requests
from requests.auth import HTTPBasicAuth

auth = HTTPBasicAuth("sys", "mynewpassword")
base = "http://localhost:7700"

# create a database
requests.post(f"{base}/db/create", json={"name":"users","engine":"doc"}, auth=auth)

# insert a user
r = requests.post(f"{base}/doc/users/insert",
    json={"name": "Alice", "email": "alice@example.com", "plan": "free"},
    auth=auth)
user_id = r.json()["id"]

# get the user back
r = requests.get(f"{base}/doc/users/get/{user_id}", auth=auth)
print(r.json()["document"])

# find all free plan users
r = requests.post(f"{base}/doc/users/find",
    json={"field": "plan", "value": "free"},
    auth=auth)
print(r.json()["results"])
```

### JavaScript (Node.js)

```javascript
const base = "http://localhost:7700";
const headers = {
  "Authorization": "Basic " + Buffer.from("sys:mynewpassword").toString("base64"),
  "Content-Type": "application/json"
};

// create a database
await fetch(`${base}/db/create`, {
  method: "POST",
  headers,
  body: JSON.stringify({ name: "users", engine: "doc" })
});

// insert a document
const res = await fetch(`${base}/doc/users/insert`, {
  method: "POST",
  headers,
  body: JSON.stringify({ name: "Alice", email: "alice@example.com" })
});
const { id } = await res.json();

// get it back
const doc = await fetch(`${base}/doc/users/get/${id}`, { headers });
console.log(await doc.json());
```

### Go

```go
package main

import (
    "bytes"
    "encoding/json"
    "fmt"
    "net/http"
)

func main() {
    client := &http.Client{}
    body, _ := json.Marshal(map[string]string{
        "name": "users", "engine": "doc",
    })
    req, _ := http.NewRequest("POST", "http://localhost:7700/db/create", bytes.NewBuffer(body))
    req.SetBasicAuth("sys", "mynewpassword")
    req.Header.Set("Content-Type", "application/json")
    resp, _ := client.Do(req)
    fmt.Println(resp.Status)
}
```

---

## Part 7 — Real world examples

### Example 1: storing user sessions (Key-Value)

Sessions are perfect for key-value because you look them up by a single ID.

```python
import requests, uuid
from requests.auth import HTTPBasicAuth

auth = HTTPBasicAuth("sys", "mynewpassword")
base = "http://localhost:7700"

# create the session database once
requests.post(f"{base}/db/create", json={"name":"sessions","engine":"kv"}, auth=auth)

# when a user logs in, create a session
session_id = str(uuid.uuid4())
requests.post(f"{base}/kv/sessions/set",
    json={"key": session_id, "value": "user:42"},
    auth=auth)

# on each request, validate the session
r = requests.get(f"{base}/kv/sessions/get/{session_id}", auth=auth)
if r.json().get("ok"):
    print("valid session for", r.json()["value"])
```

### Example 2: tracking sensor data (Time Series)

```python
import time, random, requests
from requests.auth import HTTPBasicAuth

auth = HTTPBasicAuth("sys", "mynewpassword")
base = "http://localhost:7700"

requests.post(f"{base}/db/create", json={"name":"sensors","engine":"ts"}, auth=auth)

# log a temperature reading every second
for i in range(10):
    requests.post(f"{base}/ts/sensors/insert", auth=auth, json={
        "key": "room1_temp",
        "time": int(time.time()),
        "value": round(20 + random.uniform(-1, 1), 2)
    })
    time.sleep(1)

# get the last reading
r = requests.get(f"{base}/ts/sensors/latest/room1_temp", auth=auth)
print("latest:", r.json())

# get all readings from the last 10 seconds
now = int(time.time())
r = requests.post(f"{base}/ts/sensors/range", auth=auth, json={
    "key": "room1_temp",
    "from": now - 10,
    "to": now
})
print("history:", r.json()["entries"])
```

### Example 3: a social follow graph

```python
import requests
from requests.auth import HTTPBasicAuth

auth = HTTPBasicAuth("sys", "mynewpassword")
base = "http://localhost:7700"

requests.post(f"{base}/db/create", json={"name":"social","engine":"graph"}, auth=auth)

# add users as nodes
alice = requests.post(f"{base}/graph/social/node/add",
    json={"username": "alice"}, auth=auth).json()["id"]
bob   = requests.post(f"{base}/graph/social/node/add",
    json={"username": "bob"},   auth=auth).json()["id"]
carol = requests.post(f"{base}/graph/social/node/add",
    json={"username": "carol"}, auth=auth).json()["id"]

# alice follows bob, bob follows carol
requests.post(f"{base}/graph/social/edge/add",
    json={"src": alice, "dst": bob, "data": {"type": "follows"}}, auth=auth)
requests.post(f"{base}/graph/social/edge/add",
    json={"src": bob, "dst": carol, "data": {"type": "follows"}}, auth=auth)

# who can alice reach within 2 hops
r = requests.get(f"{base}/graph/social/bfs/{alice}/2", auth=auth)
print("alice's network:", r.json()["nodes"])
```

---

## Part 8 — Managing users and access

### Create a read-only user for your frontend

```bash
# create the user
curl -X POST http://localhost:7700/auth/user/create \
  -H "Authorization: Basic $(echo -n 'sys:mynewpassword' | base64)" \
  -H "Content-Type: application/json" \
  -d '{"username":"frontend","password":"frontendpass","isAdmin":false}'

# give them read access to the books database
curl -X POST http://localhost:7700/auth/user/grant \
  -H "Authorization: Basic $(echo -n 'sys:mynewpassword' | base64)" \
  -H "Content-Type: application/json" \
  -d '{"username":"frontend","db":"books","level":"read"}'
```

Now your frontend can read from `books` but cannot write, delete, or touch any other database.

### Create a write user for your backend

```bash
curl -X POST http://localhost:7700/auth/user/grant \
  -H "Authorization: Basic $(echo -n 'sys:mynewpassword' | base64)" \
  -H "Content-Type: application/json" \
  -d '{"username":"backend","db":"books","level":"write"}'
```

### List all users

```bash
curl http://localhost:7700/auth/users \
  -H "Authorization: Basic $(echo -n 'sys:mynewpassword' | base64)"
```

---

## Part 9 — Common mistakes and how to fix them

**"engine not active"**
You created the database with one engine but are trying to use a different one. A `doc` database does not have a `kv` engine. Create separate databases for each engine type.

**"database not found"**
The database name in the URL does not match any created database. Check spelling. Database names are case sensitive.

**401 on every request**
Your credentials are wrong or the base64 encoding is off. Double check the username and password. In curl use `$(echo -n 'user:pass' | base64)` — the `-n` flag is important, without it there is a newline in the encoded string and auth breaks.

**"admin required" in CLI**
The `sys` user is admin but the `authorize` check on a database they have not explicitly been granted access to returns not-ok. This is a known edge case — `sys` should always be able to create databases regardless. If you hit this, use the HTTP API to create the database instead.

**Server crashes on startup**
Delete the `data/` directory and let it regenerate. If you have an old auth file from a different version of the code the binary format may not match.

**Port already in use**
Something else is running on port 7700. Either stop that process or change `kDefaultPort` in `database.h` and recompile.

---

## Part 10 — What's coming next

These are not in v1 but are planned:

- **WideColumn engine** — sparse rows with column families, perfect for user profiles with hundreds of optional fields
- **Custom engine profiles via JSON** — define your own combination of engines in a config file and share it with the community
- **Python SDK** — `pip install polystore` and use it like any other database client
- **JavaScript SDK** — `npm install polystore`
- **Replication** — run two instances and keep them in sync

---

That is everything you need to go from nothing to a working self-hosted database. If something is unclear or broken, open an issue on GitHub.
