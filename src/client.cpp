#include "main.hpp"
#include <cstdlib>
#include <netdb.h>
#include <unistd.h>

using std::cout;
using std::cerr;
using std::cin;
using std::endl;
using std::string;
using std::ref;
using std::thread;

int client(const char ip[]) {
    if (ip == nullptr) {
        cerr << "Must specify server IP Address." << endl;
        return EXIT_FAILURE;
    }

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
