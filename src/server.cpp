#include "main.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <list>
#include <sys/poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <poll.h>
#include <mutex>
#include <fcntl.h>

using std::cout;
using std::cerr;
using std::string;
using std::list;
using std::vector;
using std::ref;
using std::thread;
using std::this_thread::sleep_for;
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

        if (clients.size() == MAX_HARDWARE_CONCURRENCY) {
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

//Limit variable-length arrays
constexpr const size_t MAX_CONNECTIONS_PER_WORKER = 32;

//References are safe, mutexes not required for 'all' since size shall not change, vector is used because std::thread::hardware_concurrency is a runtime value
struct async_context {
    struct connection {
        int sock = -1;
        sockaddr_in addr = {};
        socklen_t addr_len = sizeof(addr);
    };

    vector<mutex> locks;
    vector<vector<connection>> all;
    mutex echo_lock;

    async_context(const int& worker_count)
        : locks(vector<mutex>(worker_count)), all(worker_count), echo_lock(mutex()) {}
};

bool append_read_until_zero(int sock, string& str) {
    constexpr const size_t chunk_size = 16;
    bool resize_required = true;
    while (true) {
        if (resize_required) {
            str.resize(str.size() + chunk_size);
            resize_required = false;
        }

        int received = recv(sock, &*str.end() - chunk_size, chunk_size, 0);

        if (received == -1) {
            if (errno == EWOULDBLOCK) {
                continue;
            }
            return false;
        } else if (received == 0) {
            return false;
        }
        resize_required = true;

        if (received != chunk_size) {
            str.resize(str.size() - chunk_size + received);
        }

        if (str.back() == '\0') {
            break;
        }
    }
    return true;
}

void asynchronous_worker(async_context& context, const int index) {
    vector<async_context::connection>& connections = context.all[index];
    mutex& connections_lock = context.locks[index];

    while (true) {
        context.echo_lock.lock();
        defer([&]() {
            context.echo_lock.unlock();
        });

        connections_lock.lock();
        defer([&]() {
            using namespace std::chrono_literals;
            connections_lock.unlock();
            sleep_for(50ms);
        });

        const size_t& count = connections.size();
        pollfd pollfds[count];
        for (int i = 0; i < count; i++) {
            pollfds[i].fd = connections[i].sock;
            pollfds[i].events = POLLIN;
        }

        if (poll(pollfds, count, 0) == -1) {
            string call = "worker[" + to_string(index) + "]: poll(...)";
            errno_to_cerr(call.c_str());
            continue;
        }

        vector<int> disconnected;
        for (int i = 0; i < count; i++) {
            if (!(pollfds[i].revents & POLLIN)) {
                continue;
            }

            string message;
            if (!append_read_until_zero(connections[i].sock, message)) {
                string call = "worker[" + to_string(index) + "]: append_read_until_zero(" + to_string(connections[i].addr) + ")";
                errno_to_cerr(call.c_str());
                disconnected.push_back(i);
                continue;
            }

            string out = "(" + to_string(connections[i].addr) + ") " + message;
            for (vector<async_context::connection>& assigned : context.all) {
                int i = 0;
                for (async_context::connection& connection : assigned) {
                    if (send(connection.sock, out.c_str(), out.size() + 1, 0) == -1) {
                        string call = "worker[" + to_string(index) + "]: send(worker[" + to_string(i) + "]: " + to_string(connections[i].addr) + ")";
                        errno_to_cerr(call.c_str());
                    }
                    i++;
                }
            }
            cout << out << '\n';

            if (message.rfind(".exit", 0) == 0) {
                disconnected.push_back(i);
            }
        }

        for (const int& to_erase : disconnected) {
            if (shutdown(connections[to_erase].sock, SHUT_RDWR) == -1) {
                string call = "worker[" + to_string(index) + "]: shutdown(" + to_string(connections[to_erase].addr) + ")";
                errno_to_cerr(call.c_str());
            }
            if (close(connections[to_erase].sock) == -1) {
                string call = "worker[" + to_string(index) + "]: close(" + to_string(connections[to_erase].addr) + ")";
                errno_to_cerr(call.c_str());
            }
            connections.erase(connections.begin() + to_erase);
            cout << to_string(connections[to_erase].addr) << " Disconected\n";
        }
    }
}

int asynchronous_workers(int listener) {
    const size_t MAX_HARDWARE_CONCURRENCY = 1;
    async_context context = async_context(MAX_HARDWARE_CONCURRENCY);
    context.locks[0].lock();
    context.locks[0].unlock();

    thread workers[MAX_HARDWARE_CONCURRENCY];

    for (int i = 0; i < MAX_HARDWARE_CONCURRENCY; i++) {
        workers[i] = thread(asynchronous_worker, ref(context), i);
    }

    int next_assignment_index = 0;
    pollfd listener_poll;
    listener_poll.fd = listener;
    listener_poll.events = POLLIN;
    while (true) {
        if (poll(&listener_poll, 1, 100) == -1) {
            errno_to_cerr("poll(...)");
        }

        if (!(listener_poll.revents & POLLIN)) {
            using namespace std::chrono_literals;
            sleep_for(50ms);
            continue;
        }

        {
            mutex& clients_lock = context.locks[next_assignment_index];
            vector<async_context::connection>& clients = context.all[next_assignment_index];

            clients_lock.lock();
            defer([&]() {
                clients_lock.unlock();
                using namespace std::chrono_literals;
                sleep_for(50ms);
            });
            clients.push_back({});

            async_context::connection& client = clients.back();
            client.sock = accept(listener, reinterpret_cast<sockaddr*>(&client.addr), &client.addr_len);
            if (client.sock == -1) {
                clients.pop_back();
                errno_to_cerr("accept(...)");
                continue;
            }
            cout << to_string(client.addr) << " Connected\n";
        }

        int i = MAX_HARDWARE_CONCURRENCY;
        while (true) {
            for (i = 0; i < MAX_HARDWARE_CONCURRENCY; i++) {
                next_assignment_index = (next_assignment_index + i) % MAX_HARDWARE_CONCURRENCY;
                if (context.all[next_assignment_index].size() != MAX_CONNECTIONS_PER_WORKER) {
                    break;
                }
            }

            if (i == MAX_HARDWARE_CONCURRENCY) {
                cerr << "Server at 100% load\n";
                using namespace std::chrono_literals;
                sleep_for(3s);
            }

            break;
        }
    }

    return EXIT_SUCCESS;
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

    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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

    if (listen(listener, MAX_HARDWARE_CONCURRENCY)) {
        errno_to_cerr("listen(...)");
        return EXIT_FAILURE;
    }

    int fcntl_flags = fcntl(listener, F_GETFL, 0);
    if (fcntl_flags == -1) {
        errno_to_cerr("fcntl(..., F_GETFL, ...)");
        return EXIT_FAILURE;
    }

    fcntl_flags |= O_NONBLOCK;

    if (fcntl(listener, F_SETFL, fcntl_flags) == -1) {
        errno_to_cerr("fcntl(..., F_SETFL, ...)");
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
