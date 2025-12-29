#pragma once

#include "json_model.h"

#include <string>

namespace ghx {

struct FetchOptions {
  bool include_issues = true;
  bool include_prs = true;

  // all|open|closed
  std::string state = "all";

  // Maximum number of items (issues+prs). 0 means unlimited.
  int limit = 0;

  // Match output ordering: default false means oldest-first (ASC). If true, newest-first (DESC).
  bool reverse_order = false;

  // Progress output (stderr) during fetch.
  bool progress_enabled = true;
  int progress_interval_ms = 100;

  bool include_body = true;
  bool include_comments = true;
  bool include_labels = true;
  bool include_reactions = true;
  bool include_authors = true;
  bool include_timestamps = true;
  bool include_links = true;  // render-only, but we keep it here for symmetry
  bool include_assignees = true;
  bool include_milestone = true;

  int per_page = 100;
  int comment_per_page = 100;
  // Per-item nested connections. These are NOT fully paginated today; values cap results.
  // GitHub GraphQL generally caps `first` at 100 for connections.
  int labels_first = 100;      // labels(first: N)
  int assignees_first = 20;    // assignees(first: N)
  std::string hostname = "";  // optional gh --hostname value
};

// Fetches repo issues+PRs via `gh api graphql ...`, including comment pagination.
RepoExport fetch_repo_via_gh_graphql(const std::string& owner, const std::string& repo, const FetchOptions& opt);

// Fetches a single issue-or-pull-request by number via `gh api graphql ...`, including comment pagination.
// Implementation queries both issue(number) and pullRequest(number) in one request, and expects exactly one to exist.
// Throws std::runtime_error if not found / ambiguous / if `gh` fails.
RepoExport fetch_item_by_number_via_gh_graphql(
    const std::string& owner,
    const std::string& repo,
    int number,
    const FetchOptions& opt);

}  // namespace ghx


