#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <sstream>
#include <vector>
#include <thread>
#include <mutex>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <cstring>
#include <algorithm>

namespace httplib {

struct Request {
    std::string method;
    std::string path;
    std::string body;
    std::unordered_map<std::string,std::string> headers;
    std::unordered_map<std::string,std::string> params;
    std::vector<std::string> pathParts;
};

struct Response {
    int status = 200;
    std::string body;
    std::unordered_map<std::string,std::string> headers;
    void set_content(const std::string& b, const std::string&) { body = b; }
};

using Handler = std::function<void(const Request&, Response&)>;

struct Route {
    std::string method;
    std::vector<std::string> parts;
    Handler handler;
};

inline std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/'))
        if (!part.empty()) parts.push_back(part);
    return parts;
}

inline bool matchRoute(const std::vector<std::string>& routeParts,
                       const std::vector<std::string>& reqParts,
                       std::unordered_map<std::string,std::string>& params) {
    if (routeParts.size() != reqParts.size()) return false;
    for (size_t i = 0; i < routeParts.size(); i++) {
        if (routeParts[i][0] == ':')
            params[routeParts[i].substr(1)] = reqParts[i];
        else if (routeParts[i] != reqParts[i])
            return false;
    }
    return true;
}

inline std::string statusText(int s) {
    if (s == 200) return "OK";
    if (s == 201) return "Created";
    if (s == 400) return "Bad Request";
    if (s == 401) return "Unauthorized";
    if (s == 403) return "Forbidden";
    if (s == 404) return "Not Found";
    if (s == 409) return "Conflict";
    if (s == 500) return "Internal Server Error";
    return "Unknown";
}

class Server {
public:
    void Get(const std::string& path, Handler h)    { addRoute("GET",    path, h); }
    void Post(const std::string& path, Handler h)   { addRoute("POST",   path, h); }
    void Put(const std::string& path, Handler h)    { addRoute("PUT",    path, h); }
    void Delete(const std::string& path, Handler h) { addRoute("DELETE", path, h); }

    bool listen(const std::string&, int port) {
        int serverFd = socket(AF_INET, SOCK_STREAM, 0);
        if (serverFd < 0) return false;
        int opt = 1;
        setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(port);
        if (bind(serverFd, (sockaddr*)&addr, sizeof(addr)) < 0) return false;
        ::listen(serverFd, 64);
        while (true) {
            int clientFd = accept(serverFd, nullptr, nullptr);
            if (clientFd < 0) continue;
            std::thread([this, clientFd]() { handleClient(clientFd); }).detach();
        }
        return true;
    }

private:
    std::vector<Route> mRoutes;
    std::mutex         mMutex;

    void addRoute(const std::string& method, const std::string& path, Handler h) {
        mRoutes.push_back({ method, splitPath(path), h });
    }

    void handleClient(int fd) {
        char buf[8192] = {};
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) { close(fd); return; }
        std::string raw(buf, n);

        Request req;
        std::istringstream stream(raw);
        std::string line;
        std::getline(stream, line);
        if (line.empty()) { close(fd); return; }
        std::istringstream firstLine(line);
        std::string fullPath;
        firstLine >> req.method >> fullPath;
        size_t qmark = fullPath.find('?');
        req.path = fullPath.substr(0, qmark);
        req.pathParts = splitPath(req.path);

        while (std::getline(stream, line) && line != "\r") {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string val = line.substr(colon + 2);
                val.erase(val.find_last_not_of("\r\n") + 1);
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                req.headers[key] = val;
            }
        }

        std::string bodyBuf;
        while (std::getline(stream, line)) bodyBuf += line + "\n";
        req.body = bodyBuf;

        Response res;
        res.headers["Content-Type"]                 = "application/json";
        res.headers["Access-Control-Allow-Origin"]  = "*";
        res.headers["Access-Control-Allow-Headers"] = "Authorization, Content-Type";

        bool matched = false;
        for (auto& route : mRoutes) {
            if (route.method != req.method) continue;
            std::unordered_map<std::string,std::string> params;
            if (matchRoute(route.parts, req.pathParts, params)) {
                req.params = params;
                try { route.handler(req, res); }
                catch (const std::exception& e) {
                    res.status = 500;
                    res.body   = std::string("{\"error\":\"") + e.what() + "\"}";
                }
                matched = true;
                break;
            }
        }
        if (!matched) { res.status = 404; res.body = "{\"error\":\"not found\"}"; }

        std::ostringstream response;
        response << "HTTP/1.1 " << res.status << " " << statusText(res.status) << "\r\n";
        for (auto& [k, v] : res.headers) response << k << ": " << v << "\r\n";
        response << "Content-Length: " << res.body.size() << "\r\n\r\n" << res.body;
        std::string out = response.str();
        send(fd, out.c_str(), out.size(), 0);
        close(fd);
    }
}; // class Server

} // namespace httplib
