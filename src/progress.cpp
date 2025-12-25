#include "progress.h"

// TTY detection portability:
// - POSIX: isatty(STDERR_FILENO) from <unistd.h>
// - Windows: _isatty(_fileno(stderr)) from <io.h>/<stdio.h>
#ifndef _WIN32
#include <unistd.h>
#define GHX_ISATTY ::isatty
#define GHX_STDERR_FILENO STDERR_FILENO
#else
#include <io.h>     // _isatty
#include <stdio.h>  // _fileno, stderr
#define GHX_ISATTY ::_isatty
#define GHX_STDERR_FILENO ::_fileno(stderr)
#endif

#include <iostream>
#include <vector>

namespace ghx {

ProgressPrinter::ProgressPrinter(bool enabled, int interval_ms)
    : enabled_(enabled),
      is_tty_(GHX_ISATTY(GHX_STDERR_FILENO) == 1),
      interval_ms_(interval_ms) {
  last_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(interval_ms_);
  if (enabled_ && is_tty_) {
    start_spinner_thread();
  }
}

ProgressPrinter::~ProgressPrinter() {
  stop_spinner_thread();
}

void ProgressPrinter::set_enabled(bool enabled) { enabled_ = enabled; }

void ProgressPrinter::set_interval_ms(int interval_ms) { interval_ms_ = interval_ms; }

std::string ProgressPrinter::with_spinner(const std::string& message) {
  if (!spinner_enabled_ || !is_tty_) return message;

  static const std::vector<std::string> frames = {
      "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏",
  };
  const auto idx = spinner_idx_.fetch_add(1, std::memory_order_relaxed);
  const auto& f = frames[idx % frames.size()];
  return f + " " + message;
}

void ProgressPrinter::write_line(const std::string& message, bool same_line) {
  if (!enabled_) return;
  const auto msg = with_spinner(message);
  if (same_line && is_tty_) {
    std::cerr << "\r" << msg;
    if (last_len_ > msg.size()) {
      std::cerr << std::string(last_len_ - msg.size(), ' ');
      std::cerr << "\r" << msg;
    }
    last_len_ = msg.size();
    std::cerr.flush();
  } else {
    std::cerr << msg << "\n";
    std::cerr.flush();
  }
}

void ProgressPrinter::tick(const std::string& message, bool force) {
  if (!enabled_) return;
  {
    std::lock_guard<std::mutex> lock(mu_);
    last_message_ = message;
  }

  auto now = std::chrono::steady_clock::now();
  if (!force) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_).count();
    if (elapsed < interval_ms_) return;
  }
  last_ = now;

  std::lock_guard<std::mutex> lock(mu_);
  write_line(last_message_, /*same_line=*/true);
}

void ProgressPrinter::done(const std::string& message) {
  if (!enabled_) return;
  stop_spinner_thread();
  if (is_tty_) {
    // Do not animate spinner on final line; keep output stable.
    std::cerr << "\r" << message;
    if (last_len_ > message.size()) {
      std::cerr << std::string(last_len_ - message.size(), ' ');
      std::cerr << "\r" << message;
    }
    std::cerr << "\n";
    last_len_ = 0;
  } else {
    std::cerr << message << "\n";
  }
  std::cerr.flush();
}

void ProgressPrinter::start_spinner_thread() {
  // Only meaningful on TTY; on non-TTY we don't animate.
  if (!is_tty_) return;
  if (spinner_thread_.joinable()) return;
  stop_thread_.store(false, std::memory_order_release);
  spinner_thread_ = std::thread([this]() {
    while (true) {
      if (stop_thread_.load(std::memory_order_acquire)) break;
      int sleep_ms = 100;
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (stop_thread_.load(std::memory_order_acquire)) break;
        sleep_ms = (interval_ms_ > 0 ? interval_ms_ : 100);
        if (enabled_ && !last_message_.empty()) {
          // Animate spinner even if message doesn't change.
          write_line(last_message_, /*same_line=*/true);
        }
      }
      // Sleep outside lock.
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
  });
}

void ProgressPrinter::stop_spinner_thread() {
  if (!spinner_thread_.joinable()) return;
  stop_thread_.store(true, std::memory_order_release);
  spinner_thread_.join();
}

}  // namespace ghx


