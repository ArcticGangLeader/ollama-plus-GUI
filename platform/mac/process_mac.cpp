#include "core/process.hpp"

#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <cstdio>

extern char** environ;

namespace opg {

CommandResult runCommand(const std::string& command, int timeoutSeconds) {
    CommandResult result;

    int fds[2];
    if (pipe(fds) != 0) {
        result.error = std::string("创建管道失败: ") + std::strerror(errno);
        return result;
    }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);

    const char* argv[] = { "/bin/sh", "-c", command.c_str(), nullptr };
    pid_t pid = 0;
    int rc = posix_spawn(&pid, "/bin/sh", &actions, nullptr,
                         const_cast<char**>(argv), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);

    if (rc != 0) {
        close(fds[0]);
        result.error = std::string("启动命令失败: ") + std::strerror(rc);
        return result;
    }
    result.started = true;

    std::string output;
    char buf[8192];
    bool timedOut = false;

    for (;;) {
        if (timeoutSeconds > 0) {
            struct pollfd pfd = { fds[0], POLLIN, 0 };
            int pr = poll(&pfd, 1, timeoutSeconds * 1000);
            if (pr == 0) { timedOut = true; break; }
            if (pr < 0) break;
        }
        ssize_t n = read(fds[0], buf, sizeof(buf));
        if (n > 0) { output.append(buf, (size_t)n); continue; }
        if (n == 0) break;
        if (errno == EINTR) continue;
        break;
    }
    close(fds[0]);

    int status = 0;
    if (timedOut) {
        kill(pid, SIGTERM);
        usleep(1000000);
        if (waitpid(pid, &status, WNOHANG) == 0) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
        if (WIFSIGNALED(status)) result.exitCode = 128 + WTERMSIG(status);
        else if (WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
        result.error = "命令执行超时。";
        } else {
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            result.exitCode = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result.exitCode = 128 + WTERMSIG(status);
        }
    }

    result.output = output;
    if (result.error.empty() && result.exitCode != 0) {
        result.error = output.empty() ? "命令返回非零退出码。" : output;
    }
    return result;
}

std::string quoteIfNeeded(const std::string& value) {
    if (!value.empty() &&
        value.find(' ') == std::string::npos &&
        value.find('"') == std::string::npos &&
        value.find('\'') == std::string::npos &&
        value.find('\\') == std::string::npos) {
        return value;
    }
    std::string result = "'";
    for (char c : value) {
        if (c == '\'') result += "'\\''";
        else result.push_back(c);
    }
    result.push_back('\'');
    return result;
}

} // namespace opg