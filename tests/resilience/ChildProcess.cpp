#include "ChildProcess.h"

#include <cstring>
#include <sstream>

#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace archmage::testing {

ChildResult runChild(const std::vector<std::string> &args, const std::vector<std::string> &environment) {
    ChildResult result;
    int pipeFds[2];
    if (::pipe(pipeFds) != 0) {
        result.output = "pipe failed";
        return result;
    }
    std::vector<std::string> env;
    for (char **entry = environ; *entry != nullptr; ++entry) {
        env.emplace_back(*entry);
    }
    env.insert(env.end(), environment.begin(), environment.end());
    std::vector<char *> argv;
    for (const std::string &arg : args) {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);
    std::vector<char *> envp;
    for (const std::string &entry : env) {
        envp.push_back(const_cast<char *>(entry.c_str()));
    }
    envp.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(pipeFds[1], STDOUT_FILENO);
        ::dup2(pipeFds[1], STDERR_FILENO);
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        ::execve(argv[0], argv.data(), envp.data());
        ::_exit(127);
    }
    ::close(pipeFds[1]);
    char buffer[4096];
    ssize_t n = 0;
    while ((n = ::read(pipeFds[0], buffer, sizeof buffer)) > 0) {
        result.output.append(buffer, static_cast<std::size_t>(n));
    }
    ::close(pipeFds[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.signal = WTERMSIG(status);
    }
    std::istringstream lines(result.output);
    for (std::string line; std::getline(lines, line);) {
        result.lines.push_back(line);
    }
    return result;
}

} // namespace archmage::testing
