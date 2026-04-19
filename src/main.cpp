#include "../include/database.h"
#include <iostream>
#include <filesystem>

int main(int argc, char* argv[]) {
    std::string dataDir = "./data";
    std::filesystem::create_directories(dataDir);

    if (argc >= 2 && std::string(argv[1]) == "--server") {
    polystore::PolyServer server(dataDir, polystore::kDefaultPort);
    server.start();
} else {
    polystore::Cli cli(dataDir);
    cli.run();
}
    return 0;
}