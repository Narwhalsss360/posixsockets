#include "main.hpp"
#include <list>
#include <unistd.h>
#include <mutex>

using std::cout;
using std::string;
using std::list;
using std::ref;
using std::thread;
using std::to_string;
using std::mutex;

struct client_info {
    int sock = -1;
    sockaddr_in addr {};
    socklen_t addr_len = sizeof(addr);
    bool quit_reader = false;
    bool reader_failure = false;
    thread worker_thread;
};

void client_worker(client_info& client) {
    static mutex cout_lock = mutex();

    string str;
    constexpr const size_t chunk_size = 16;
    bool resize_required = true;
    client.reader_failure = false;
    while (!client.quit_reader) {
        if (resize_required) {
            str.resize(str.size() + chunk_size);
            resize_required = false;
        }

        int received = recv(client.sock, &*str.end() - chunk_size, chunk_size, 0);

        if (received == -1) {
            if (errno != EWOULDBLOCK) {
                errno_to_cerr("recv(...)");
                client.reader_failure = true;
                return;
            }
            continue;
        } else if (received == 0) {
            break;
        }
        resize_required = true;

        if (received != chunk_size) {
            str.resize(str.size() - chunk_size + received);
        }

        if (str.back() != '\0') {
            continue;
        }

        cout_lock.lock();
        cout << to_string(client.addr) << ' ' << str << '\n';
        cout_lock.unlock();

        if (str == ".exit") {
            break;
        }
        str.clear();
    }
    client.quit_reader = true;
}

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
    list<client_info> clients;

    while (true) {
        using namespace std::chrono_literals;

        for (auto it = clients.begin(); it != clients.end();) {
            client_info& client = *it;
            if (client.quit_reader) {
                client.worker_thread.join();
                if (close(client.sock) == -1) {
                    string call = string("close( ") + to_string(client.addr)  + ")";
                    errno_to_cerr(call.c_str());
                }
                it = clients.erase(it);
            } else {
                ++it;
            }
        }

        if (clients.size() == MAX_CLIENTS) {
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
            clients.pop_back();
            continue;
        }

        client.worker_thread = thread(client_worker, ref(client));
    }
    return EXIT_SUCCESS;
}
