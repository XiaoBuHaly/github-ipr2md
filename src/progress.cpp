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
#include <windows.h>
#define GHX_ISATTY ::_isatty
#define GHX_STDERR_FILENO ::_fileno(stderr)
#endif

#include <iostream>
#include <vector>

namespace ghx {

#if defined(_WIN32)
static bool is_console_stderr() {
  HANDLE h = ::GetStdHandle(STD_ERROR_HANDLE);
  if (h == nullptr || h == INVALID_HANDLE_VALUE) return false;
  DWORD mode = 0;
  return ::GetConsoleMode(h, &mode) != 0;
}

static bool console_output_is_utf8() {
  // 65001 is CP_UTF8. In classic CMD/CP936, Unicode spinner glyphs are mojibake.
  return ::GetConsoleOutputCP() == 65001;
}
#endif

ProgressPrinter::ProgressPrinter(bool enabled, int interval_ms)
    : enabled_(enabled),
      is_tty_(
#if defined(_WIN32)
          // Some Windows terminals (e.g. ConPTY) can confuse _isatty; treat console stderr as TTY.
          is_console_stderr() || (GHX_ISATTY(GHX_STDERR_FILENO) == 1)
#else
          (GHX_ISATTY(GHX_STDERR_FILENO) == 1)
#endif
      ),
      interval_ms_(interval_ms) {
  last_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(interval_ms_);

#if defined(_WIN32)
  // Default: hide Unicode spinner on non-UTF8 console to avoid garbled prefix like "鉅?".
  if (is_console_stderr() && !console_output_is_utf8()) {
    spinner_enabled_ = false;
  }
#endif

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
      // Braille spinner frames in UTF-8 bytes (avoid source file encoding issues on MSVC/CP936).
      "\xE2\xA0\x8B",  // ⠋ U+280B
      "\xE2\xA0\x99",  // ⠙ U+2819
      "\xE2\xA0\xB9",  // ⠹ U+2839
      "\xE2\xA0\xB8",  // ⠸ U+2838
      "\xE2\xA0\xBC",  // ⠼ U+283C
      "\xE2\xA0\xB4",  // ⠴ U+2834
      "\xE2\xA0\xA6",  // ⠦ U+2826
      "\xE2\xA0\xA7",  // ⠧ U+2827
      "\xE2\xA0\x87",  // ⠇ U+2807
      "\xE2\xA0\x8F",  // ⠏ U+280F
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


