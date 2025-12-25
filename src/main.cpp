#include "gh_graphql.h"
#include "json_convert.h"
#include "md_render.h"
#include "progress.h"
#include "repo_detect.h"
#include "stats.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace ghx;

static nlohmann::json read_json_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("failed to open input json: " + path);
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return nlohmann::json::parse(data);
}

static void write_text_file(const fs::path& path, const std::string& content) {
  // If writing to a file in the current directory (e.g. "issues.md"),
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
  std::string out_path = "issues.md";
  std::string state = "all";
  int limit = 0;
  int split_n = 0;
  bool per_item = false;
  bool idempotent = false;
  std::string title = "Issues Export";
  std::string stats_json_path;
  std::string hostname;

  bool include_issues = true;
  bool include_prs = true;
  bool include_body = true;
  bool include_comments = true;
  bool include_labels = true;
  bool include_reactions = true;
  bool include_authors = true;
  bool include_timestamps = true;
  bool include_links = true;
  bool include_assignees = true;
  bool include_milestone = true;
  bool include_stats = true;

  bool reverse_order = false;
  bool progress_enabled = true;
  bool no_progress = false;
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
  bool no_stats = false;

  int labels_first = 100;
  int assignees_first = 20;

  app.add_option("--repo", repo_arg, "Repo in owner/name format. Default: infer from git remote.");
  app.add_option("--in", in_json, "Convert existing JSON (from `gh issue list --json ...`) to Markdown.");
  app.add_option("--out", out_path, "Output path. Default: ./issues.md (or a directory when --split/--per-item).");
  app.add_option("--state", state, "Filter state: all|open|closed. Default: all.")
      ->check(CLI::IsMember({"all", "open", "closed"}));
  app.add_option("--limit", limit, "Max number of items (issues+prs). 0 means unlimited. Default: 0.");
  app.add_flag("--reverse", reverse_order, "Reverse output order (default is ascending by number).");
  app.add_flag("--no-progress", no_progress, "Disable progress output.");
  app.add_option("--progress-interval-ms", progress_interval_ms, "Progress refresh interval in ms. Default: 100.");
  app.add_option("--split", split_n, "Split output into multiple files, each file max N items. Requires --out be a directory.");
  app.add_flag("--per-item", per_item, "Write one file per issue/PR. Requires --out be a directory. Mutually exclusive with --split.");
  app.add_flag("--idempotent", idempotent, "Deterministic output: no generated timestamps; stable sorting where applicable.");
  app.add_option("--title", title, "Markdown document title. Default: \"Issues Export\".");
  app.add_option("--stats-json", stats_json_path, "Write stats as JSON to this path.");
  app.add_option("--hostname", hostname, "GitHub hostname for `gh api` (default: github.com).");

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
  app.add_flag("--no-stats", no_stats, "Do not print stats to stdout by default.");

  CLI11_PARSE(app, argc, argv);

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
  include_stats = !no_stats;
  progress_enabled = !no_progress;

  if (per_item && split_n > 0) {
    std::cerr << "Error: --per-item and --split are mutually exclusive.\n";
    return 2;
  }

  try {
    RepoExport repo;
    if (!in_json.empty()) {
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
      repo = convert_from_issue_list_json(j, repo_arg, copt);
    } else {
      std::string owner_repo = repo_arg.empty() ? infer_repo_from_git_remote(fs::current_path().string()) : repo_arg;
      auto slash = owner_repo.find('/');
      if (slash == std::string::npos) throw std::runtime_error("--repo must be owner/name");
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
      fopt.hostname = hostname;
      fopt.reverse_order = reverse_order;
      fopt.progress_enabled = progress_enabled;
      fopt.progress_interval_ms = progress_interval_ms;
      fopt.labels_first = labels_first;
      fopt.assignees_first = assignees_first;
      repo = fetch_repo_via_gh_graphql(owner, name, fopt);
    }

    auto pp = post_process_repo(repo, include_issues, include_prs, state, limit, reverse_order);

    StatsSummary stats = compute_stats(repo);
    if (include_stats) {
      std::cout << stats_to_pretty_text(stats) << "\n";
    }
    if (!stats_json_path.empty()) {
      write_text_file(stats_json_path, stats_to_json(stats).dump(2));
    }

    RenderOptions ropt;
    ropt.title = title;
    ropt.include_body = include_body;
    ropt.include_comments = include_comments;
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
      for (size_t i = 0; i < total; i++) {
        write_item_markdown(out, repo.items[i], ropt);
        progress.tick("Writing markdown: " + std::to_string(i + 1) + "/" + std::to_string(total), false);
      }
      out.flush();
      progress.done("Wrote: " + out_path);
      return 0;
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
      for (const auto& it : repo.items) {
        idx++;
        std::string prefix = (it.kind == ghx::ItemKind::PullRequest) ? "pr" : "issue";
        fs::path p = out_dir / (prefix + "-" + std::to_string(it.number) + ".md");
        const auto parent = p.parent_path();
        if (!parent.empty()) fs::create_directories(parent);
        std::ofstream out(p, std::ios::binary);
        if (!out) throw std::runtime_error("failed to open output: " + p.string());
        write_item_markdown(out, it, ropt);
        progress.tick("Writing per-item: " + std::to_string(idx) + "/" + std::to_string(repo.items.size()), false);
      }
      progress.done("Wrote directory: " + out_dir.string());
      return 0;
    }

    // split_n chunked
    int chunk = 0;
    const size_t total_items = repo.items.size();
    const size_t chunk_size = static_cast<size_t>(split_n);
    const size_t total_chunks = (total_items + chunk_size - 1) / chunk_size;
    const int chunk_width =
        std::max(2, static_cast<int>(std::to_string(std::max<size_t>(1, total_chunks)).size()));
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

    progress.done("Wrote directory: " + out_dir.string());
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}


