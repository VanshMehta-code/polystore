#include "../include/database.h"
#include "../include/httplib.h"

#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace polystore {

// -----------------------------------------------------------------------------
// Serializer
// -----------------------------------------------------------------------------

void Serializer::writeValue(const Value& value, ByteVec& out) {
    out.push_back(static_cast<uint8_t>(value.type));
    switch (value.type) {
        case Type::Null: break;
        case Type::Bool:
            out.push_back(value.asBool ? 1 : 0);
            break;
        case Type::Int64: {
            const uint8_t* b = reinterpret_cast<const uint8_t*>(&value.asInt);
            out.insert(out.end(), b, b + 8);
            break;
        }
        case Type::Double: {
            const uint8_t* b = reinterpret_cast<const uint8_t*>(&value.asDouble);
            out.insert(out.end(), b, b + 8);
            break;
        }
        case Type::String: {
            ByteVec s = serializeString(value.asString);
            out.insert(out.end(), s.begin(), s.end());
            break;
        }
        case Type::Array: {
            uint32_t count = static_cast<uint32_t>(value.asArray.size());
            const uint8_t* b = reinterpret_cast<const uint8_t*>(&count);
            out.insert(out.end(), b, b + 4);
            for (const Value& v : value.asArray) writeValue(v, out);
            break;
        }
        case Type::Object: {
            uint32_t count = static_cast<uint32_t>(value.asObject.size());
            const uint8_t* b = reinterpret_cast<const uint8_t*>(&count);
            out.insert(out.end(), b, b + 4);
            for (const auto& [k, v] : value.asObject) {
                ByteVec kb = serializeString(k);
                out.insert(out.end(), kb.begin(), kb.end());
                writeValue(v, out);
            }
            break;
        }
    }
}

Value Serializer::readValue(const uint8_t* data, size_t& offset) {
    Value value;
    value.type = static_cast<Type>(data[offset++]);
    switch (value.type) {
        case Type::Null: break;
        case Type::Bool:
            value.asBool = data[offset++] != 0;
            break;
        case Type::Int64:
            std::memcpy(&value.asInt, data + offset, 8); offset += 8;
            break;
        case Type::Double:
            std::memcpy(&value.asDouble, data + offset, 8); offset += 8;
            break;
        case Type::String:
            value.asString = deserializeString(data, offset);
            break;
        case Type::Array: {
            uint32_t count = 0;
            std::memcpy(&count, data + offset, 4); offset += 4;
            value.asArray.reserve(count);
            for (uint32_t i = 0; i < count; i++)
                value.asArray.push_back(readValue(data, offset));
            break;
        }
        case Type::Object: {
            uint32_t count = 0;
            std::memcpy(&count, data + offset, 4); offset += 4;
            for (uint32_t i = 0; i < count; i++) {
                std::string k = deserializeString(data, offset);
                value.asObject[k] = readValue(data, offset);
            }
            break;
        }
    }
    return value;
}

ByteVec Serializer::serialize(const Value& value) {
    ByteVec out;
    writeValue(value, out);
    return out;
}

Value Serializer::deserialize(const ByteVec& bytes) {
    size_t offset = 0;
    return readValue(bytes.data(), offset);
}

ByteVec Serializer::serializeString(const std::string& str) {
    ByteVec out;
    uint32_t len = static_cast<uint32_t>(str.size());
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&len);
    out.insert(out.end(), b, b + 4);
    out.insert(out.end(), str.begin(), str.end());
    return out;
}

std::string Serializer::deserializeString(const uint8_t* data, size_t& offset) {
    uint32_t len = 0;
    std::memcpy(&len, data + offset, 4); offset += 4;
    std::string s(reinterpret_cast<const char*>(data + offset), len);
    offset += len;
    return s;
}

// -----------------------------------------------------------------------------
// BinFile
// -----------------------------------------------------------------------------

void BinFile::open(const std::string& path) {
    mPath = path;
    mFile.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!mFile.is_open()) {
        mFile.open(path, std::ios::out | std::ios::binary);
        mFile.close();
        mFile.open(path, std::ios::in | std::ios::out | std::ios::binary);
    }
    mFile.seekg(0, std::ios::end);
    mWriteHead = static_cast<Offset>(mFile.tellg());
}

void BinFile::close() {
    if (mFile.is_open()) mFile.close();
}

Offset BinFile::write(const ByteVec& data) {
    mFile.seekp(static_cast<std::streamoff>(mWriteHead));
    uint32_t len = static_cast<uint32_t>(data.size());
    mFile.write(reinterpret_cast<const char*>(&len), 4);
    mFile.write(reinterpret_cast<const char*>(data.data()), len);
    Offset at = mWriteHead;
    mWriteHead += 4 + len;
    return at;
}

ByteVec BinFile::read(Offset offset, uint32_t length) {
    mFile.seekg(static_cast<std::streamoff>(offset));
    uint32_t stored = 0;
    mFile.read(reinterpret_cast<char*>(&stored), 4);
    uint32_t n = length > 0 ? length : stored;
    ByteVec data(n);
    mFile.read(reinterpret_cast<char*>(data.data()), n);
    return data;
}

void BinFile::writeHeader(const BinHeader& header) {
    mFile.seekp(0);
    mFile.write(reinterpret_cast<const char*>(&header), sizeof(BinHeader));
}

BinHeader BinFile::readHeader() {
    BinHeader h{};
    mFile.seekg(0);
    mFile.read(reinterpret_cast<char*>(&h), sizeof(BinHeader));
    return h;
}

void BinFile::flush() { mFile.flush(); }

// -----------------------------------------------------------------------------
// Wal
// -----------------------------------------------------------------------------

void Wal::open(const std::string& path) {
    mPath = path;
    mFile.open(path, std::ios::in | std::ios::out | std::ios::app | std::ios::binary);
    if (!mFile.is_open()) {
        mFile.open(path, std::ios::out | std::ios::binary);
        mFile.close();
        mFile.open(path, std::ios::in | std::ios::out | std::ios::app | std::ios::binary);
    }
}

void Wal::append(const WalEntry& entry) {
    Serializer s;
    mFile.write(reinterpret_cast<const char*>(&entry.opCode), 1);
    ByteVec kb = s.serializeString(entry.key);
    mFile.write(reinterpret_cast<const char*>(kb.data()), kb.size());
    ByteVec vb = s.serialize(entry.value);
    uint32_t vlen = static_cast<uint32_t>(vb.size());
    mFile.write(reinterpret_cast<const char*>(&vlen), 4);
    mFile.write(reinterpret_cast<const char*>(vb.data()), vlen);
    mFile.flush();
}

std::vector<WalEntry> Wal::readAll() {
    std::vector<WalEntry> entries;
    Serializer s;
    std::ifstream reader(mPath, std::ios::binary);
    if (!reader.is_open()) return entries;
    while (reader.peek() != EOF) {
        WalEntry entry;
        reader.read(reinterpret_cast<char*>(&entry.opCode), 1);
        if (reader.fail()) break;
        uint32_t klen = 0;
        reader.read(reinterpret_cast<char*>(&klen), 4);
        entry.key.resize(klen);
        reader.read(entry.key.data(), klen);
        uint32_t vlen = 0;
        reader.read(reinterpret_cast<char*>(&vlen), 4);
        ByteVec vb(vlen);
        reader.read(reinterpret_cast<char*>(vb.data()), vlen);
        entry.value = s.deserialize(vb);
        entries.push_back(std::move(entry));
    }
    return entries;
}

void Wal::truncate() {
    mFile.close();
    mFile.open(mPath, std::ios::out | std::ios::trunc | std::ios::binary);
    mFile.close();
    mFile.open(mPath, std::ios::in | std::ios::out | std::ios::app | std::ios::binary);
}

void Wal::close() {
    if (mFile.is_open()) mFile.close();
}

// -----------------------------------------------------------------------------
// AuthStore
// -----------------------------------------------------------------------------

void AuthStore::generateSalt(uint8_t* salt) {
    srand(static_cast<unsigned>(time(nullptr)));
    for (uint32_t i = 0; i < kSaltSize; i++)
        salt[i] = static_cast<uint8_t>(rand() % 256);
}

void AuthStore::hashPassword(const std::string& password,
                             const uint8_t*     salt,
                             uint8_t*           outHash) {
    // djb2-based hash over salt+password, expanded to 32 bytes
    uint64_t h[4] = { 5381, 52711, 0x9e3779b9, 0x6c62272e };
    for (uint32_t i = 0; i < kSaltSize; i++) {
        h[0] = ((h[0] << 5) + h[0]) ^ salt[i];
        h[1] = ((h[1] << 5) + h[1]) ^ salt[i];
        h[2] = ((h[2] << 5) + h[2]) ^ salt[i];
        h[3] = ((h[3] << 5) + h[3]) ^ salt[i];
    }
    for (char c : password) {
        h[0] = ((h[0] << 5) + h[0]) ^ static_cast<uint8_t>(c);
        h[1] = ((h[1] << 5) + h[1]) ^ static_cast<uint8_t>(c);
        h[2] = ((h[2] << 5) + h[2]) ^ static_cast<uint8_t>(c);
        h[3] = ((h[3] << 5) + h[3]) ^ static_cast<uint8_t>(c);
    }
    std::memcpy(outHash,      &h[0], 8);
    std::memcpy(outHash + 8,  &h[1], 8);
    std::memcpy(outHash + 16, &h[2], 8);
    std::memcpy(outHash + 24, &h[3], 8);
}

UserRecord* AuthStore::findUser(const std::string& username) {
    for (auto& u : mUsers)
        if (u.username == username) return &u;
    return nullptr;
}

bool AuthStore::isCallerAdmin(const std::string& callerUsername) {
    UserRecord* u = findUser(callerUsername);
    return u && u->isAdmin;
}

void AuthStore::open(const std::string& dataDir) {
    mDataDir     = dataDir;
    mSysUsername = kSysUsername;
    std::string path = dataDir + "/" + kAuthFileName;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        // First run — seed sys with default password "sys"
        UserRecord sys{};
        sys.username = kSysUsername;
        sys.isAdmin  = true;
        generateSalt(sys.salt);
        hashPassword("sys", sys.salt, sys.hash);
        mUsers.push_back(sys);
        save();
        std::cout << "[polystore] first run: sys user created with password 'sys'\n";
        std::cout << "[polystore] change it immediately with: user passwd sys <newpassword>\n";
        return;
    }

    while (file.peek() != EOF) {
        UserRecord u{};
        uint32_t ulen = 0;
        file.read(reinterpret_cast<char*>(&ulen), 4);
        if (file.fail()) break;
        u.username.resize(ulen);
        file.read(u.username.data(), ulen);
        file.read(reinterpret_cast<char*>(u.salt), kSaltSize);
        file.read(reinterpret_cast<char*>(u.hash), kHashSize);
        file.read(reinterpret_cast<char*>(&u.isAdmin), 1);
        uint32_t pcount = 0;
        file.read(reinterpret_cast<char*>(&pcount), 4);
        for (uint32_t i = 0; i < pcount; i++) {
            DbPermission p;
            uint32_t dlen = 0;
            file.read(reinterpret_cast<char*>(&dlen), 4);
            p.dbName.resize(dlen);
            file.read(p.dbName.data(), dlen);
            file.read(reinterpret_cast<char*>(&p.level), 1);
            u.permissions.push_back(p);
        }
        mUsers.push_back(u);
    }
}

void AuthStore::save() {
    std::string path = mDataDir + "/" + kAuthFileName;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    for (const auto& u : mUsers) {
        uint32_t ulen = static_cast<uint32_t>(u.username.size());
        file.write(reinterpret_cast<const char*>(&ulen), 4);
        file.write(u.username.data(), ulen);
        file.write(reinterpret_cast<const char*>(u.salt), kSaltSize);
        file.write(reinterpret_cast<const char*>(u.hash), kHashSize);
        file.write(reinterpret_cast<const char*>(&u.isAdmin), 1);
        uint32_t pcount = static_cast<uint32_t>(u.permissions.size());
        file.write(reinterpret_cast<const char*>(&pcount), 4);
        for (const auto& p : u.permissions) {
            uint32_t dlen = static_cast<uint32_t>(p.dbName.size());
            file.write(reinterpret_cast<const char*>(&dlen), 4);
            file.write(p.dbName.data(), dlen);
            file.write(reinterpret_cast<const char*>(&p.level), 1);
        }
    }
}

AuthResult AuthStore::authenticate(const std::string& username,
                                   const std::string& password) {
    std::lock_guard<std::mutex> lock(mMutex);
    UserRecord* u = findUser(username);
    if (!u) return { false, false, "", "user not found" };
    uint8_t h[kHashSize];
    hashPassword(password, u->salt, h);
    if (std::memcmp(h, u->hash, kHashSize) != 0)
        return { false, false, "", "invalid password" };
    return { true, u->isAdmin, username, "" };
}

AuthResult AuthStore::authorize(const std::string& username,
                                const std::string& dbName,
                                Permission         required) {
    std::lock_guard<std::mutex> lock(mMutex);
    UserRecord* u = findUser(username);
    if (!u) return { false, false, "", "user not found" };
    if (u->isAdmin) return { true, true, username, "" };
    for (const auto& p : u->permissions) {
        if (p.dbName == dbName) {
            if (static_cast<uint8_t>(p.level) >= static_cast<uint8_t>(required))
                return { true, false, username, "" };
            return { false, false, "", "insufficient permission" };
        }
    }
    return { false, false, "", "no access to this database" };
}

bool AuthStore::createUser(const std::string& callerUsername,
                           const std::string& newUsername,
                           const std::string& password,
                           bool               isAdmin) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!isCallerAdmin(callerUsername)) return false;
    if (findUser(newUsername)) return false;
    UserRecord u{};
    u.username = newUsername;
    u.isAdmin  = isAdmin;
    generateSalt(u.salt);
    hashPassword(password, u.salt, u.hash);
    mUsers.push_back(u);
    save();
    return true;
}

bool AuthStore::deleteUser(const std::string& callerUsername,
                           const std::string& targetUsername) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!isCallerAdmin(callerUsername)) return false;
    if (targetUsername == mSysUsername) return false;
    auto it = std::remove_if(mUsers.begin(), mUsers.end(),
        [&](const UserRecord& u) { return u.username == targetUsername; });
    if (it == mUsers.end()) return false;
    mUsers.erase(it, mUsers.end());
    save();
    return true;
}

bool AuthStore::changePassword(const std::string& callerUsername,
                               const std::string& targetUsername,
                               const std::string& newPassword) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (callerUsername != targetUsername && !isCallerAdmin(callerUsername))
        return false;
    UserRecord* u = findUser(targetUsername);
    if (!u) return false;
    generateSalt(u->salt);
    hashPassword(newPassword, u->salt, u->hash);
    save();
    return true;
}

bool AuthStore::changeSysUsername(const std::string& callerUsername,
                                  const std::string& newUsername) {
    std::lock_guard<std::mutex> lock(mMutex);
    UserRecord* caller = findUser(callerUsername);
    if (!caller) return false;
    if (caller->username != mSysUsername) return false;
    if (findUser(newUsername)) return false;
    caller->username = newUsername;
    mSysUsername     = newUsername;
    save();
    return true;
}

bool AuthStore::grantPermission(const std::string& callerUsername,
                                const std::string& targetUsername,
                                const std::string& dbName,
                                Permission         level) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!isCallerAdmin(callerUsername)) return false;
    UserRecord* u = findUser(targetUsername);
    if (!u) return false;
    for (auto& p : u->permissions) {
        if (p.dbName == dbName) { p.level = level; save(); return true; }
    }
    u->permissions.push_back({ dbName, level });
    save();
    return true;
}

bool AuthStore::revokePermission(const std::string& callerUsername,
                                 const std::string& targetUsername,
                                 const std::string& dbName) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!isCallerAdmin(callerUsername)) return false;
    UserRecord* u = findUser(targetUsername);
    if (!u) return false;
    auto it = std::remove_if(u->permissions.begin(), u->permissions.end(),
        [&](const DbPermission& p) { return p.dbName == dbName; });
    if (it == u->permissions.end()) return false;
    u->permissions.erase(it, u->permissions.end());
    save();
    return true;
}

std::vector<std::string> AuthStore::listUsers(const std::string& callerUsername) {
    std::lock_guard<std::mutex> lock(mMutex);
    std::vector<std::string> names;
    if (!isCallerAdmin(callerUsername)) return names;
    for (const auto& u : mUsers) names.push_back(u.username);
    return names;
}

// -----------------------------------------------------------------------------
// KeyValueStore
// -----------------------------------------------------------------------------

void KeyValueStore::init(const std::string& binPath, const std::string& walPath) {
    mBin.open(binPath);
    mWal.open(walPath);
}

void KeyValueStore::set(const KeyType& key, const Value& value) {
    mStore[key] = value;
    mWal.append({ 0x01, key, value });
    if (++mOpsSinceCheckpoint >= kCheckpointFreq) {
        checkpoint();
        mOpsSinceCheckpoint = 0;
    }
}

std::optional<Value> KeyValueStore::get(const KeyType& key) {
    auto it = mStore.find(key);
    if (it == mStore.end()) return std::nullopt;
    return it->second;
}

bool KeyValueStore::has(const KeyType& key) {
    return mStore.find(key) != mStore.end();
}

void KeyValueStore::remove(const KeyType& key) {
    mStore.erase(key);
    Value t; t.type = Type::Null;
    mWal.append({ 0x02, key, t });
}

std::vector<KeyType> KeyValueStore::keys() {
    std::vector<KeyType> result;
    result.reserve(mStore.size());
    for (const auto& [k, v] : mStore) result.push_back(k);
    return result;
}

void KeyValueStore::checkpoint() {
    for (const auto& [key, value] : mStore) {
        ByteVec kb = mSerializer.serializeString(key);
        ByteVec vb = mSerializer.serialize(value);
        ByteVec combined;
        combined.insert(combined.end(), kb.begin(), kb.end());
        combined.insert(combined.end(), vb.begin(), vb.end());
        mBin.write(combined);
    }
    mBin.flush();
    mWal.truncate();
}

void KeyValueStore::recover() {
    for (const auto& e : mWal.readAll()) {
        if (e.opCode == 0x01) mStore[e.key] = e.value;
        else if (e.opCode == 0x02) mStore.erase(e.key);
    }
}

void KeyValueStore::unload() {
    checkpoint();
    mStore.clear();
    mBin.close();
    mWal.close();
}

// -----------------------------------------------------------------------------
// DocumentStore
// -----------------------------------------------------------------------------

void DocumentStore::init(const std::string& binPath, const std::string& walPath) {
    mBin.open(binPath);
    mWal.open(walPath);
}

uint64_t DocumentStore::insert(const Value& document) {
    uint64_t id    = mNextId++;
    mDocs[id]      = document;
    ByteVec data   = mSerializer.serialize(document);
    Offset  offset = mBin.write(data);
    mIndex[id]     = offset;
    mWal.append({ 0x01, std::to_string(id), document });
    return id;
}

std::optional<Value> DocumentStore::findById(uint64_t id) {
    auto it = mDocs.find(id);
    if (it == mDocs.end()) return std::nullopt;
    return it->second;
}

std::vector<Value> DocumentStore::findWhere(const std::string& field, const Value& val) {
    std::vector<Value> results;
    for (const auto& [id, doc] : mDocs) {
        if (doc.type != Type::Object) continue;
        auto it = doc.asObject.find(field);
        if (it == doc.asObject.end()) continue;
        const Value& fv = it->second;
        if (fv.type != val.type) continue;
        bool match = false;
        switch (val.type) {
            case Type::Bool:   match = fv.asBool   == val.asBool;   break;
            case Type::Int64:  match = fv.asInt    == val.asInt;    break;
            case Type::Double: match = fv.asDouble == val.asDouble; break;
            case Type::String: match = fv.asString == val.asString; break;
            default: break;
        }
        if (match) results.push_back(doc);
    }
    return results;
}

void DocumentStore::update(uint64_t id, const Value& document) {
    mDocs[id]      = document;
    ByteVec data   = mSerializer.serialize(document);
    Offset  offset = mBin.write(data);
    mIndex[id]     = offset;
    mWal.append({ 0x03, std::to_string(id), document });
}

void DocumentStore::remove(uint64_t id) {
    mDocs.erase(id);
    mIndex.erase(id);
    Value t; t.type = Type::Null;
    mWal.append({ 0x02, std::to_string(id), t });
}

void DocumentStore::checkpoint() {
    for (const auto& [id, doc] : mDocs) {
        ByteVec data   = mSerializer.serialize(doc);
        Offset  offset = mBin.write(data);
        mIndex[id]     = offset;
    }
    mBin.flush();
    mWal.truncate();
}

void DocumentStore::recover() {
    for (const auto& e : mWal.readAll()) {
        uint64_t id = std::stoull(e.key);
        if (e.opCode == 0x01 || e.opCode == 0x03) {
            mDocs[id] = e.value;
            if (id >= mNextId) mNextId = id + 1;
        } else if (e.opCode == 0x02) {
            mDocs.erase(id);
            mIndex.erase(id);
        }
    }
}

void DocumentStore::unload() {
    checkpoint();
    mDocs.clear();
    mIndex.clear();
    mBin.close();
    mWal.close();
}

// -----------------------------------------------------------------------------
// GraphStore
// -----------------------------------------------------------------------------

void GraphStore::init(const std::string& binPath, const std::string& walPath) {
    mBin.open(binPath);
    mWal.open(walPath);
}

NodeId GraphStore::addNode(const Value& data) {
    NodeId id  = mNextId++;
    mGraph[id] = { id, data, {} };
    mWal.append({ 0x01, std::to_string(id), data });
    return id;
}

void GraphStore::addEdge(NodeId src, NodeId dst, const Value& edgeData) {
    if (mGraph.find(src) == mGraph.end() || mGraph.find(dst) == mGraph.end())
        throw std::runtime_error("node not found");
    mGraph[src].edges.push_back({ dst, edgeData });
    Value ep; ep.type = Type::Object;
    Value dv; dv.type = Type::Int64; dv.asInt = static_cast<int64_t>(dst);
    ep.asObject["dst"]  = dv;
    ep.asObject["data"] = edgeData;
    mWal.append({ 0x04, std::to_string(src), ep });
}

std::optional<Node> GraphStore::getNode(NodeId id) {
    auto it = mGraph.find(id);
    if (it == mGraph.end()) return std::nullopt;
    return it->second;
}

EdgeList GraphStore::getEdges(NodeId id) {
    auto it = mGraph.find(id);
    if (it == mGraph.end()) return {};
    return it->second.edges;
}

std::vector<NodeId> GraphStore::bfs(NodeId start, uint32_t maxDepth) {
    std::vector<NodeId> visited;
    std::unordered_map<NodeId, bool> seen;
    std::vector<std::pair<NodeId, uint32_t>> queue;
    queue.push_back({ start, 0 });
    seen[start] = true;
    size_t head = 0;
    while (head < queue.size()) {
        auto [cur, depth] = queue[head++];
        visited.push_back(cur);
        if (depth >= maxDepth) continue;
        auto it = mGraph.find(cur);
        if (it == mGraph.end()) continue;
        for (const auto& [nb, _] : it->second.edges) {
            if (!seen[nb]) { seen[nb] = true; queue.push_back({ nb, depth + 1 }); }
        }
    }
    return visited;
}

std::vector<NodeId> GraphStore::dfs(NodeId start, uint32_t maxDepth) {
    std::vector<NodeId> visited;
    std::unordered_map<NodeId, bool> seen;
    std::vector<std::pair<NodeId, uint32_t>> stack;
    stack.push_back({ start, 0 });
    while (!stack.empty()) {
        auto [cur, depth] = stack.back(); stack.pop_back();
        if (seen[cur]) continue;
        seen[cur] = true;
        visited.push_back(cur);
        if (depth >= maxDepth) continue;
        auto it = mGraph.find(cur);
        if (it == mGraph.end()) continue;
        for (const auto& [nb, _] : it->second.edges)
            if (!seen[nb]) stack.push_back({ nb, depth + 1 });
    }
    return visited;
}

void GraphStore::removeNode(NodeId id) {
    mGraph.erase(id);
    for (auto& [_, node] : mGraph) {
        auto& e = node.edges;
        e.erase(std::remove_if(e.begin(), e.end(),
            [id](const std::pair<NodeId, Value>& p) { return p.first == id; }), e.end());
    }
    Value t; t.type = Type::Null;
    mWal.append({ 0x02, std::to_string(id), t });
}

void GraphStore::removeEdge(NodeId src, NodeId dst) {
    auto it = mGraph.find(src);
    if (it == mGraph.end()) return;
    auto& e = it->second.edges;
    e.erase(std::remove_if(e.begin(), e.end(),
        [dst](const std::pair<NodeId, Value>& p) { return p.first == dst; }), e.end());
    Value dv; dv.type = Type::Int64; dv.asInt = static_cast<int64_t>(dst);
    mWal.append({ 0x05, std::to_string(src), dv });
}

void GraphStore::checkpoint() {
    for (const auto& [id, node] : mGraph) {
        Value nv; nv.type = Type::Object;
        Value iv; iv.type = Type::Int64; iv.asInt = static_cast<int64_t>(id);
        nv.asObject["id"]   = iv;
        nv.asObject["data"] = node.data;
        Value ev; ev.type = Type::Array;
        for (const auto& [dst, ed] : node.edges) {
            Value e; e.type = Type::Object;
            Value dv; dv.type = Type::Int64; dv.asInt = static_cast<int64_t>(dst);
            e.asObject["dst"]  = dv;
            e.asObject["data"] = ed;
            ev.asArray.push_back(e);
        }
        nv.asObject["edges"] = ev;
        mBin.write(mSerializer.serialize(nv));
    }
    mBin.flush();
    mWal.truncate();
}

void GraphStore::recover() {
    for (const auto& e : mWal.readAll()) {
        NodeId id = std::stoull(e.key);
        if (e.opCode == 0x01) {
            mGraph[id] = { id, e.value, {} };
            if (id >= mNextId) mNextId = id + 1;
        } else if (e.opCode == 0x02) {
            mGraph.erase(id);
        } else if (e.opCode == 0x04) {
            NodeId dst = static_cast<NodeId>(e.value.asObject.at("dst").asInt);
            mGraph[id].edges.push_back({ dst, e.value.asObject.at("data") });
        } else if (e.opCode == 0x05) {
            NodeId dst = static_cast<NodeId>(e.value.asInt);
            auto& edges = mGraph[id].edges;
            edges.erase(std::remove_if(edges.begin(), edges.end(),
                [dst](const std::pair<NodeId, Value>& p) { return p.first == dst; }),
                edges.end());
        }
    }
}

void GraphStore::unload() {
    checkpoint();
    mGraph.clear();
    mBin.close();
    mWal.close();
}

// -----------------------------------------------------------------------------
// TimeSeriesStore
// -----------------------------------------------------------------------------

void TimeSeriesStore::init(const std::string& binPath, const std::string& walPath) {
    mBin.open(binPath);
    mWal.open(walPath);
}

void TimeSeriesStore::insert(const KeyType& key, TimeStamp time, const Value& value) {
    auto& s = mSeries[key];
    TimeEntry entry{ time, value };
    auto it = std::lower_bound(s.begin(), s.end(), entry,
        [](const TimeEntry& a, const TimeEntry& b) { return a.time < b.time; });
    s.insert(it, entry);
    Value packed; packed.type = Type::Object;
    Value tv; tv.type = Type::Int64; tv.asInt = time;
    packed.asObject["time"]  = tv;
    packed.asObject["value"] = value;
    mWal.append({ 0x01, key, packed });
}

std::vector<TimeEntry> TimeSeriesStore::range(const KeyType& key,
                                              TimeStamp from, TimeStamp to) {
    auto it = mSeries.find(key);
    if (it == mSeries.end()) return {};
    std::vector<TimeEntry> results;
    for (const auto& e : it->second) {
        if (e.time >= from && e.time <= to) results.push_back(e);
        if (e.time > to) break;
    }
    return results;
}

std::optional<TimeEntry> TimeSeriesStore::latest(const KeyType& key) {
    auto it = mSeries.find(key);
    if (it == mSeries.end() || it->second.empty()) return std::nullopt;
    return it->second.back();
}

std::vector<TimeEntry> TimeSeriesStore::last(const KeyType& key, uint32_t n) {
    auto it = mSeries.find(key);
    if (it == mSeries.end()) return {};
    const auto& s = it->second;
    uint32_t count = std::min(n, static_cast<uint32_t>(s.size()));
    return std::vector<TimeEntry>(s.end() - count, s.end());
}

void TimeSeriesStore::purge(const KeyType& key, TimeStamp olderThan) {
    auto it = mSeries.find(key);
    if (it == mSeries.end()) return;
    auto& s = it->second;
    s.erase(std::remove_if(s.begin(), s.end(),
        [olderThan](const TimeEntry& e) { return e.time < olderThan; }), s.end());
    Value tv; tv.type = Type::Int64; tv.asInt = olderThan;
    mWal.append({ 0x02, key, tv });
}

void TimeSeriesStore::checkpoint() {
    for (const auto& [key, series] : mSeries) {
        for (const auto& e : series) {
            Value packed; packed.type = Type::Object;
            Value tv; tv.type = Type::Int64; tv.asInt = e.time;
            packed.asObject["time"]  = tv;
            packed.asObject["value"] = e.value;
            ByteVec kb = mSerializer.serializeString(key);
            ByteVec vb = mSerializer.serialize(packed);
            ByteVec combined;
            combined.insert(combined.end(), kb.begin(), kb.end());
            combined.insert(combined.end(), vb.begin(), vb.end());
            mBin.write(combined);
        }
    }
    mBin.flush();
    mWal.truncate();
}

void TimeSeriesStore::recover() {
    for (const auto& e : mWal.readAll()) {
        if (e.opCode == 0x01) {
            TimeStamp t = e.value.asObject.at("time").asInt;
            insert(e.key, t, e.value.asObject.at("value"));
        } else if (e.opCode == 0x02) {
            purge(e.key, e.value.asInt);
        }
    }
}

void TimeSeriesStore::unload() {
    checkpoint();
    mSeries.clear();
    mBin.close();
    mWal.close();
}

// -----------------------------------------------------------------------------
// Database
// -----------------------------------------------------------------------------


Database::Database(const std::string& name, const std::string& dataDir,
                   std::vector<Engine> engines)
    : mName(name), mDataDir(dataDir), mActiveEngines(std::move(engines)) {}

void Database::initEngines() {
    std::string base = mDataDir + "/" + mName;
    for (Engine engine : mActiveEngines) {
        switch (engine) {
            case Engine::KeyValue:
                mKv = std::make_unique<KeyValueStore>();
                mKv->init(base + "_kv.bin", base + "_kv.wal");
                mKv->recover();
                break;
            case Engine::Document:
                mDocs = std::make_unique<DocumentStore>();
                mDocs->init(base + "_doc.bin", base + "_doc.wal");
                mDocs->recover();
                break;
            case Engine::Graph:
                mGraph = std::make_unique<GraphStore>();
                mGraph->init(base + "_graph.bin", base + "_graph.wal");
                mGraph->recover();
                break;
            case Engine::TimeSeries:
                mTs = std::make_unique<TimeSeriesStore>();
                mTs->init(base + "_ts.bin", base + "_ts.wal");
                mTs->recover();
                break;
        }
    }
}

void Database::open() {
    if (mOpen) return;
    initEngines();
    mOpen = true;
}

void Database::close() {
    if (!mOpen) return;
    if (mKv)    mKv->unload();
    if (mDocs)  mDocs->unload();
    if (mGraph) mGraph->unload();
    if (mTs)    mTs->unload();
    mOpen = false;
}

void Database::checkpoint() {
    if (mKv)    mKv->checkpoint();
    if (mDocs)  mDocs->checkpoint();
    if (mGraph) mGraph->checkpoint();
    if (mTs)    mTs->checkpoint();
}

KeyValueStore&   Database::kv()    { if (!mKv)    throw std::runtime_error("kv engine not active");    return *mKv; }
DocumentStore&   Database::docs()  { if (!mDocs)  throw std::runtime_error("doc engine not active");   return *mDocs; }
GraphStore&      Database::graph() { if (!mGraph) throw std::runtime_error("graph engine not active"); return *mGraph; }
TimeSeriesStore& Database::ts()    { if (!mTs)    throw std::runtime_error("ts engine not active");    return *mTs; }
std::string      Database::name()  const { return mName; }

bool Database::hasEngine(Engine engine) const {
    return std::find(mActiveEngines.begin(), mActiveEngines.end(), engine) != mActiveEngines.end();
}

// -----------------------------------------------------------------------------
// PolyServer helpers
// -----------------------------------------------------------------------------

std::string PolyServer::decodeBase64(const std::string& encoded) {
    const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : encoded) {
        if (c == '=') break;
        auto pos = chars.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + static_cast<int>(pos);
        bits += 6;
        if (bits >= 0) {
            out += static_cast<char>((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return out;
}

AuthResult PolyServer::extractAuth(
        const std::unordered_map<std::string,std::string>& headers) {
    auto it = headers.find("authorization");
    if (it == headers.end())
        return { false, false, "", "missing authorization header" };
    const std::string& val = it->second;
    if (val.substr(0, 6) != "Basic ")
        return { false, false, "", "only Basic auth supported" };
    std::string decoded = decodeBase64(val.substr(6));
    size_t colon = decoded.find(':');
    if (colon == std::string::npos)
        return { false, false, "", "malformed credentials" };
    std::string username = decoded.substr(0, colon);
    std::string password = decoded.substr(colon + 1);
    return mAuth.authenticate(username, password);
}

std::string PolyServer::jsonError(const std::string& msg) {
    return "{\"ok\":false,\"error\":\"" + msg + "\"}";
}

std::string PolyServer::jsonOk(const std::string& msg) {
    return "{\"ok\":true,\"message\":\"" + msg + "\"}";
}

std::string PolyServer::toJson(const Value& value) {
    switch (value.type) {
        case Type::Null:   return "null";
        case Type::Bool:   return value.asBool ? "true" : "false";
        case Type::Int64:  return std::to_string(value.asInt);
        case Type::Double: {
            std::ostringstream oss;
            oss << std::setprecision(10) << value.asDouble;
            return oss.str();
        }
        case Type::String: return "\"" + value.asString + "\"";
        case Type::Array: {
            std::string out = "[";
            for (size_t i = 0; i < value.asArray.size(); i++) {
                if (i > 0) out += ",";
                out += toJson(value.asArray[i]);
            }
            return out + "]";
        }
        case Type::Object: {
            std::string out = "{";
            bool first = true;
            for (const auto& [k, v] : value.asObject) {
                if (!first) out += ",";
                out += "\"" + k + "\":" + toJson(v);
                first = false;
            }
            return out + "}";
        }
    }
    return "null";
}

std::string PolyServer::toJsonArray(const std::vector<Value>& values) {
    std::string out = "[";
    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) out += ",";
        out += toJson(values[i]);
    }
    return out + "]";
}

std::string PolyServer::toJsonIds(const std::vector<NodeId>& ids) {
    std::string out = "[";
    for (size_t i = 0; i < ids.size(); i++) {
        if (i > 0) out += ",";
        out += std::to_string(ids[i]);
    }
    return out + "]";
}

Value PolyServer::parseJson(const std::string& json) {
    Value v;
    std::string trimmed = json;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

    if (trimmed == "null")  { v.type = Type::Null;  return v; }
    if (trimmed == "true")  { v.type = Type::Bool; v.asBool = true;  return v; }
    if (trimmed == "false") { v.type = Type::Bool; v.asBool = false; return v; }

    if (!trimmed.empty() && trimmed[0] == '"') {
        v.type     = Type::String;
        v.asString = trimmed.substr(1, trimmed.size() - 2);
        return v;
    }

    if (!trimmed.empty() && (std::isdigit(trimmed[0]) || trimmed[0] == '-')) {
        if (trimmed.find('.') != std::string::npos) {
            v.type     = Type::Double;
            v.asDouble = std::stod(trimmed);
        } else {
            v.type  = Type::Int64;
            v.asInt = std::stoll(trimmed);
        }
        return v;
    }

    if (!trimmed.empty() && trimmed[0] == '{') {
        v.type = Type::Object;
        std::string inner = trimmed.substr(1, trimmed.size() - 2);
        size_t pos = 0;
        while (pos < inner.size()) {
            while (pos < inner.size() && (inner[pos] == ' ' || inner[pos] == ',')) pos++;
            if (pos >= inner.size() || inner[pos] != '"') break;
            size_t keyEnd = inner.find('"', pos + 1);
            std::string key = inner.substr(pos + 1, keyEnd - pos - 1);
            pos = keyEnd + 1;
            while (pos < inner.size() && inner[pos] != ':') pos++;
            pos++;
            while (pos < inner.size() && inner[pos] == ' ') pos++;
            size_t valStart = pos;
            int depth = 0;
            bool inStr = false;
            while (pos < inner.size()) {
                char c = inner[pos];
                if (c == '"' && (pos == 0 || inner[pos-1] != '\\')) inStr = !inStr;
                if (!inStr) {
                    if (c == '{' || c == '[') depth++;
                    else if (c == '}' || c == ']') depth--;
                    else if (c == ',' && depth == 0) break;
                }
                pos++;
            }
            std::string valStr = inner.substr(valStart, pos - valStart);
            v.asObject[key] = parseJson(valStr);
        }
        return v;
    }

    return v;
}

Database* PolyServer::findDb(const std::string& name) {
    auto it = mDatabases.find(name);
    if (it == mDatabases.end()) return nullptr;
    return &it->second;
}

Database& PolyServer::createDb(const std::string& name, std::vector<Engine> engines) {
    std::lock_guard<std::mutex> lock(mDbMutex);
    mDatabases.emplace(name, Database(name, mDataDir, engines));
    mDatabases.at(name).open();
    return mDatabases.at(name);
}
// -----------------------------------------------------------------------------
// PolyServer route registration
// -----------------------------------------------------------------------------

PolyServer::PolyServer(const std::string& dataDir, int port)
    : mDataDir(dataDir), mPort(port) {
    mAuth.open(dataDir);
}

void PolyServer::start() {
    httplib::Server svr;

    // -- DB management --------------------------------------------------------
    // POST /db/create  body: {"name":"mydb","preset":"AgentMemory"}
    svr.Post("/db/create", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        if (!auth.isAdmin) { res.status = 403; res.set_content(jsonError("admin required"), "application/json"); return; }
        Value body = parseJson(req.body);
        std::string dbName = body.asObject.count("name") ? body.asObject.at("name").asString : "";
        if (dbName.empty()) { res.status = 400; res.set_content(jsonError("name required"), "application/json"); return; }
        std::string engineStr = body.asObject.count("engine") ? body.asObject.at("engine").asString : "";
        std::vector<Engine> engines;
        if      (engineStr == "kv")    engines = { Engine::KeyValue };
        else if (engineStr == "doc")   engines = { Engine::Document };
        else if (engineStr == "graph") engines = { Engine::Graph };
        else if (engineStr == "ts")    engines = { Engine::TimeSeries };
        else { res.status = 400; res.set_content(jsonError("engine required: kv, doc, graph, ts"), "application/json"); return; }

        std::lock_guard<std::mutex> lock(mDbMutex);
        mDatabases.emplace(dbName, Database(dbName, mDataDir, engines));
        mDatabases.at(dbName).open();
        res.set_content(jsonOk("database " + dbName + " created"), "application/json");
    });

    // GET /db/list
    svr.Get("/db/list", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string out = "[";
        bool first = true;
        for (const auto& [name, _] : mDatabases) {
            if (!first) out += ",";
            out += "\"" + name + "\"";
            first = false;
        }
        out += "]";
        res.set_content("{\"ok\":true,\"databases\":" + out + "}", "application/json");
    });

    // -- Auth management ------------------------------------------------------
    // POST /auth/user/create  body: {"username":"alice","password":"secret","isAdmin":false}
    svr.Post("/auth/user/create", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        Value body = parseJson(req.body);
        std::string newUser = body.asObject.count("username") ? body.asObject.at("username").asString : "";
        std::string pass    = body.asObject.count("password") ? body.asObject.at("password").asString : "";
        bool isAdmin = body.asObject.count("isAdmin") && body.asObject.at("isAdmin").asBool;
        if (newUser.empty() || pass.empty()) { res.status = 400; res.set_content(jsonError("username and password required"), "application/json"); return; }
        bool ok = mAuth.createUser(auth.username, newUser, pass, isAdmin);
        if (!ok) { res.status = 403; res.set_content(jsonError("failed — not admin or user exists"), "application/json"); return; }
        res.set_content(jsonOk("user " + newUser + " created"), "application/json");
    });

    // POST /auth/user/delete  body: {"username":"alice"}
    svr.Post("/auth/user/delete", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        Value body = parseJson(req.body);
        std::string target = body.asObject.count("username") ? body.asObject.at("username").asString : "";
        bool ok = mAuth.deleteUser(auth.username, target);
        if (!ok) { res.status = 403; res.set_content(jsonError("failed — not admin or cannot delete sys"), "application/json"); return; }
        res.set_content(jsonOk("user " + target + " deleted"), "application/json");
    });

    // POST /auth/user/passwd  body: {"username":"alice","password":"newpass"}
    svr.Post("/auth/user/passwd", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        Value body = parseJson(req.body);
        std::string target = body.asObject.count("username") ? body.asObject.at("username").asString : "";
        std::string pass   = body.asObject.count("password") ? body.asObject.at("password").asString : "";
        bool ok = mAuth.changePassword(auth.username, target, pass);
        if (!ok) { res.status = 403; res.set_content(jsonError("failed"), "application/json"); return; }
        res.set_content(jsonOk("password changed"), "application/json");
    });

    // POST /auth/user/grant  body: {"username":"alice","db":"mydb","level":"write"}
    svr.Post("/auth/user/grant", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        Value body   = parseJson(req.body);
        std::string target = body.asObject.count("username") ? body.asObject.at("username").asString : "";
        std::string db     = body.asObject.count("db")       ? body.asObject.at("db").asString       : "";
        std::string level  = body.asObject.count("level")    ? body.asObject.at("level").asString    : "read";
        Permission perm = Permission::Read;
        if (level == "write") perm = Permission::Write;
        if (level == "admin") perm = Permission::Admin;
        bool ok = mAuth.grantPermission(auth.username, target, db, perm);
        if (!ok) { res.status = 403; res.set_content(jsonError("failed"), "application/json"); return; }
        res.set_content(jsonOk("granted " + level + " on " + db + " to " + target), "application/json");
    });

    // GET /auth/users
    svr.Get("/auth/users", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        auto users = mAuth.listUsers(auth.username);
        std::string out = "[";
        for (size_t i = 0; i < users.size(); i++) {
            if (i > 0) out += ",";
            out += "\"" + users[i] + "\"";
        }
        out += "]";
        res.set_content("{\"ok\":true,\"users\":" + out + "}", "application/json");
    });

    // -- KV routes ------------------------------------------------------------
    // POST /kv/:db/set   body: {"key":"k","value":<val>}
    svr.Post("/kv/:db/set", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Write);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        Value body = parseJson(req.body);
        std::string key = body.asObject.count("key") ? body.asObject.at("key").asString : "";
        if (key.empty() || !body.asObject.count("value")) { res.status = 400; res.set_content(jsonError("key and value required"), "application/json"); return; }
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        db->kv().set(key, body.asObject.at("value"));
        res.set_content(jsonOk("set"), "application/json");
    });

    // GET /kv/:db/get/:key
    svr.Get("/kv/:db/get/:key", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Read);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        std::string key = req.params.at("key");
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        auto val = db->kv().get(key);
        if (!val) { res.status = 404; res.set_content(jsonError("key not found"), "application/json"); return; }
        res.set_content("{\"ok\":true,\"value\":" + toJson(*val) + "}", "application/json");
    });

    // DELETE /kv/:db/remove/:key
    svr.Delete("/kv/:db/remove/:key", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Write);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        db->kv().remove(req.params.at("key"));
        res.set_content(jsonOk("removed"), "application/json");
    });

    // GET /kv/:db/keys
    svr.Get("/kv/:db/keys", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Read);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        auto keys = db->kv().keys();
        std::string out = "[";
        for (size_t i = 0; i < keys.size(); i++) { if (i > 0) out += ","; out += "\"" + keys[i] + "\""; }
        res.set_content("{\"ok\":true,\"keys\":" + out + "]}", "application/json");
    });

    // -- Document routes ------------------------------------------------------
    // POST /doc/:db/insert   body: <json object>
    svr.Post("/doc/:db/insert", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Write);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        Value doc = parseJson(req.body);
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        uint64_t id = db->docs().insert(doc);
        res.set_content("{\"ok\":true,\"id\":" + std::to_string(id) + "}", "application/json");
    });

    // GET /doc/:db/get/:id
    svr.Get("/doc/:db/get/:id", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Read);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        uint64_t id = std::stoull(req.params.at("id"));
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        auto doc = db->docs().findById(id);
        if (!doc) { res.status = 404; res.set_content(jsonError("document not found"), "application/json"); return; }
        res.set_content("{\"ok\":true,\"document\":" + toJson(*doc) + "}", "application/json");
    });

    // POST /doc/:db/find   body: {"field":"name","value":"Alice"}
    svr.Post("/doc/:db/find", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Read);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        Value body  = parseJson(req.body);
        std::string field = body.asObject.count("field") ? body.asObject.at("field").asString : "";
        Value val = body.asObject.count("value") ? body.asObject.at("value") : Value{};
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        auto results = db->docs().findWhere(field, val);
        res.set_content("{\"ok\":true,\"results\":" + toJsonArray(results) + "}", "application/json");
    });

    // PUT /doc/:db/update/:id   body: <json object>
    svr.Put("/doc/:db/update/:id", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Write);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        uint64_t id = std::stoull(req.params.at("id"));
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        db->docs().update(id, parseJson(req.body));
        res.set_content(jsonOk("updated"), "application/json");
    });

    // DELETE /doc/:db/remove/:id
    svr.Delete("/doc/:db/remove/:id", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Write);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        db->docs().remove(std::stoull(req.params.at("id")));
        res.set_content(jsonOk("removed"), "application/json");
    });

    // -- Graph routes ---------------------------------------------------------
    // POST /graph/:db/node/add   body: <json value for node data>
    svr.Post("/graph/:db/node/add", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Write);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        NodeId id = db->graph().addNode(parseJson(req.body));
        res.set_content("{\"ok\":true,\"id\":" + std::to_string(id) + "}", "application/json");
    });

    // POST /graph/:db/edge/add   body: {"src":0,"dst":1,"data":{}}
    svr.Post("/graph/:db/edge/add", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Write);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        Value body = parseJson(req.body);
        NodeId src = static_cast<NodeId>(body.asObject.at("src").asInt);
        NodeId dst = static_cast<NodeId>(body.asObject.at("dst").asInt);
        Value edgeData = body.asObject.count("data") ? body.asObject.at("data") : Value{};
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        db->graph().addEdge(src, dst, edgeData);
        res.set_content(jsonOk("edge added"), "application/json");
    });

    // GET /graph/:db/node/:id
    svr.Get("/graph/:db/node/:id", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Read);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        NodeId id  = std::stoull(req.params.at("id"));
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        auto   node = db->graph().getNode(id);
        if (!node) { res.status = 404; res.set_content(jsonError("node not found"), "application/json"); return; }
        res.set_content("{\"ok\":true,\"node\":" + toJson(node->data) + "}", "application/json");
    });

    // GET /graph/:db/bfs/:id/:depth
    svr.Get("/graph/:db/bfs/:id/:depth", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Read);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        NodeId   id    = std::stoull(req.params.at("id"));
        uint32_t depth = static_cast<uint32_t>(std::stoul(req.params.at("depth")));
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        auto nodes = db->graph().bfs(id, depth);
        res.set_content("{\"ok\":true,\"nodes\":" + toJsonIds(nodes) + "}", "application/json");
    });

    // GET /graph/:db/dfs/:id/:depth
    svr.Get("/graph/:db/dfs/:id/:depth", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Read);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        NodeId   id    = std::stoull(req.params.at("id"));
        uint32_t depth = static_cast<uint32_t>(std::stoul(req.params.at("depth")));
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        auto nodes = db->graph().dfs(id, depth);
        res.set_content("{\"ok\":true,\"nodes\":" + toJsonIds(nodes) + "}", "application/json");
    });

    // DELETE /graph/:db/node/:id
    svr.Delete("/graph/:db/node/:id", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Write);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        db->graph().removeNode(std::stoull(req.params.at("id")));
        res.set_content(jsonOk("node removed"), "application/json");
    });

    // -- TimeSeries routes ----------------------------------------------------
    // POST /ts/:db/insert   body: {"key":"cpu","time":1700000000,"value":98.6}
    svr.Post("/ts/:db/insert", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Write);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        Value body = parseJson(req.body);
        std::string key = body.asObject.at("key").asString;
        TimeStamp   time = body.asObject.at("time").asInt;
        Value       val  = body.asObject.at("value");
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        db->ts().insert(key, time, val);
        res.set_content(jsonOk("inserted"), "application/json");
    });

    // GET /ts/:db/latest/:key
    svr.Get("/ts/:db/latest/:key", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Read);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        auto entry = db->ts().latest(req.params.at("key"));
        if (!entry) { res.status = 404; res.set_content(jsonError("no entries"), "application/json"); return; }
        res.set_content("{\"ok\":true,\"time\":" + std::to_string(entry->time) +
                        ",\"value\":" + toJson(entry->value) + "}", "application/json");
    });

    // POST /ts/:db/range   body: {"key":"cpu","from":1700000000,"to":1700001000}
    svr.Post("/ts/:db/range", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Read);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        Value body = parseJson(req.body);
        std::string key = body.asObject.at("key").asString;
        TimeStamp from  = body.asObject.at("from").asInt;
        TimeStamp to    = body.asObject.at("to").asInt;
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        auto entries = db->ts().range(key, from, to);
        std::string out = "[";
        for (size_t i = 0; i < entries.size(); i++) {
            if (i > 0) out += ",";
            out += "{\"time\":" + std::to_string(entries[i].time) +
                   ",\"value\":" + toJson(entries[i].value) + "}";
        }
        out += "]";
        res.set_content("{\"ok\":true,\"entries\":" + out + "}", "application/json");
    });

    // DELETE /ts/:db/purge   body: {"key":"cpu","olderThan":1700000000}
    svr.Delete("/ts/:db/purge", [&](const httplib::Request& req, httplib::Response& res) {
        AuthResult auth = extractAuth(req.headers);
        if (!auth.ok) { res.status = 401; res.set_content(jsonError(auth.reason), "application/json"); return; }
        std::string dbName = req.params.at("db");
        AuthResult perm = mAuth.authorize(auth.username, dbName, Permission::Write);
        if (!perm.ok) { res.status = 403; res.set_content(jsonError(perm.reason), "application/json"); return; }
        Value body = parseJson(req.body);
        std::string key = body.asObject.at("key").asString;
        TimeStamp olderThan = body.asObject.at("olderThan").asInt;
        Database* db = findDb(dbName);
        if (!db) { res.status = 404; res.set_content(jsonError("database not found"), "application/json"); return; }
        db->ts().purge(key, olderThan);
        res.set_content(jsonOk("purged"), "application/json");
    });

    std::cout << "[polystore] server running on port " << mPort << "\n";
    std::cout << "[polystore] default login: sys / sys\n";
    svr.listen("0.0.0.0", mPort);
}

// -----------------------------------------------------------------------------
// Cli
// -----------------------------------------------------------------------------

Cli::Cli(const std::string& dataDir) : mDataDir(dataDir) {
    mAuth.open(dataDir);
}

bool Cli::login() {
    std::string user, pass;
    std::cout << "username: "; std::getline(std::cin, user);
    std::cout << "password: "; std::getline(std::cin, pass);
    AuthResult result = mAuth.authenticate(user, pass);
    if (!result.ok) { std::cout << "login failed: " << result.reason << "\n"; return false; }
    mCurrentUser = user;
    mLoggedIn    = true;
    std::cout << "logged in as " << user << "\n";
    return true;
}

void Cli::printHelp() {
    std::cout <<
        "commands:\n"
        "  db create <name> [preset]     — create a database\n"
        "  db list                       — list all databases\n"
        "  kv <db> set <key> <value>     — set a key-value pair\n"
        "  kv <db> get <key>             — get a value by key\n"
        "  kv <db> del <key>             — delete a key\n"
        "  doc <db> insert <json>        — insert a document\n"
        "  doc <db> get <id>             — get document by id\n"
        "  doc <db> find <field> <value> — find documents by field\n"
        "  graph <db> addnode <json>     — add a graph node\n"
        "  graph <db> addedge <s> <d>   — add edge from s to d\n"
        "  graph <db> bfs <id> <depth>  — BFS traversal\n"
        "  ts <db> insert <k> <t> <v>   — insert time series entry\n"
        "  ts <db> latest <key>         — get latest entry\n"
        "  user create <u> <p> [admin]  — create a user (admin only)\n"
        "  user passwd <u> <p>          — change password\n"
        "  user grant <u> <db> <level>  — grant read/write/admin\n"
        "  user list                    — list all users\n"
        "  checkpoint                   — force checkpoint all dbs\n"
        "  help                         — show this\n"
        "  exit                         — quit\n";
}

void Cli::handleCommand(const std::string& line) {
    std::istringstream ss(line);
    std::vector<std::string> tokens;
    std::string token;
    while (ss >> token) tokens.push_back(token);
    if (tokens.empty()) return;

    const std::string& cmd = tokens[0];

    if (cmd == "help") { printHelp(); return; }

    if (cmd == "db") {
        if (tokens.size() < 2) { std::cout << "usage: db create <name> | db list\n"; return; }
        if (tokens[1] == "list") {
            for (const auto& [name, _] : mDatabases) std::cout << "  " << name << "\n";
            return;
        }
        if (tokens[1] == "create" && tokens.size() >= 3) {
            std::string name = tokens[2];
            AuthResult whoami = mAuth.authenticate(mCurrentUser, "");
            // re-authenticate without password check — just check isAdmin flag via listUsers trick
            auto users = mAuth.listUsers(mCurrentUser);
            if (users.empty()) {
                std::cout << "error: admin required to create databases\n"; return;
            }
            std::string eng = tokens.size() >= 4 ? tokens[3] : "";
            std::vector<Engine> engines;
            if      (eng == "kv")    engines = { Engine::KeyValue };
            else if (eng == "doc")   engines = { Engine::Document };
            else if (eng == "graph") engines = { Engine::Graph };
            else if (eng == "ts")    engines = { Engine::TimeSeries };
            else { std::cout << "error: engine required — kv, doc, graph, ts\n"; return; }

            Database db (name, mDataDir, engines);
            db.open();
            mDatabases.emplace(name, std::move(db));
            return;
        }
    }
    if (cmd == "kv" && tokens.size() >= 4) {
        std::string dbName = tokens[1];
        AuthResult perm = mAuth.authorize(mCurrentUser, dbName,
            tokens[2] == "get" ? Permission::Read : Permission::Write);
        if (!perm.ok) { std::cout << "error: " << perm.reason << "\n"; return; }
        auto it = mDatabases.find(dbName);
        if (it == mDatabases.end()) { std::cout << "error: db not found\n"; return; }
        if (tokens[2] == "set" && tokens.size() >= 5) {
            Value v; v.type = Type::String; v.asString = tokens[4];
            it->second.kv().set(tokens[3], v);
            std::cout << "ok\n";
        } else if (tokens[2] == "get") {
            auto val = it->second.kv().get(tokens[3]);
            if (!val) { std::cout << "(not found)\n"; return; }
            if (val->type == Type::String) std::cout << val->asString << "\n";
            else if (val->type == Type::Int64) std::cout << val->asInt << "\n";
            else std::cout << "(value)\n";
        } else if (tokens[2] == "del") {
            it->second.kv().remove(tokens[3]);
            std::cout << "ok\n";
        }
        return;
    }

    if (cmd == "user" && tokens.size() >= 2) {
        if (tokens[1] == "list") {
            auto users = mAuth.listUsers(mCurrentUser);
            for (const auto& u : users) std::cout << "  " << u << "\n";
            return;
        }
        if (tokens[1] == "create" && tokens.size() >= 4) {
            bool isAdmin = tokens.size() >= 5 && tokens[4] == "admin";
            bool ok = mAuth.createUser(mCurrentUser, tokens[2], tokens[3], isAdmin);
            std::cout << (ok ? "user created\n" : "error: failed\n");
            return;
        }
        if (tokens[1] == "passwd" && tokens.size() >= 4) {
            bool ok = mAuth.changePassword(mCurrentUser, tokens[2], tokens[3]);
            std::cout << (ok ? "password changed\n" : "error: failed\n");
            return;
        }
        if (tokens[1] == "grant" && tokens.size() >= 5) {
            Permission p = Permission::Read;
            if (tokens[4] == "write") p = Permission::Write;
            if (tokens[4] == "admin") p = Permission::Admin;
            bool ok = mAuth.grantPermission(mCurrentUser, tokens[2], tokens[3], p);
            std::cout << (ok ? "granted\n" : "error: failed\n");
            return;
        }
    }

    if (cmd == "checkpoint") {
        for (auto& [name, db] : mDatabases) { db.checkpoint(); std::cout << "checkpointed " << name << "\n"; }
        return;
    }

    std::cout << "unknown command — type 'help'\n";
}

void Cli::run() {
    // std::cout << "polystore cli v" << static_cast<int>(kVersion) << "\n";
    if (!login()) return;
    printHelp();
    std::string line;
    while (true) {
        std::cout << mCurrentUser << "> ";
        if (!std::getline(std::cin, line)) break;
        if (line == "exit" || line == "quit") break;
        handleCommand(line);
    }
    for (auto& [name, db] : mDatabases) db.close();
    std::cout << "bye\n";
}

} // namespace polystore
