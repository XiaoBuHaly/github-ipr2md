#include "../src/process.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  try {
    std::string git = "git";
    if (argc >= 2 && argv[1] && argv[1][0] != '\0') {
      git = argv[1];
    }

    std::vector<std::string> args = {git, "--version"};
    auto r = ghx::run_process(args);
    if (r.exit_code != 0) {
      std::cerr << "run_process failed. exit=" << r.exit_code << "\n";
      if (!r.stderr_str.empty()) std::cerr << r.stderr_str << "\n";
      return 1;
    }

    // Print something so CI logs show it worked.
    if (!r.stdout_str.empty()) {
      std::cout << r.stdout_str;
    } else if (!r.stderr_str.empty()) {
      std::cout << r.stderr_str;
    } else {
      std::cout << "ok\n";
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "exception: " << e.what() << "\n";
    return 2;
  }
}


