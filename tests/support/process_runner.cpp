// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The process is started without a shell and the library call that starts it is the platform's
// own (posix_spawn, CreateProcessW), so an argument reaches the program as given, whatever
// characters it holds.

#include "process_runner.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace bm3dtest {

#ifdef _WIN32

namespace {

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &wide[0], size);
    return wide;
}

// One argument as CommandLineToArgvW reads it back: quoted, with the backslashes before a quote
// doubled
std::wstring quoteArgument(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return argument;
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"')
            quoted.append(backslashes * 2 + 1, L'\\');
        else
            quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(c);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

// Closes a handle when it goes out of scope
class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE h) : h_(h) {}
    ~Handle() { close(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return h_; }
    void reset(HANDLE h) {
        close();
        h_ = h;
    }
    void close() {
        if (h_ != nullptr && h_ != INVALID_HANDLE_VALUE)
            CloseHandle(h_);
        h_ = nullptr;
    }

private:
    HANDLE h_ = nullptr;
};

void readAll(HANDLE pipe, std::string& into) {
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &got, nullptr) && got > 0)
        into.append(buffer, got);
}

// The C runtime of the child writes "\r\n" for every "\n" to stdout and stderr, which are text
// streams on Windows. The line ends are made "\n" again, so that the tests compare the same text
// on every platform.
void toNewlineLineEnds(std::string& text) {
    size_t kept = 0;
    for (size_t i = 0; i < text.size(); ++i)
        if (!(text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n'))
            text[kept++] = text[i];
    text.resize(kept);
}

// A pipe whose write end the child inherits and whose read end it does not
bool makePipe(Handle& read, Handle& write) {
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE r = nullptr, w = nullptr;
    if (!CreatePipe(&r, &w, &inheritable, 0))
        return false;
    read.reset(r);
    write.reset(w);
    return SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0) != 0;
}

}  // namespace

ProcessResult runProcess(const fs::path& executable,
                         const std::vector<std::string>& arguments,
                         std::chrono::seconds timeout) {
    ProcessResult result;

    std::wstring commandLine = quoteArgument(executable.wstring());
    for (const std::string& argument : arguments)
        commandLine += L" " + quoteArgument(toWide(argument));

    Handle outRead, outWrite, errRead, errWrite;
    if (!makePipe(outRead, outWrite) || !makePipe(errRead, errWrite))
        return result;
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle nul(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0,
                           nullptr));

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nul.get();
    startup.hStdOutput = outWrite.get();
    startup.hStdError = errWrite.get();

    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), &commandLine[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
                        &info))
        return result;
    Handle process(info.hProcess), thread(info.hThread);
    // The parent's copies of the write ends: the readers see the end of the data when the child
    // closes its own
    outWrite.close();
    errWrite.close();

    std::thread outReader([&] { readAll(outRead.get(), result.out); });
    std::thread errReader([&] { readAll(errRead.get(), result.err); });

    const DWORD waited = WaitForSingleObject(process.get(), static_cast<DWORD>(timeout.count() * 1000));
    if (waited == WAIT_TIMEOUT) {
        result.timedOut = true;
        TerminateProcess(process.get(), 1);
        WaitForSingleObject(process.get(), INFINITE);
    }
    outReader.join();
    errReader.join();
    toNewlineLineEnds(result.out);
    toNewlineLineEnds(result.err);

    DWORD code = 0;
    GetExitCodeProcess(process.get(), &code);
    result.exitCode = result.timedOut ? -1 : static_cast<int>(code);
    return result;
}

#else

namespace {

// Closes a descriptor when it goes out of scope
class Fd {
public:
    Fd() = default;
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() { close(); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return fd_; }
    void reset(int fd) {
        close();
        fd_ = fd;
    }
    void close() {
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = -1;
    }

private:
    int fd_ = -1;
};

// A pipe whose descriptors are not inherited by programs started later; the one the child gets
// as stdout or stderr is duplicated onto 1 or 2, and a duplicate does not carry the flag
bool makePipe(Fd& read, Fd& write) {
    int fds[2];
    if (pipe(fds) != 0)
        return false;
    read.reset(fds[0]);
    write.reset(fds[1]);
    for (const int fd : fds)
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    return true;
}

// Appends what is there to read; false at the end of the data
bool readSome(int fd, std::string& into) {
    char buffer[4096];
    for (;;) {
        const ssize_t got = read(fd, buffer, sizeof(buffer));
        if (got > 0) {
            into.append(buffer, static_cast<size_t>(got));
            return true;
        }
        if (got < 0 && errno == EINTR)
            continue;
        return false;
    }
}

}  // namespace

ProcessResult runProcess(const fs::path& executable,
                         const std::vector<std::string>& arguments,
                         std::chrono::seconds timeout) {
    ProcessResult result;

    const std::string program = pathToUtf8(executable);
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const std::string& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);

    Fd outRead, outWrite, errRead, errWrite;
    if (!makePipe(outRead, outWrite) || !makePipe(errRead, errWrite))
        return result;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, outWrite.get(), 1);
    posix_spawn_file_actions_adddup2(&actions, errWrite.get(), 2);

    pid_t pid = 0;
    const int started = posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (started != 0)
        return result;
    // The parent's copies of the write ends: the pipes end when the child closes its own
    outWrite.close();
    errWrite.close();

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool outOpen = true, errOpen = true;
    while (outOpen || errOpen) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            result.timedOut = true;
            kill(pid, SIGKILL);
            break;
        }
        pollfd fds[2] = {{outOpen ? outRead.get() : -1, POLLIN, 0}, {errOpen ? errRead.get() : -1, POLLIN, 0}};
        const int ready = poll(fds, 2, static_cast<int>(std::min<long long>(left.count(), 1000000)));
        if (ready < 0 && errno != EINTR)
            break;
        if (ready <= 0)
            continue;
        if (fds[0].revents != 0 && !readSome(outRead.get(), result.out))
            outOpen = false;
        if (fds[1].revents != 0 && !readSome(errRead.get(), result.err))
            errOpen = false;
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (WIFEXITED(status))
        result.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        result.exitCode = -WTERMSIG(status);
    return result;
}

#endif

}  // namespace bm3dtest
