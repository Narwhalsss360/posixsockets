#include "main.hpp"
#include <unistd.h>

using std::cout;
using std::string;
using std::vector;
using std::ref;
using std::thread;
using std::to_string;

struct client_info {
    int sock = -1;
    sockaddr_in addr {};
    socklen_t addr_len = sizeof(addr);
    bool quit_reader = false;
    bool reader_failure = false;
    thread worker_thread;
};

int server() {
    cout << "Serving...\n";

    int listener = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
    if (listener == -1) {
        errno_to_cerr("socket(...)");
        return EXIT_FAILURE;
    }
    defer([&]() {
        if (close(listener) == -1) {
            errno_to_cerr("close(listener)");
        }
    });

    sockaddr_in all;
    all.sin_family = AF_INET;
    all.sin_addr.s_addr = 0;
    all.sin_port = htons(PORT);
    if (bind(listener, reinterpret_cast<sockaddr*>(&all), sizeof(all)) == -1) {
        errno_to_cerr("bind(...)");
        return EXIT_FAILURE;
    }

    if (listen(listener, MAX_CLIENTS)) {
        return EXIT_FAILURE;
        errno_to_cerr("listen(...)");
    }

    size_t client_count = 0;
    vector<client_info> clients;
    clients.reserve(MAX_CLIENTS);

    while (true) {
        using namespace std::chrono_literals;

        for (int i = 0; i < clients.size(); i++) {
            clients[i].worker_thread.join();
            if (clients[i].quit_reader) {
                if (close(clients[i].sock) == -1) {
                    string call = string("close( ") + to_string(clients[i].addr)  + ")";
                    errno_to_cerr(call.c_str());
                }
                clients.erase(clients.begin() + i);
            }
        }

        if (clients.size() == clients.capacity()) {
            std::this_thread::sleep_for(50ms);
            continue;
        }

        clients.push_back({});
        client_info& client = clients.back();
        client.sock = accept(listener, reinterpret_cast<sockaddr*>(&client.addr), &client.addr_len);

        if (client.sock == -1) {
            if (errno != EWOULDBLOCK) {
                errno_to_cerr("accept(...)");
                return EXIT_FAILURE;
            }
            std::this_thread::sleep_for(50ms);
            clients.erase(clients.end() - 1);
            continue;
        }

        client.worker_thread = thread(reader, client.sock, ref(client.quit_reader), ref(client.reader_failure));
    }
    return EXIT_SUCCESS;
}
