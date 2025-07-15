#include "main.hpp"
#include <cstdlib>
#include <cstring>
#include <list>
#include <sys/socket.h>
#include <unistd.h>
#include <mutex>

using std::cout;
using std::cerr;
using std::string;
using std::list;
using std::ref;
using std::thread;
using std::to_string;
using std::mutex;
using std::strncmp;
using std::size_t;

struct client_info {
    int sock = -1;
    sockaddr_in addr {};
    socklen_t addr_len = sizeof(addr);
    bool quit_reader = false;
    bool reader_failure = false;
    thread worker_thread;
};

void hardware_concurrency_worker(client_info& client, list<client_info>& all, mutex& all_lock) {
    static mutex out_lock = mutex();

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

        if (str == ".exit") {
            break;
        }

        string out = to_string(client.addr) + ' ' + str;
        out_lock.lock();
        cout << out << '\n';
        all_lock.lock();
        for (client_info& to_send : all) {
            if (to_send.sock == -1) {
                continue;
            }

            if (send(to_send.sock, out.c_str(), out.size() + 1, 0) == -1) {
                string call = to_string(to_send.addr) + " send(...)";
                errno_to_cerr(call.c_str());
            }
        }
        all_lock.unlock();
        out_lock.unlock();
        str.clear();
    }
    client.quit_reader = true;
}

int hardware_concurrency_limit(int listener) {
    list<client_info> clients;
    mutex clients_lock = mutex();

    while (true) {
        using namespace std::chrono_literals;

        for (auto it = clients.begin(); it != clients.end();) {
            client_info& client = *it;
            if (client.quit_reader || client.reader_failure) {
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

        clients_lock.lock();
        clients.push_back({});
        clients_lock.unlock();

        client_info& client = clients.back();
        client.sock = accept(listener, reinterpret_cast<sockaddr*>(&client.addr), &client.addr_len);

        if (client.sock == -1) {
            if (errno != EWOULDBLOCK) {
                errno_to_cerr("accept(...)");
                return EXIT_FAILURE;
            }
            std::this_thread::sleep_for(50ms);
            clients_lock.lock();
            clients.pop_back();
            clients_lock.unlock();
            continue;
        }

        client.worker_thread = thread(hardware_concurrency_worker, ref(client), ref(clients), ref(clients_lock));
    }

    return EXIT_SUCCESS;
}

int asynchronous_workers(int listener) {
    cerr << "This method is not implemented.\n";
    return EXIT_FAILURE;
}

constexpr const char HARDWARE_METHOD[] = "hardware";
constexpr const size_t HARDWARE_METHOD_LENGTH = sizeof(HARDWARE_METHOD) / sizeof(HARDWARE_METHOD[0]);

constexpr const char ASYNC_METHOD[] = "async";
constexpr const size_t ASYNC_METHOD_LENGTH = sizeof(ASYNC_METHOD) / sizeof(ASYNC_METHOD[0]);

int server(const char concurrency_method[]) {
    if (concurrency_method == nullptr) {
        cerr << "Must specify either '" << HARDWARE_METHOD << "' or '" << ASYNC_METHOD << "'\n";
        return EXIT_FAILURE;
    }

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
        errno_to_cerr("listen(...)");
        return EXIT_FAILURE;
    }

    if (strncmp(concurrency_method, HARDWARE_METHOD, HARDWARE_METHOD_LENGTH) == 0) {
        return hardware_concurrency_limit(listener);
    } else if (strncmp(concurrency_method, ASYNC_METHOD, ASYNC_METHOD_LENGTH) == 0) {
        return asynchronous_workers(listener);
    }

    cerr << "Must specify either '" << HARDWARE_METHOD << "' or '" << ASYNC_METHOD << "'\n";
    return EXIT_FAILURE;
}
