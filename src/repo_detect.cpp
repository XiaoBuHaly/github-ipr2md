#include "repo_detect.h"

#include "process.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace ghx {

static std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
    s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
  return s.substr(i);
}

static std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == delim) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

std::string parse_owner_repo_from_remote_url(const std::string& remote_url_in) {
  std::string u = trim(remote_url_in);
  if (u.empty()) throw std::runtime_error("remote url is empty");

  // Normalize: drop trailing .git and trailing slashes.
  while (!u.empty() && u.back() == '/') u.pop_back();
  if (u.size() > 4 && u.substr(u.size() - 4) == ".git") u = u.substr(0, u.size() - 4);

  std::string path;

  // git@github.com:owner/repo
  if (u.rfind("git@", 0) == 0) {
    auto pos = u.find(':');
    if (pos == std::string::npos) throw std::runtime_error("unsupported ssh remote: " + u);
    path = u.substr(pos + 1);
  } else {
    // https://github.com/owner/repo or ssh://git@github.com/owner/repo
    auto scheme_pos = u.find("://");
    if (scheme_pos != std::string::npos) {
      auto slash = u.find('/', scheme_pos + 3);
      if (slash == std::string::npos) throw std::runtime_error("remote missing path: " + u);
      path = u.substr(slash + 1);
    } else {
      // Fallback: github.com/owner/repo
      auto slash = u.find('/');
      if (slash == std::string::npos) throw std::runtime_error("remote missing /: " + u);
      path = u.substr(slash + 1);
    }
  }

  while (!path.empty() && path.front() == '/') path.erase(path.begin());
  while (!path.empty() && path.back() == '/') path.pop_back();
  if (path.size() > 4 && path.substr(path.size() - 4) == ".git") path = path.substr(0, path.size() - 4);

  auto parts = split(path, '/');
  if (parts.size() < 2 || parts[0].empty() || parts[1].empty()) {
    throw std::runtime_error("cannot parse owner/repo from remote: " + u);
  }
  return parts[0] + "/" + parts[1];
}

std::string infer_repo_from_git_remote(const std::string& working_dir) {
  // Prefer origin.
  auto r = run_process({"git", "remote", "get-url", "origin"}, "", working_dir);
  if (r.exit_code == 0) {
    return parse_owner_repo_from_remote_url(r.stdout_str);
  }

  // Try first remote.
  auto list = run_process({"git", "remote"}, "", working_dir);
  if (list.exit_code != 0) {
    throw std::runtime_error("git remote failed; are you in a git repo? stderr: " + trim(list.stderr_str));
  }
  auto remotes = split(trim(list.stdout_str), '\n');
  for (const auto& name_raw : remotes) {
    auto name = trim(name_raw);
    if (name.empty()) continue;
    auto rr = run_process({"git", "remote", "get-url", name}, "", working_dir);
    if (rr.exit_code == 0) {
      return parse_owner_repo_from_remote_url(rr.stdout_str);
    }
  }
  throw std::runtime_error("could not infer repo from git remotes");
}

}  // namespace ghx


