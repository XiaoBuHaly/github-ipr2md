#pragma once

#include <string>
#include <vector>

namespace ghx {

struct ProcessResult {
  int exit_code = -1;
  std::string stdout_str;
  std::string stderr_str;
};

// Runs a subprocess without invoking a shell. Optionally writes stdin_data to stdin.
// args[0] must be an executable name resolvable via PATH or an absolute path.
ProcessResult run_process(
    const std::vector<std::string>& args,
    const std::string& stdin_data = "",
    const std::string& working_dir = "");

}  // namespace ghx


