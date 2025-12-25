#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace ghx {

// Simple rate-limited progress printer to stderr.
// - If stderr is a TTY: updates a single line with '\r' and flushes.
// - Otherwise: prints periodic newline-delimited messages.
class ProgressPrinter {
 public:
  explicit ProgressPrinter(bool enabled = true, int interval_ms = 100);
  ~ProgressPrinter();

  ProgressPrinter(const ProgressPrinter&) = delete;
  ProgressPrinter& operator=(const ProgressPrinter&) = delete;
  ProgressPrinter(ProgressPrinter&&) = delete;
  ProgressPrinter& operator=(ProgressPrinter&&) = delete;

  void set_enabled(bool enabled);
  void set_interval_ms(int interval_ms);

  // Print a progress message (rate-limited). If force is true, always print.
  void tick(const std::string& message, bool force = false);

  // Print a final message and end the progress line (always newline).
  void done(const std::string& message);

 private:
  bool enabled_ = true;
  bool is_tty_ = false;
  int interval_ms_ = 100;
  std::chrono::steady_clock::time_point last_;
  std::atomic<size_t> spinner_idx_{0};
  bool spinner_enabled_ = true;
  size_t last_len_ = 0;

  std::mutex mu_;
  std::thread spinner_thread_;
  std::atomic<bool> stop_thread_{false};
  std::string last_message_;

  void write_line(const std::string& message, bool same_line);
  std::string with_spinner(const std::string& message);
  void start_spinner_thread();
  void stop_spinner_thread();
};

}  // namespace ghx


