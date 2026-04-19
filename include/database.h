#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <memory>
#include <mutex>

namespace polystore {

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

constexpr uint8_t  kMagicByte      = 0xDB;
constexpr int  kVersion        = 1;
constexpr uint32_t kMaxKeySize     = 4096;
constexpr uint32_t kMaxValueSize   = 10 * 1024 * 1024;
constexpr uint32_t kPageSize       = 4096;
constexpr uint32_t kCheckpointFreq = 100;
constexpr uint32_t kSaltSize       = 16;
constexpr uint32_t kHashSize       = 32;
constexpr int      kDefaultPort    = 7700;

const std::string kSysUsername  = "sys";
const std::string kAuthFileName = ".auth.bin";

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

enum class Type : uint8_t {
    Null,
    Bool,
    Int64,
    Double,
    String,
    Array,
    Object
}; // enum class Type


enum class Engine : uint8_t {
    KeyValue,
    Document,
    Graph,
    TimeSeries
}; // enum class Engine

enum class Permission : uint8_t {
    Read,
    Write,
    Admin
}; // enum class Permission

struct Value;

using NodeId    = uint64_t;
using Offset    = uint64_t;
using KeyType   = std::string;
using EdgeList  = std::vector<std::pair<NodeId, Value>>;
using FieldMap  = std::unordered_map<std::string, Value>;
using Array     = std::vector<Value>;
using TimeStamp = int64_t;
using ByteVec   = std::vector<uint8_t>;

struct Value {
    Type        type     = Type::Null;
    bool        asBool   = false;
    int64_t     asInt    = 0;
    double      asDouble = 0.0;
    std::string asString;
    Array       asArray;
    FieldMap    asObject;
}; // struct Value

struct Node {
    NodeId   id;
    Value    data;
    EdgeList edges;
}; // struct Node

struct TimeEntry {
    TimeStamp time;
    Value     value;
}; // struct TimeEntry

struct BinHeader {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  engineMask;
    uint32_t docCount;
    Offset   indexOffset;
}; // struct BinHeader

struct WalEntry {
    uint8_t opCode;
    KeyType key;
    Value   value;
}; // struct WalEntry

struct DbPermission {
    std::string dbName;
    Permission  level;
}; // struct DbPermission

struct UserRecord {
    std::string               username;
    uint8_t                   salt[kSaltSize];
    uint8_t                   hash[kHashSize];
    bool                      isAdmin;
    std::vector<DbPermission> permissions;
}; // struct UserRecord

struct AuthResult {
    bool        ok      = false;
    bool        isAdmin = false;
    std::string username;
    std::string reason;
}; // struct AuthResult

// -----------------------------------------------------------------------------
// Base classes
// -----------------------------------------------------------------------------

class Serializer {
public:
    // ByteVec — serializes Value to bytes
    ByteVec serialize(const Value& value);

    // Value — deserializes bytes back to Value
    Value deserialize(const ByteVec& bytes);

    // ByteVec — serializes string with 4-byte length prefix
    ByteVec serializeString(const std::string& str);

    // std::string — deserializes a length-prefixed string
    std::string deserializeString(const uint8_t* data, size_t& offset);

private:
    void  writeValue(const Value& value, ByteVec& out);
    Value readValue(const uint8_t* data, size_t& offset);
}; // class Serializer

class BinFile {
public:
    // void — opens or creates the bin file at path
    void open(const std::string& path);

    // void — closes the file handle
    void close();

    // Offset — writes bytes and returns the offset written at
    Offset write(const ByteVec& data);

    // ByteVec — reads bytes from offset with given length
    ByteVec read(Offset offset, uint32_t length);

    // void — writes the bin header at position 0
    void writeHeader(const BinHeader& header);

    // BinHeader — reads and returns the bin header
    BinHeader readHeader();

    // void — flushes all pending writes to disk
    void flush();

private:
    std::fstream mFile;
    std::string  mPath;
    Offset       mWriteHead = 0;
}; // class BinFile

class Wal {
public:
    // void — opens or creates the WAL file at path
    void open(const std::string& path);

    // void — appends a WalEntry to the log
    void append(const WalEntry& entry);

    // std::vector<WalEntry> — reads all entries for replay
    std::vector<WalEntry> readAll();

    // void — truncates the WAL after a successful checkpoint
    void truncate();

    // void — closes the WAL file
    void close();

private:
    std::fstream mFile;
    std::string  mPath;
}; // class Wal

class AuthStore {
public:
    // void — opens or creates auth file, seeds sys on first run
    void open(const std::string& dataDir);

    // void — saves all user records to the auth file
    void save();

    // AuthResult — validates username and password
    AuthResult authenticate(const std::string& username,
                            const std::string& password);

    // AuthResult — checks if user has at least the given permission on dbName
    AuthResult authorize(const std::string& username,
                         const std::string& dbName,
                         Permission         required);

    // bool — creates a new user, callable only by an admin
    bool createUser(const std::string& callerUsername,
                    const std::string& newUsername,
                    const std::string& password,
                    bool               isAdmin);

    // bool — deletes a user, sys cannot be deleted
    bool deleteUser(const std::string& callerUsername,
                    const std::string& targetUsername);

    // bool — changes a user's password
    bool changePassword(const std::string& callerUsername,
                        const std::string& targetUsername,
                        const std::string& newPassword);

    // bool — changes the sys username, callable only by sys
    bool changeSysUsername(const std::string& callerUsername,
                           const std::string& newUsername);

    // bool — grants permission on a db to a user
    bool grantPermission(const std::string& callerUsername,
                         const std::string& targetUsername,
                         const std::string& dbName,
                         Permission         level);

    // bool — revokes permission on a db from a user
    bool revokePermission(const std::string& callerUsername,
                          const std::string& targetUsername,
                          const std::string& dbName);

    // std::vector<std::string> — lists all usernames, admin only
    std::vector<std::string> listUsers(const std::string& callerUsername);

private:
    void        hashPassword(const std::string& password,
                             const uint8_t*     salt,
                             uint8_t*           outHash);
    void        generateSalt(uint8_t* salt);
    UserRecord* findUser(const std::string& username);
    bool        isCallerAdmin(const std::string& callerUsername);

    std::string             mDataDir;
    std::string             mSysUsername;
    std::vector<UserRecord> mUsers;
    std::mutex              mMutex;
}; // class AuthStore

// -----------------------------------------------------------------------------
// Engine classes
// -----------------------------------------------------------------------------

class KeyValueStore {
public:
    // void — initializes store with bin and wal paths
    void init(const std::string& binPath, const std::string& walPath);

    // void — sets key to value
    void set(const KeyType& key, const Value& value);

    // std::optional<Value> — returns value for key if exists
    std::optional<Value> get(const KeyType& key);

    // bool — returns true if key exists
    bool has(const KeyType& key);

    // void — removes key
    void remove(const KeyType& key);

    // std::vector<KeyType> — returns all keys
    std::vector<KeyType> keys();

    // void — flushes memory state to bin file
    void checkpoint();

    // void — replays WAL and rebuilds memory state
    void recover();

    // void — unloads from RAM
    void unload();

private:
    std::unordered_map<KeyType, Value> mStore;
    BinFile    mBin;
    Wal        mWal;
    Serializer mSerializer;
    uint32_t   mOpsSinceCheckpoint = 0;
}; // class KeyValueStore

class DocumentStore {
public:
    // void — initializes store with bin and wal paths
    void init(const std::string& binPath, const std::string& walPath);

    // uint64_t — inserts document and returns generated id
    uint64_t insert(const Value& document);

    // std::optional<Value> — returns document by id
    std::optional<Value> findById(uint64_t id);

    // std::vector<Value> — returns documents matching field value
    std::vector<Value> findWhere(const std::string& field, const Value& val);

    // void — updates document at id
    void update(uint64_t id, const Value& document);

    // void — removes document by id
    void remove(uint64_t id);

    // void — flushes memory state to bin file
    void checkpoint();

    // void — replays WAL and rebuilds memory state
    void recover();

    // void — unloads from RAM
    void unload();

private:
    std::unordered_map<uint64_t, Value>  mDocs;
    std::unordered_map<uint64_t, Offset> mIndex;
    uint64_t   mNextId = 0;
    BinFile    mBin;
    Wal        mWal;
    Serializer mSerializer;
}; // class DocumentStore

class GraphStore {
public:
    // void — initializes store with bin and wal paths
    void init(const std::string& binPath, const std::string& walPath);

    // NodeId — adds a node and returns its id
    NodeId addNode(const Value& data);

    // void — adds a directed edge from src to dst
    void addEdge(NodeId src, NodeId dst, const Value& edgeData = Value{});

    // std::optional<Node> — returns node by id
    std::optional<Node> getNode(NodeId id);

    // EdgeList — returns all edges from node id
    EdgeList getEdges(NodeId id);

    // std::vector<NodeId> — BFS from start up to maxDepth
    std::vector<NodeId> bfs(NodeId start, uint32_t maxDepth);

    // std::vector<NodeId> — DFS from start up to maxDepth
    std::vector<NodeId> dfs(NodeId start, uint32_t maxDepth);

    // void — removes a node and all its edges
    void removeNode(NodeId id);

    // void — removes edge between src and dst
    void removeEdge(NodeId src, NodeId dst);

    // void — flushes memory state to bin file
    void checkpoint();

    // void — replays WAL and rebuilds memory state
    void recover();

    // void — unloads from RAM
    void unload();

private:
    std::unordered_map<NodeId, Node> mGraph;
    NodeId     mNextId = 0;
    BinFile    mBin;
    Wal        mWal;
    Serializer mSerializer;
}; // class GraphStore

class TimeSeriesStore {
public:
    // void — initializes store with bin and wal paths
    void init(const std::string& binPath, const std::string& walPath);

    // void — inserts a value at timestamp under key
    void insert(const KeyType& key, TimeStamp time, const Value& value);

    // std::vector<TimeEntry> — returns entries in time range
    std::vector<TimeEntry> range(const KeyType& key, TimeStamp from, TimeStamp to);

    // std::optional<TimeEntry> — returns latest entry for key
    std::optional<TimeEntry> latest(const KeyType& key);

    // std::vector<TimeEntry> — returns last N entries for key
    std::vector<TimeEntry> last(const KeyType& key, uint32_t n);

    // void — removes all entries older than timestamp
    void purge(const KeyType& key, TimeStamp olderThan);

    // void — flushes memory state to bin file
    void checkpoint();

    // void — replays WAL and rebuilds memory state
    void recover();

    // void — unloads from RAM
    void unload();

private:
    std::unordered_map<KeyType, std::vector<TimeEntry>> mSeries;
    BinFile    mBin;
    Wal        mWal;
    Serializer mSerializer;
}; // class TimeSeriesStore

// -----------------------------------------------------------------------------
// Database class
// -----------------------------------------------------------------------------

class Database {
public:

    // Database — constructs with name, data directory, explicit engine list
    Database(const std::string& name,
             const std::string& dataDir,
             std::vector<Engine> engines);

    // void — opens the database and loads engines into RAM
    void open();

    // void — closes and unloads from RAM
    void close();

    // void — forces a checkpoint on all active engines
    void checkpoint();

    // KeyValueStore& — returns the key-value engine
    KeyValueStore& kv();

    // DocumentStore& — returns the document engine
    DocumentStore& docs();

    // GraphStore& — returns the graph engine
    GraphStore& graph();

    // TimeSeriesStore& — returns the time series engine
    TimeSeriesStore& ts();

    // std::string — returns the database name
    std::string name() const;

    // bool — returns true if the given engine is active
    bool hasEngine(Engine engine) const;

private:
    void initEngines();

    std::string         mName;
    std::string         mDataDir;
    bool                mOpen = false;
    std::vector<Engine> mActiveEngines;

    std::unique_ptr<KeyValueStore>   mKv;
    std::unique_ptr<DocumentStore>   mDocs;
    std::unique_ptr<GraphStore>      mGraph;
    std::unique_ptr<TimeSeriesStore> mTs;
}; // class Database

// -----------------------------------------------------------------------------
// PolyServer class
// -----------------------------------------------------------------------------

class PolyServer {
public:
    // PolyServer — constructs with data directory and port
    PolyServer(const std::string& dataDir, int port = kDefaultPort);

    // void — starts the HTTP server, blocks forever
    void start();

private:
    // AuthResult — extracts and validates Basic auth from request headers
    AuthResult extractAuth(const std::unordered_map<std::string,std::string>& headers);

    // std::string — decodes a base64 string
    std::string decodeBase64(const std::string& encoded);

    // std::string — builds JSON error body
    std::string jsonError(const std::string& msg);

    // std::string — builds JSON ok body
    std::string jsonOk(const std::string& msg);

    // Value — parses a minimal JSON object string into a Value
    Value parseJson(const std::string& json);

    // std::string — serializes a Value to JSON string
    std::string toJson(const Value& value);

    // std::string — serializes a vector of Values to JSON array
    std::string toJsonArray(const std::vector<Value>& values);

    // std::string — serializes a vector of NodeIds to JSON array
    std::string toJsonIds(const std::vector<NodeId>& ids);

    // Database* — returns existing db or nullptr if not found
Database* findDb(const std::string& name);

// Database& — creates and opens a new db with given engines
Database& createDb(const std::string& name, std::vector<Engine> engines);

    void registerDbRoutes();
    void registerAuthRoutes();
    void registerKvRoutes();
    void registerDocRoutes();
    void registerGraphRoutes();
    void registerTsRoutes();

    std::string                               mDataDir;
    int                                       mPort;
    AuthStore                                 mAuth;
    std::unordered_map<std::string, Database> mDatabases;
    std::mutex                                mDbMutex;
}; // class PolyServer

// -----------------------------------------------------------------------------
// Cli class
// -----------------------------------------------------------------------------

class Cli {
public:
    // Cli — constructs with data directory
    Cli(const std::string& dataDir);

    // void — runs the interactive terminal loop
    void run();

private:
    // void — processes a single command line
    void handleCommand(const std::string& line);

    // void — prints available commands
    void printHelp();

    // bool — authenticates the current session interactively
    bool login();

    std::string                               mDataDir;
    std::string                               mCurrentUser;
    bool                                      mLoggedIn = false;
    AuthStore                                 mAuth;
    std::unordered_map<std::string, Database> mDatabases;
}; // class Cli

} // namespace polystore
