#include "gh_graphql.h"
#include "json_convert.h"
#include "md_render.h"
#include "progress.h"
#include "repo_detect.h"
#include "stats.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace ghx;

// #region agent log
static void agent_log(
    const char* hypothesisId,
    const char* location,
    const char* message,
    const nlohmann::json& data) {
  try {
    std::ofstream f(R"(f:\113Code\github-ipr2md\.cursor\debug.log)", std::ios::app);
    if (!f) return;
    nlohmann::json j;
    j["sessionId"] = "debug-session";
    j["runId"] = "limit-run1";
    j["hypothesisId"] = hypothesisId;
    j["location"] = location;
    j["message"] = message;
    j["data"] = data;
    j["timestamp"] = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    f << j.dump() << "\n";
  } catch (...) {
  }
}
// #endregion

static std::string trim_ascii_ws(std::string s) {
  auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), is_space));
  s.erase(std::find_if_not(s.rbegin(), s.rend(), is_space).base(), s.end());
  return s;
}

static std::string normalize_owner_repo_arg(std::string s) {
  s = trim_ascii_ws(std::move(s));
  // Drop URL fragments and query strings (common when pasting GitHub URLs).
  if (auto pos = s.find('#'); pos != std::string::npos) s = s.substr(0, pos);
  if (auto pos = s.find('?'); pos != std::string::npos) s = s.substr(0, pos);
  // Allow "/owner/repo" (common in URLs) by stripping leading slashes.
  while (!s.empty() && (s.front() == '/' || s.front() == '\\')) s.erase(s.begin());
  while (!s.empty() && s.back() == '/') s.pop_back();

  // If user pasted a remote URL, attempt to parse it.
  if (s.find("://") != std::string::npos || s.rfind("git@", 0) == 0 || s.find("github.com") != std::string::npos) {
    try {
      return parse_owner_repo_from_remote_url(s);
    } catch (...) {
      // Fall back to raw input.
    }
  }
  return s;
}

struct RepoArgParse {
  std::string owner_repo;
  int id = 0;  // optional: extracted from issue/pr URL, e.g. .../issues/123 or .../pull/123
};

static RepoArgParse parse_repo_arg(std::string s) {
  RepoArgParse out;
  s = trim_ascii_ws(std::move(s));
  if (s.empty()) return out;

  // Drop URL fragments and query strings.
  if (auto pos = s.find('#'); pos != std::string::npos) s = s.substr(0, pos);
  if (auto pos = s.find('?'); pos != std::string::npos) s = s.substr(0, pos);

  // Extract id from GitHub web URLs if present.
  // Accept both full URLs and bare paths like /owner/repo/issues/123.
  auto s_for_scan = s;
  // Normalize backslashes.
  for (auto& c : s_for_scan) if (c == '\\') c = '/';

  auto find_number_after = [&](const char* marker) -> int {
    auto p = s_for_scan.find(marker);
    if (p == std::string::npos) return 0;
    p += std::strlen(marker);
    if (p >= s_for_scan.size()) return 0;
    // Parse digits.
    size_t end = p;
    while (end < s_for_scan.size() && std::isdigit(static_cast<unsigned char>(s_for_scan[end]))) end++;
    if (end == p) return 0;
    try {
      return std::stoi(s_for_scan.substr(p, end - p));
    } catch (...) {
      return 0;
    }
  };

  int id = 0;
  id = std::max(id, find_number_after("/issues/"));
  id = std::max(id, find_number_after("/pull/"));
  id = std::max(id, find_number_after("/pulls/"));
  out.id = id;

  out.owner_repo = normalize_owner_repo_arg(std::move(s));
  return out;
}

static nlohmann::json read_json_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("failed to open input json: " + path);
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return nlohmann::json::parse(data);
}

static void write_text_file(const fs::path& path, const std::string& content) {
  // If writing to a file in the current directory (e.g. "output.md"),
  // parent_path() is empty. create_directories("") would throw.
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent);
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) throw std::runtime_error("failed to open output: " + path.string());
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

static bool state_match(const ghx::Item& it, const std::string& state) {
  if (state == "all") return true;
  if (state == "open") return it.state == "OPEN";
  if (state == "closed") {
    if (it.kind == ghx::ItemKind::PullRequest) return it.state == "CLOSED" || it.state == "MERGED";
    return it.state == "CLOSED";
  }
  return true;
}

struct PostProcessInfo {
  int total_available = 0;  // after filter, before limit
  bool truncated = false;
  int min_number = 0;
  int max_number = 0;
  std::string min_created_at;
  std::string max_created_at;
};

struct WriteResult {
  std::string wrote_message;  // e.g. "Wrote: output.md" or "Wrote directory: outdir"
};

static WriteResult write_markdown_output(
    const RepoExport& repo,
    const RenderOptions& ropt,
    const ExportMetadata& meta,
    const std::string& out_path,
    int split_n,
    bool per_item,
    bool progress_enabled,
    int progress_interval_ms) {
  const bool reverse_order = meta.reverse_order;

  WriteResult r;
  if (split_n <= 0 && !per_item) {
    ProgressPrinter progress(/*enabled=*/progress_enabled, /*interval_ms=*/progress_interval_ms);
    // Stream to file to avoid building a huge string and to show progress.
    const fs::path outp = out_path;
    const auto parent = outp.parent_path();
    if (!parent.empty()) fs::create_directories(parent);
    std::ofstream out(outp, std::ios::binary);
    if (!out) throw std::runtime_error("failed to open output: " + outp.string());

    write_repo_preamble(out, repo, ropt, nullptr, &meta);
    const size_t total = repo.items.size();
    auto last_ui = std::chrono::steady_clock::now() - std::chrono::milliseconds(progress_interval_ms);
    constexpr size_t kCheckEvery = 256;
    for (size_t i = 0; i < total; i++) {
      write_item_markdown(out, repo.items[i], ropt);
      if (progress_enabled) {
        const bool force = (i == 0) || (i + 1 == total);
        if (force || ((i + 1) % kCheckEvery == 0)) {
          auto now = std::chrono::steady_clock::now();
          if (force || std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ui).count() >= progress_interval_ms) {
            last_ui = now;
            progress.tick("Writing markdown: " + std::to_string(i + 1) + "/" + std::to_string(total), /*force=*/true);
          }
        }
      }
    }
    out.flush();

    r.wrote_message = "Wrote: " + out_path;
    progress.done(r.wrote_message);
    return r;
  }

  fs::path out_dir = out_path;
  if (out_dir.extension() == ".md") {
    // Common mistake: user passed a file path but asked for split mode.
    out_dir = out_dir.replace_extension();
  }
  fs::create_directories(out_dir);

  if (per_item) {
    ProgressPrinter progress(/*enabled=*/progress_enabled, /*interval_ms=*/progress_interval_ms);
    size_t idx = 0;
    auto last_ui = std::chrono::steady_clock::now() - std::chrono::milliseconds(progress_interval_ms);
    constexpr size_t kCheckEvery = 64;
    const size_t total = repo.items.size();
    for (const auto& it : repo.items) {
      idx++;
      std::string prefix = (it.kind == ghx::ItemKind::PullRequest) ? "pr" : "issue";
      fs::path p = out_dir / (prefix + "-" + std::to_string(it.number) + ".md");
      const auto parent = p.parent_path();
      if (!parent.empty()) fs::create_directories(parent);
      std::ofstream out(p, std::ios::binary);
      if (!out) throw std::runtime_error("failed to open output: " + p.string());
      write_item_markdown(out, it, ropt);
      if (progress_enabled) {
        const bool force = (idx == 1) || (idx == total);
        if (force || (idx % kCheckEvery == 0)) {
          auto now = std::chrono::steady_clock::now();
          if (force || std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ui).count() >= progress_interval_ms) {
            last_ui = now;
            progress.tick("Writing per-item: " + std::to_string(idx) + "/" + std::to_string(total), /*force=*/true);
          }
        }
      }
    }

    r.wrote_message = "Wrote directory: " + out_dir.string();
    progress.done(r.wrote_message);
    return r;
  }

  // split_n chunked
  int chunk = 0;
  const size_t total_items = repo.items.size();
  const size_t chunk_size = static_cast<size_t>(split_n);
  const size_t total_chunks = (total_items + chunk_size - 1) / chunk_size;
  const int chunk_width = std::max(2, static_cast<int>(std::to_string(std::max<size_t>(1, total_chunks)).size()));
  ProgressPrinter progress(/*enabled=*/progress_enabled, /*interval_ms=*/progress_interval_ms);
  for (size_t i = 0; i < repo.items.size(); i += static_cast<size_t>(split_n)) {
    chunk++;
    RepoExport sub = repo;
    sub.items.clear();
    size_t end = std::min(repo.items.size(), i + static_cast<size_t>(split_n));
    for (size_t k = i; k < end; k++) sub.items.push_back(repo.items[k]);

    std::ostringstream name;
    name << "chunk-" << std::setw(chunk_width) << std::setfill('0') << chunk << ".md";
    fs::path p = out_dir / name.str();
    ExportMetadata chunk_meta = meta;
    chunk_meta.limit = 0;
    chunk_meta.truncated = false;
    chunk_meta.total_available = static_cast<int>(sub.items.size());
    if (!sub.items.empty()) {
      // sub.items are already in output order
      chunk_meta.min_number = sub.items.front().number;
      chunk_meta.max_number = sub.items.back().number;
      if (reverse_order) std::swap(chunk_meta.min_number, chunk_meta.max_number);
      // Only set created_at metadata if both endpoints have timestamps
      bool first_has_created_at = !sub.items.front().created_at.empty();
      bool last_has_created_at = !sub.items.back().created_at.empty();
      if (first_has_created_at && last_has_created_at) {
        auto first = sub.items.front().created_at;
        auto last = sub.items.back().created_at;
        if (reverse_order) std::swap(first, last);
        chunk_meta.min_created_at = first;
        chunk_meta.max_created_at = last;
      }
    }

    const auto parent = p.parent_path();
    if (!parent.empty()) fs::create_directories(parent);
    std::ofstream out(p, std::ios::binary);
    if (!out) throw std::runtime_error("failed to open output: " + p.string());
    write_repo_preamble(out, sub, ropt, nullptr, &chunk_meta);
    for (const auto& it : sub.items) {
      write_item_markdown(out, it, ropt);
    }
    progress.tick("Wrote chunk " + std::to_string(chunk) + " (" + std::to_string(sub.items.size()) + " items)", false);
  }

  r.wrote_message = "Wrote directory: " + out_dir.string();
  progress.done(r.wrote_message);
  return r;
}

static PostProcessInfo post_process_repo(
    RepoExport& repo,
    bool include_issues,
    bool include_prs,
    const std::string& state,
    int limit,
    bool reverse_order) {
  // Filter by kind and state (even though GraphQL already filters, `--in` needs this).
  std::vector<Item> filtered;
  filtered.reserve(repo.items.size());
  for (const auto& it : repo.items) {
    if (it.kind == ItemKind::Issue && !include_issues) continue;
    if (it.kind == ItemKind::PullRequest && !include_prs) continue;
    if (!state_match(it, state)) continue;
    filtered.push_back(it);
  }
  repo.items = std::move(filtered);

  // Prefer created_at if available (GraphQL fetch), otherwise fall back to number (JSON --in mode).
  auto has_created_at = [&]() -> bool {
    for (const auto& it : repo.items) {
      if (!it.created_at.empty()) return true;
    }
    return false;
  }();

  if (has_created_at) {
    // ISO8601 timestamps sort lexicographically.
    if (reverse_order) {
      std::sort(repo.items.begin(), repo.items.end(), [](const Item& a, const Item& b) {
        if (a.created_at != b.created_at) return a.created_at > b.created_at;
        return a.number > b.number;
      });
    } else {
      std::sort(repo.items.begin(), repo.items.end(), [](const Item& a, const Item& b) {
        if (a.created_at != b.created_at) return a.created_at < b.created_at;
        return a.number < b.number;
      });
    }
  } else {
    // Default: ascending by number (older -> newer). Use --reverse for descending.
    if (reverse_order) {
      std::sort(repo.items.begin(), repo.items.end(), [](const Item& a, const Item& b) { return a.number > b.number; });
    } else {
      std::sort(repo.items.begin(), repo.items.end(), [](const Item& a, const Item& b) { return a.number < b.number; });
    }
  }

  PostProcessInfo info;
  info.total_available = static_cast<int>(repo.items.size());

  if (limit > 0 && static_cast<int>(repo.items.size()) > limit) {
    repo.items.resize(static_cast<size_t>(limit));
    info.truncated = true;
  }

  // #region agent log
  agent_log(
      "H1",
      "src/main.cpp:post_process_repo:limit",
      "truncate",
      {{"limit", limit},
       {"sort_by_created_at", has_created_at},
       {"size_after_filter", info.total_available},
       {"size_after_limit", (int)repo.items.size()},
       {"truncated", info.truncated},
       {"include_issues", include_issues},
       {"include_prs", include_prs},
       {"state", state},
       {"reverse_order", reverse_order}});
  // #endregion

  if (!repo.items.empty()) {
    info.min_number = repo.items.front().number;
    info.max_number = repo.items.back().number;
    // After sorting, the range depends on order.
    if (reverse_order) std::swap(info.min_number, info.max_number);

    // CreatedAt range if present.
    bool any_created_at = false;
    for (const auto& it : repo.items) {
      if (!it.created_at.empty()) { any_created_at = true; break; }
    }
    if (any_created_at) {
      auto first = repo.items.front().created_at;
      auto last = repo.items.back().created_at;
      if (reverse_order) std::swap(first, last);
      info.min_created_at = first;
      info.max_created_at = last;
    }
  }

  return info;
}

int main(int argc, char** argv) {
  CLI::App app{"Export GitHub issues/PRs (and comments/metadata) to Markdown via `gh api graphql`."};

  std::string repo_arg;
  std::string in_json;
  std::string out_path = "output.md";
  std::string state = "all";
  int limit = 0;
  int split_n = 0;
  bool per_item = false;
  bool idempotent = false;
  std::string title = "Issues Export";
  std::string stats_json_path;
  std::string hostname;
  int id = 0;
  int removed_issue = 0;
  int removed_pr = 0;

  bool include_issues = true;
  bool include_prs = true;
  bool include_body = true;
  bool include_comments = true;
  std::string pr_review_mode = "none";  // none|decision|reviews|threads
  bool include_labels = true;
  bool include_reactions = true;
  bool include_authors = true;
  bool include_timestamps = true;
  bool include_links = true;
  bool include_assignees = true;
  bool include_milestone = true;

  bool reverse_order = false;
  bool progress_enabled = true;
  bool no_progress = false;
  bool quiet = false;
  int progress_interval_ms = 100;

  bool no_issues = false;
  bool no_prs = false;
  bool no_body = false;
  bool no_comments = false;
  bool no_labels = false;
  bool no_reactions = false;
  bool no_authors = false;
  bool no_timestamps = false;
  bool no_links = false;
  bool no_assignees = false;
  bool no_milestone = false;

  int labels_first = 100;
  int assignees_first = 20;

  app.add_option("--repo", repo_arg, "Repo in owner/name format. Default: infer from git remote.");
  app.add_option("--in", in_json, "Convert existing JSON (from `gh issue list --json ...`) to Markdown.");
  app.add_option("--out", out_path, "Output path. Default: ./output.md (or a directory when --split/--per-item).");
  app.add_option("--id", id, "Export a single Issue/PR by number (requires --repo or infer from git remote).");
  auto opt_removed_issue = app.add_option("--issue", removed_issue, "(removed) Use --id instead.");
  auto opt_removed_pr = app.add_option("--pr", removed_pr, "(removed) Use --id instead.");
  app.add_option("--state", state, "Filter state: all|open|closed. Default: all.")
      ->check(CLI::IsMember({"all", "open", "closed"}));
  app.add_option("--limit", limit, "Max number of items (issues+prs). 0 means unlimited. Default: 0.");
  app.add_flag("--reverse", reverse_order, "Reverse output order (default is ascending by number).");
  app.add_flag("--no-progress", no_progress, "Disable progress output (fetch/select/write). Still prints final Wrote/Stats. Use --quiet to silence everything except errors.");
  app.add_flag("--quiet", quiet, "Quiet mode: no output except errors.");
  app.add_option("--progress-interval-ms", progress_interval_ms, "Progress refresh interval in ms. Default: 100.");
  app.add_option("--split", split_n, "Split output into multiple files, each file max N items. Requires --out be a directory.");
  app.add_flag("--per-item", per_item, "Write one file per issue/PR. Requires --out be a directory. Mutually exclusive with --split.");
  app.add_flag("--idempotent", idempotent, "Deterministic output: no generated timestamps; stable sorting where applicable.");
  app.add_option("--title", title, "Markdown document title. Default: \"Issues Export\".");
  app.add_option("--stats-json", stats_json_path, "Write stats as JSON to this path.");
  app.add_option("--hostname", hostname, "GitHub hostname for `gh api` (default: github.com).");
  app.add_option(
         "--pr-review",
         pr_review_mode,
         "Export PR review data: none|decision|reviews|threads. Default: none. "
         "Note: reviewThreads can be expensive.")
      ->check(CLI::IsMember({"none", "decision", "reviews", "threads"}));

  app.add_option("--labels-first", labels_first, "Per-item GraphQL limit: labels(first: N). Default: 100 (may truncate if more).")
      ->check(CLI::Range(1, 100));
  app.add_option("--assignees-first", assignees_first, "Per-item GraphQL limit: assignees(first: N). Default: 20 (may truncate if more).")
      ->check(CLI::Range(1, 100));

  app.add_flag("--no-issues", no_issues, "Do not include issues.");
  app.add_flag("--no-prs", no_prs, "Do not include pull requests.");

  app.add_flag("--no-body", no_body, "Do not export body text.");
  app.add_flag("--no-comments", no_comments, "Do not export comments.");
  app.add_flag("--no-labels", no_labels, "Do not export labels.");
  app.add_flag("--no-reactions", no_reactions, "Do not export reactions.");
  app.add_flag("--no-authors", no_authors, "Do not export author/authorAssociation.");
  app.add_flag("--no-timestamps", no_timestamps, "Do not export timestamps.");
  app.add_flag("--no-links", no_links, "Do not export URLs/links in metadata.");
  app.add_flag("--no-assignees", no_assignees, "Do not export assignees.");
  app.add_flag("--no-milestone", no_milestone, "Do not export milestone.");

  CLI11_PARSE(app, argc, argv);

  if (opt_removed_issue->count() > 0) {
    std::cerr << "Error: --issue has been removed. Use --id instead.\n";
    return 2;
  }
  if (opt_removed_pr->count() > 0) {
    std::cerr << "Error: --pr has been removed. Use --id instead.\n";
    return 2;
  }

  include_issues = !no_issues;
  include_prs = !no_prs;
  include_body = !no_body;
  include_comments = !no_comments;
  include_labels = !no_labels;
  include_reactions = !no_reactions;
  include_authors = !no_authors;
  include_timestamps = !no_timestamps;
  include_links = !no_links;
  include_assignees = !no_assignees;
  include_milestone = !no_milestone;
  progress_enabled = !no_progress;
  if (quiet) {
    // Quiet means no non-error output. Still perform all file writes.
    progress_enabled = false;
  }

  // Parse repo_arg once: allow /owner/repo, remote URLs, and GitHub web URLs (repo/issues/pr).
  auto parsed_repo_arg = parse_repo_arg(repo_arg);
  if (parsed_repo_arg.id > 0 && id > 0 && parsed_repo_arg.id != id) {
    std::cerr << "Error: conflicting id: --id and --repo URL number differ.\n";
    return 2;
  }
  if (id <= 0 && parsed_repo_arg.id > 0) id = parsed_repo_arg.id;

  if (id > 0 && (!include_issues || !include_prs)) {
    // With --id we don't pre-classify Issue/PR; treat --no-issues/--no-prs as conflicting.
    std::cerr << "Error: --id cannot be used with --no-issues/--no-prs.\n";
    return 2;
  }

  if (per_item && split_n > 0) {
    std::cerr << "Error: --per-item and --split are mutually exclusive.\n";
    return 2;
  }

  try {
    RepoExport repo;
    const bool include_pr_review_decision =
        (pr_review_mode == "decision" || pr_review_mode == "reviews" || pr_review_mode == "threads");
    const bool include_pr_reviews = (pr_review_mode == "reviews" || pr_review_mode == "threads");
    const bool include_pr_review_threads = (pr_review_mode == "threads");
    if (!in_json.empty()) {
      if (pr_review_mode != "none") {
        throw std::runtime_error(
            "--pr-review is not supported with --in JSON mode. Use online mode (GraphQL fetch) instead.");
      }
      auto j = read_json_file(in_json);
      ConvertOptions copt;
      copt.include_body = include_body;
      copt.include_comments = include_comments;
      copt.include_labels = include_labels;
      copt.include_reactions = include_reactions;
      copt.include_authors = include_authors;
      copt.include_timestamps = include_timestamps;
      copt.include_links = include_links;
      copt.include_assignees = include_assignees;
      copt.include_milestone = include_milestone;
      repo = convert_from_issue_list_json(j, parsed_repo_arg.owner_repo, copt);

      if (id > 0) {
        std::vector<Item> filtered;
        for (const auto& it : repo.items) {
          if (it.number == id) filtered.push_back(it);
        }
        repo.items = std::move(filtered);
        if (repo.items.empty()) {
          throw std::runtime_error("item not found in --in JSON: #" + std::to_string(id) +
                                   " (note: --in JSON is from `gh issue list`, so it typically contains issues only)");
        }
      }
    } else {
      std::string owner_repo;
      if (!parsed_repo_arg.owner_repo.empty()) {
        owner_repo = parsed_repo_arg.owner_repo;
      } else {
        try {
          owner_repo = infer_repo_from_git_remote(fs::current_path().string());
        } catch (const std::exception& e) {
          throw std::runtime_error(std::string(e.what()) + "\nHint: pass --repo owner/name");
        }
      }
      owner_repo = normalize_owner_repo_arg(owner_repo);
      auto slash = owner_repo.find('/');
      if (slash == std::string::npos || slash == 0 || slash + 1 >= owner_repo.size()) {
        throw std::runtime_error("--repo must be owner/name");
      }
      std::string owner = owner_repo.substr(0, slash);
      std::string name = owner_repo.substr(slash + 1);

      FetchOptions fopt;
      fopt.include_issues = include_issues;
      fopt.include_prs = include_prs;
      fopt.state = state;
      fopt.limit = limit;
      fopt.include_body = include_body;
      fopt.include_comments = include_comments;
      fopt.include_labels = include_labels;
      fopt.include_reactions = include_reactions;
      fopt.include_authors = include_authors;
      fopt.include_timestamps = include_timestamps;
      fopt.include_links = include_links;
      fopt.include_assignees = include_assignees;
      fopt.include_milestone = include_milestone;
      fopt.include_pr_review_decision = include_pr_review_decision;
      fopt.include_pr_reviews = include_pr_reviews;
      fopt.include_pr_review_threads = include_pr_review_threads;
      fopt.hostname = hostname;
      fopt.reverse_order = reverse_order;
      fopt.progress_enabled = progress_enabled;
      fopt.progress_interval_ms = progress_interval_ms;
      fopt.labels_first = labels_first;
      fopt.assignees_first = assignees_first;
      if (id > 0) {
        repo = fetch_item_by_number_via_gh_graphql(owner, name, id, fopt);
      } else {
        repo = fetch_repo_via_gh_graphql(owner, name, fopt);
      }
    }

    auto pp = post_process_repo(repo, include_issues, include_prs, state, limit, reverse_order);

    if (id > 0 && repo.items.empty()) {
      throw std::runtime_error("item not found or filtered out by --state");
    }

    if (progress_enabled) {
      const int keep_n = static_cast<int>(repo.items.size());
      const int total_n = pp.total_available;
      std::ostringstream oss;
      oss << "Selecting items: keep " << keep_n << "/" << total_n;
      if (limit > 0) oss << " (--limit=" << limit << ")";
      std::cerr << oss.str() << "\n";
    }

    StatsSummary stats = compute_stats(repo);
    if (!stats_json_path.empty()) {
      write_text_file(stats_json_path, stats_to_json(stats).dump(2));
    }

    RenderOptions ropt;
    ropt.title = title;
    ropt.include_body = include_body;
    ropt.include_comments = include_comments;
    ropt.include_pr_review_decision = include_pr_review_decision;
    ropt.include_pr_reviews = include_pr_reviews;
    ropt.include_pr_review_threads = include_pr_review_threads;
    ropt.include_labels = include_labels;
    ropt.include_reactions = include_reactions;
    ropt.include_authors = include_authors;
    ropt.include_timestamps = include_timestamps;
    ropt.include_links = include_links;
    ropt.include_assignees = include_assignees;
    ropt.include_milestone = include_milestone;
    ropt.idempotent = idempotent;

    ExportMetadata meta;
    meta.state = state;
    meta.include_issues = include_issues;
    meta.include_prs = include_prs;
    meta.reverse_order = reverse_order;
    meta.limit = limit;
    meta.truncated = pp.truncated;
    meta.total_available = pp.total_available;
    meta.min_number = pp.min_number;
    meta.max_number = pp.max_number;
    meta.min_created_at = pp.min_created_at;
    meta.max_created_at = pp.max_created_at;

    auto wr = write_markdown_output(repo, ropt, meta, out_path, split_n, per_item, progress_enabled, progress_interval_ms);
    if (!quiet) {
      if (!progress_enabled) {
        std::cerr << wr.wrote_message << "\n";
      }
      std::cout << stats_to_pretty_text(stats) << "\n";
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}


