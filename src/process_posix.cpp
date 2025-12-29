#include "process.h"

#if !defined(_WIN32)

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>

namespace ghx {

static void close_if_valid(int& fd) {
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

static void close_pipe(int pipefd[2]) {
  close_if_valid(pipefd[0]);
  close_if_valid(pipefd[1]);
}

static void write_all(int fd, const std::string& data) {
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      throw std::runtime_error(std::string("write failed: ") + std::strerror(errno));
    }
    off += static_cast<size_t>(n);
  }
}

static void set_nonblocking(int fd, const char* which) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    throw std::runtime_error(
        std::string("fcntl(F_GETFL) failed for ") + which + ": " + std::strerror(errno));
  }
  if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    throw std::runtime_error(
        std::string("fcntl(F_SETFL,O_NONBLOCK) failed for ") + which + ": " + std::strerror(errno));
  }
}

static void drain_fd(int& fd, std::string& out, const char* which) {
  char buf[8192];
  while (fd >= 0) {
    ssize_t n = ::read(fd, buf, sizeof(buf));
    if (n > 0) {
      out.append(buf, buf + n);
      continue;
    }
    if (n == 0) {
      close_if_valid(fd);
      return;
    }
    // n < 0
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return;
    throw std::runtime_error(std::string("read failed for ") + which + ": " + std::strerror(errno));
  }
}

static void read_stdout_stderr_concurrently(
    int& stdout_fd,
    int& stderr_fd,
    std::string& stdout_out,
    std::string& stderr_out) {
  if (stdout_fd >= 0) set_nonblocking(stdout_fd, "stdout");
  if (stderr_fd >= 0) set_nonblocking(stderr_fd, "stderr");

  while (stdout_fd >= 0 || stderr_fd >= 0) {
    struct pollfd fds[2];
    nfds_t nfds = 0;

    const bool has_stdout = (stdout_fd >= 0);
    const bool has_stderr = (stderr_fd >= 0);

    if (has_stdout) {
      fds[nfds].fd = stdout_fd;
      fds[nfds].events = POLLIN | POLLHUP | POLLERR;
      fds[nfds].revents = 0;
      nfds++;
    }
    if (has_stderr) {
      fds[nfds].fd = stderr_fd;
      fds[nfds].events = POLLIN | POLLHUP | POLLERR;
      fds[nfds].revents = 0;
      nfds++;
    }

    int rc = ::poll(fds, nfds, -1);
    if (rc < 0) {
      if (errno == EINTR) continue;
      throw std::runtime_error(std::string("poll failed: ") + std::strerror(errno));
    }

    nfds_t idx = 0;
    if (has_stdout) {
      short rev = fds[idx].revents;
      if (rev & POLLNVAL) {
        throw std::runtime_error("poll returned POLLNVAL for stdout fd");
      }
      if (rev & (POLLIN | POLLHUP | POLLERR)) {
        drain_fd(stdout_fd, stdout_out, "stdout");
      }
      idx++;
    }
    if (has_stderr) {
      short rev = fds[idx].revents;
      if (rev & POLLNVAL) {
        throw std::runtime_error("poll returned POLLNVAL for stderr fd");
      }
      if (rev & (POLLIN | POLLHUP | POLLERR)) {
        drain_fd(stderr_fd, stderr_out, "stderr");
      }
    }
  }
}

ProcessResult run_process(
    const std::vector<std::string>& args,
    const std::string& stdin_data,
    const std::string& working_dir) {
  if (args.empty()) {
    throw std::invalid_argument("run_process: args is empty");
  }

  int stdin_pipe[2]{-1, -1};
  int stdout_pipe[2]{-1, -1};
  int stderr_pipe[2]{-1, -1};

  if (::pipe(stdin_pipe) != 0) throw std::runtime_error("pipe(stdin) failed");
  if (::pipe(stdout_pipe) != 0) {
    close_pipe(stdin_pipe);
    throw std::runtime_error("pipe(stdout) failed");
  }
  if (::pipe(stderr_pipe) != 0) {
    close_pipe(stdin_pipe);
    close_pipe(stdout_pipe);
    throw std::runtime_error("pipe(stderr) failed");
  }

  pid_t pid = ::fork();
  if (pid < 0) {
    close_pipe(stdin_pipe);
    close_pipe(stdout_pipe);
    close_pipe(stderr_pipe);
    throw std::runtime_error("fork failed");
  }

  if (pid == 0) {
    // child
    if (!working_dir.empty()) {
      if (::chdir(working_dir.c_str()) != 0) {
        _exit(127);
      }
    }
    if (::dup2(stdin_pipe[0], STDIN_FILENO) == -1) {
      _exit(127);
    }
    if (::dup2(stdout_pipe[1], STDOUT_FILENO) == -1) {
      _exit(127);
    }
    if (::dup2(stderr_pipe[1], STDERR_FILENO) == -1) {
      _exit(127);
    }

    ::close(stdin_pipe[0]);
    ::close(stdin_pipe[1]);
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[0]);
    ::close(stderr_pipe[1]);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);
    ::execvp(argv[0], argv.data());
    _exit(127);
  }

  // parent
  ::close(stdin_pipe[0]);
  ::close(stdout_pipe[1]);
  ::close(stderr_pipe[1]);

  try {
    if (!stdin_data.empty()) write_all(stdin_pipe[1], stdin_data);
  } catch (...) {
    // fallthrough; we'll still wait and try to read what we can
  }
  ::close(stdin_pipe[1]);

  ProcessResult r;
  // Read stdout/stderr concurrently to avoid deadlocks if one pipe fills.
  int out_fd = stdout_pipe[0];
  int err_fd = stderr_pipe[0];
  try {
    read_stdout_stderr_concurrently(out_fd, err_fd, r.stdout_str, r.stderr_str);
  } catch (...) {
    close_if_valid(out_fd);
    close_if_valid(err_fd);
    int status = 0;
    (void)::waitpid(pid, &status, 0);  // best effort: avoid zombie on exceptions
    throw;
  }

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) {
    r.exit_code = -1;
    return r;
  }

  if (WIFEXITED(status)) {
    r.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    r.exit_code = 128 + WTERMSIG(status);
  } else {
    r.exit_code = -1;
  }

  return r;
}

}  // namespace ghx

#endif  // !_WIN32


