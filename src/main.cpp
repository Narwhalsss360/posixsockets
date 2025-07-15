#include "main.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <cstring>

using std::size_t;
using std::cout;
using std::cerr;
using std::endl;
using std::strncmp;
using std::string;
using std::count;

constexpr const char SERVER_ARGUMENT[] = "server";
constexpr const size_t SERVER_ARGUMENT_LENGTH = sizeof(SERVER_ARGUMENT) / sizeof(SERVER_ARGUMENT[0]) - 1;

constexpr const char CLIENT_ARGUMENT[] = "client";
constexpr const size_t CLIENT_ARGUMENT_LENGTH = sizeof(CLIENT_ARGUMENT) / sizeof(CLIENT_ARGUMENT[0]) - 1;


void reader(int sock, bool& quit_reader, bool& reader_failure) {
    string str;
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

        int line_count = count(str.begin(), str.end(), '\n');
        print_above(line_count);
        cout << str << '\n';
        print_above_restore(line_count);

        if (str == ".exit") {
            break;
        }
        str.clear();
    }
    quit_reader = true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Must specify either:" << SERVER_ARGUMENT << "|" << CLIENT_ARGUMENT << endl;
        return EXIT_FAILURE;
    }

    if (strncmp(argv[1], SERVER_ARGUMENT, SERVER_ARGUMENT_LENGTH) == 0) {
        return server(argc < 3 ? nullptr : argv[2]);
    }

    if (strncmp(argv[1], CLIENT_ARGUMENT, CLIENT_ARGUMENT_LENGTH) == 0) {
        return client(argc < 3 ? nullptr : argv[2]);
    }

    cerr << "Must specify either:" << SERVER_ARGUMENT << "|" << CLIENT_ARGUMENT << endl;
    return EXIT_FAILURE;
}
