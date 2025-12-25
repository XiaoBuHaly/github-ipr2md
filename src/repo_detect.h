#pragma once

#include <string>

namespace ghx {

// Returns "owner/repo" inferred from git remotes in the given working directory.
// Throws std::runtime_error on failure.
std::string infer_repo_from_git_remote(const std::string& working_dir);

// Parses common git remote URL formats and returns "owner/repo".
// Throws std::runtime_error if it can't be parsed.
std::string parse_owner_repo_from_remote_url(const std::string& remote_url);

}  // namespace ghx


