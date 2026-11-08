// Guest file descriptor table.
#pragma once
#include "common.h"

#include <memory>
#include <mutex>

namespace fdtab {

enum class Kind { File, Socket, Null, Random, StdIn, StdOut, StdErr, PipeRead, PipeWrite, Kqueue };

struct Fd {
    Kind kind = Kind::Null;
    void* handle = nullptr;  // HANDLE for files/pipes
    u64 sock = ~0ull;        // SOCKET
    int flags = 0;           // Darwin open flags (O_APPEND, O_NONBLOCK...)
    bool cloexec = false;
    std::string guest_path;
    std::mutex lock;  // protects file position for append writes
    ~Fd();
};

int add(std::shared_ptr<Fd> fd, int min_fd = 0);
std::shared_ptr<Fd> get(int fd);
int close(int fd);
int dup(int fd, int min_fd = 0);
int dup2(int fd, int to);

// I/O helpers usable from stdio implementation (return -1 + errno on error)
s64 read(int fd, void* buf, size_t n);
s64 write(int fd, const void* buf, size_t n);
s64 lseek(int fd, s64 off, int whence);

// Opens a guest path (Darwin flags). Returns guest fd or -1 (errno set).
int open(const char* guest_path, int dflags, int mode);

}  // namespace fdtab
