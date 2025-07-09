#include <cstdlib>
#include <iostream>
#include <cstring>
#include <functional>

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

using std::cout;
using std::cerr;
using std::endl;
using std::strncmp;

constexpr const char SERVER_ARGUMENT[] = "server";
constexpr const size_t SERVER_ARGUMENT_LENGTH = sizeof(SERVER_ARGUMENT) / sizeof(SERVER_ARGUMENT[0]) - 1;

constexpr const char CLIENT_ARGUMENT[] = "client";
constexpr const size_t CLIENT_ARGUMENT_LENGTH = sizeof(CLIENT_ARGUMENT) / sizeof(CLIENT_ARGUMENT[0]) - 1;

int server() {
    cout << "Serving...\n";

    return EXIT_SUCCESS;
}

int client(const char ip[]) {
    cout << "Client...\n";
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
