#pragma once
#include <functional>
#include <string>
#include <netinet/ip.h>
#include <cstring>
#include <iostream>
#include <thread>

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
    static string to_string(const in_addr& addr) {
        const uint8_t (&bytes) [4] = *reinterpret_cast<const uint8_t(* const)[4]>(&addr.s_addr);
        return
            to_string((int)bytes[0]) + '.' +
            to_string((int)bytes[1]) + '.' +
            to_string((int)bytes[2]) + '.' +
            to_string((int)bytes[3]);
    }

    static string to_string(const sockaddr_in& addr) {
        return to_string(addr.sin_addr) + ':' + to_string(ntohs(addr.sin_port));
    }
}

static void errno_to_cerr(const char* const call) {
    using std::cerr;
    using std::endl;

    const char* const error_name = strerrorname_np(errno);
    if (error_name) {
        std::cerr << call << "; failure: (" << errno << "): " << error_name << endl;
    } else {
        cerr << call << "; failure: " << errno << endl;
    }
}

constexpr const uint16_t PORT = 54673;

const size_t MAX_HARDWARE_CONCURRENCY = std::thread::hardware_concurrency();

constexpr const char* const CONTROL_SEQUENCE_INTRODUCER = "\x1B[";
constexpr const char* const SAVE_CURRENT_CURSOR_POSITION = "s";
constexpr const char* const RESTORE_SAVED_CURRENT_CURSOR_POSITION = "u";
constexpr const char* const INSERT_NEW_LINE = "L";
constexpr const char* const CURSOR_UP = "A";
constexpr const char* const CURSOR_DOWN = "B";

static void print_above(int line_count = 1, bool current_is_empty = false) {
    using std::cout;

    if (current_is_empty) {
        cout <<
            '\n' <<
            CONTROL_SEQUENCE_INTRODUCER << 2 << CURSOR_UP;
    }

    cout <<
        CONTROL_SEQUENCE_INTRODUCER << SAVE_CURRENT_CURSOR_POSITION <<
        CONTROL_SEQUENCE_INTRODUCER << line_count << CURSOR_UP <<
        CONTROL_SEQUENCE_INTRODUCER << INSERT_NEW_LINE <<
        std::flush;
}

static void print_above_restore(int line_count = 1, bool current_was_empty = false) {
    using std::cout;
    using std::flush;

    cout <<
        flush <<
        CONTROL_SEQUENCE_INTRODUCER << RESTORE_SAVED_CURRENT_CURSOR_POSITION <<
        CONTROL_SEQUENCE_INTRODUCER << line_count << CURSOR_DOWN <<
        flush;
}

void reader(int sock, bool& quit_reader, bool& reader_failure);

int server(const char concurrency_method[]);

int client(const char ip[]);

