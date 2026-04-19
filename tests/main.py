import requests
from requests.auth import HTTPBasicAuth

BASE = "http://localhost:7700"
AUTH = HTTPBasicAuth("sys", "sys")

def test(label, method, url, body=None):
    r = requests.request(method, BASE + url, json=body, auth=AUTH)
    print(f"{label}: {r.json()}")

# create one of each engine
test("create kv db",    "POST", "/db/create", {"name": "mykvdb",    "engine": "kv"})
test("create doc db",   "POST", "/db/create", {"name": "mydocdb",   "engine": "doc"})
test("create graph db", "POST", "/db/create", {"name": "mygraphdb", "engine": "graph"})
test("create ts db",    "POST", "/db/create", {"name": "mytsdb",    "engine": "ts"})

# key-value
test("kv set",    "POST",   "/kv/mykvdb/set",       {"key": "name", "value": "Alice"})
test("kv get",    "GET",    "/kv/mykvdb/get/name")
test("kv keys",   "GET",    "/kv/mykvdb/keys")
test("kv delete", "DELETE", "/kv/mykvdb/remove/name")

# document
test("doc insert", "POST", "/doc/mydocdb/insert",  {"name": "Alice", "age": 25})
test("doc get",    "GET",  "/doc/mydocdb/get/0")
test("doc find",   "POST", "/doc/mydocdb/find",    {"field": "name", "value": "Alice"})
test("doc update", "PUT",  "/doc/mydocdb/update/0",{"name": "Alice", "age": 26})
test("doc remove", "DELETE","/doc/mydocdb/remove/0")

# graph
test("graph add node 1", "POST", "/graph/mygraphdb/node/add", {"label": "Alice"})
test("graph add node 2", "POST", "/graph/mygraphdb/node/add", {"label": "Bob"})
test("graph add edge",   "POST", "/graph/mygraphdb/edge/add", {"src": 0, "dst": 1, "data": {"rel": "friends"}})
test("graph get node",   "GET",  "/graph/mygraphdb/node/0")
test("graph bfs",        "GET",  "/graph/mygraphdb/bfs/0/3")
test("graph dfs",        "GET",  "/graph/mygraphdb/dfs/0/3")
test("graph remove edge","DELETE","/graph/mygraphdb/edge/0/1")
test("graph remove node","DELETE","/graph/mygraphdb/node/0")

# time series
test("ts insert",  "POST",   "/ts/mytsdb/insert", {"key": "cpu", "time": 1700000001, "value": 42.5})
test("ts insert2", "POST",   "/ts/mytsdb/insert", {"key": "cpu", "time": 1700000002, "value": 43.1})
test("ts latest",  "GET",    "/ts/mytsdb/latest/cpu")
test("ts range",   "POST",   "/ts/mytsdb/range",  {"key": "cpu", "from": 1700000000, "to": 1700000010})
test("ts purge",   "DELETE", "/ts/mytsdb/purge",  {"key": "cpu", "olderThan": 1700000002})

# auth
test("create user",  "POST", "/auth/user/create", {"username": "bob", "password": "secret", "isAdmin": False})
test("grant access", "POST", "/auth/user/grant",  {"username": "bob", "db": "mykvdb", "level": "read"})
test("list users",   "GET",  "/auth/users")

# bob can read, cannot write
bob = HTTPBasicAuth("bob", "secret")
r = requests.get(BASE + "/kv/mykvdb/get/name", auth=bob)
print(f"bob read (should 404 since deleted): {r.json()}")
r = requests.post(BASE + "/kv/mykvdb/set", json={"key":"x","value":"y"}, auth=bob)
print(f"bob write (should 403): {r.json()}")

# wrong password
r = requests.get(BASE + "/db/list", auth=HTTPBasicAuth("sys", "wrongpass"))
print(f"wrong password (should 401): {r.json()}")

print("\ndone")
