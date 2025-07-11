#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <functional>
#include <netinet/in.h>
#include <sys/types.h>
#include <thread>
#include <vector>

#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <netdb.h>

struct defer_container {
    std::function<void()> function;

    defer_container(std::function<void()> defered)
        : function(defered) {}

    ~defer_container() {
        function();
    }
};

#define __PREPROC_CONCAT(a, b) a##b
#define PREPROC_CONCAT(a, b) __PREPROC_CONCAT(a, b)
#define __defer(line, function) defer_container PREPROC_CONCAT(defer_at_, line) { function }
#define defer(function) __defer(__LINE__, function)

namespace std {
    string to_string(const in_addr& addr) {
        const uint8_t (&bytes) [4] = *reinterpret_cast<const uint8_t(* const)[4]>(&addr.s_addr);
        return
            to_string((int)bytes[0]) + '.' +
            to_string((int)bytes[1]) + '.' +
            to_string((int)bytes[2]) + '.' +
            to_string((int)bytes[3]);
    }

    string to_string(const sockaddr_in& addr) {
        return to_string(addr.sin_addr) + ':' + to_string(ntohs(addr.sin_port));
    }
}

using std::size_t;
using std::cout;
using std::cin;
using std::cerr;
using std::endl;
using std::strncmp;
using std::thread;
using std::ref;
using std::find_if;
using std::vector;
using std::to_string;
using std::string;
using std::count;

constexpr const uint16_t PORT = 54673;

const size_t MAX_CLIENTS = thread::hardware_concurrency();

constexpr const char SERVER_ARGUMENT[] = "server";
constexpr const size_t SERVER_ARGUMENT_LENGTH = sizeof(SERVER_ARGUMENT) / sizeof(SERVER_ARGUMENT[0]) - 1;

constexpr const char CLIENT_ARGUMENT[] = "client";
constexpr const size_t CLIENT_ARGUMENT_LENGTH = sizeof(CLIENT_ARGUMENT) / sizeof(CLIENT_ARGUMENT[0]) - 1;

void errno_to_cerr(const char* const call) {
    const char* const error_name = strerrorname_np(errno);
    if (error_name) {
        cerr << call << "; failure: (" << errno << "): " << error_name << endl;
    } else {
        cerr << call << "; failure: " << errno << endl;
    }
}

constexpr const char* const CONTROL_SEQUENCE_INTRODUCER = "\x1B[";
constexpr const char* const SAVE_CURRENT_CURSOR_POSITION = "s";
constexpr const char* const RESTORE_SAVED_CURRENT_CURSOR_POSITION = "u";
constexpr const char* const INSERT_NEW_LINE = "L";
constexpr const char* const CURSOR_UP = "A";
constexpr const char* const CURSOR_DOWN = "B";

static void print_above(int line_count = 1, bool current_is_empty = false) {
    if (current_is_empty) {
        std::cout <<
            '\n' <<
            CONTROL_SEQUENCE_INTRODUCER << 2 << CURSOR_UP;
    }

    std::cout <<
        CONTROL_SEQUENCE_INTRODUCER << SAVE_CURRENT_CURSOR_POSITION <<
        CONTROL_SEQUENCE_INTRODUCER << line_count << CURSOR_UP <<
        CONTROL_SEQUENCE_INTRODUCER << INSERT_NEW_LINE <<
        std::flush;
}

static void print_above_restore(int line_count = 1, bool current_was_empty = false) {
    std::cout <<
        std::flush <<
        CONTROL_SEQUENCE_INTRODUCER << RESTORE_SAVED_CURRENT_CURSOR_POSITION <<
        CONTROL_SEQUENCE_INTRODUCER << line_count << CURSOR_DOWN <<
        std::flush;
}

void reader(int sock, bool& quit_reader, bool& reader_failure) {
    std::string str;
    constexpr const size_t chunk_size = 16;
    bool resize_required = true;
    reader_failure = false;
    while (!quit_reader) {
        if (resize_required) {
            str.resize(str.size() + chunk_size);
            resize_required = false;
        }

        int received = recv(sock, &*str.end() - chunk_size, chunk_size, 0);

        if (received == -1) {
            if (errno != EWOULDBLOCK) {
                errno_to_cerr("recv(...)");
                reader_failure = true;
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

        int line_count = std::count(str.begin(), str.end(), '\n');
        print_above(line_count);
        std::cout << str << '\n';
        print_above_restore(line_count);

        if (str == ".exit") {
            break;
        }
        str.clear();
    }
    quit_reader = true;
}

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

int client(const char ip[]) {
    cout << "Client...\n";
    int client = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
    if (client == -1) {
        errno_to_cerr("socket(...)");
        return EXIT_FAILURE;
    }
    defer([&]() {
        if (close(client)) {
            errno_to_cerr("close(...)");
        }
    });

    sockaddr_in local;
    local.sin_family = AF_INET;
    hostent* this_host = gethostbyname(ip);
    *reinterpret_cast<uint32_t*>(&local.sin_addr) = *reinterpret_cast<const uint32_t*>(this_host->h_addr_list[0]);
    local.sin_port = htons(PORT);
    cout << "Connecting..." << endl;
    while (true) {
        int result = connect(client, reinterpret_cast<sockaddr*>(&local), sizeof(local));
        if (result == -1) {
            if (errno != EWOULDBLOCK && errno != EINPROGRESS) {
                errno_to_cerr("connect(...)");
                return EXIT_FAILURE;
            }
            continue;
        }
        break;
    }
    cout << "Connected!\n";

    bool quit_reader = false;
    bool reader_failure = false;
    thread reader_thread = thread(reader, client, ref(quit_reader), ref(reader_failure));
    defer([&]() {
        if (reader_failure) {
            errno_to_cerr("reader(...)");
        }

        if (!reader_thread.joinable()) {
            return;
        }
        quit_reader = true;
        reader_thread.join();
    });

    string line;
    do {
        cout << ">";
        getline(cin, line);
        if (quit_reader || reader_failure) {
            break;
        }

        if (send(client, line.c_str(), line.size() + 1, 0) == -1) {
            errno_to_cerr("send(...)");
            return EXIT_FAILURE;
        }
    } while (line != ".exit" && !reader_failure && !quit_reader);

    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Must specify either:" << SERVER_ARGUMENT << "|" << CLIENT_ARGUMENT << endl;
        return EXIT_FAILURE;
    }

    if (strncmp(argv[1], SERVER_ARGUMENT, SERVER_ARGUMENT_LENGTH) == 0) {
        return server();
    }

    if (strncmp(argv[1], CLIENT_ARGUMENT, CLIENT_ARGUMENT_LENGTH) != 0) {
        cerr << "Must specify either:" << SERVER_ARGUMENT << "|" << CLIENT_ARGUMENT << endl;
        return EXIT_FAILURE;
    }

    if (argc < 3) {
        cerr << "Must specify server IP Address." << endl;
        return EXIT_FAILURE;
    }

    return client(argv[2]);
}
