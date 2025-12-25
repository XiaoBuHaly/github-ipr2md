#pragma once

#include "json_model.h"
#include "stats.h"

#include <iosfwd>
#include <string>

namespace ghx {

struct ExportMetadata {
  // Filters / ordering used for this output.
  std::string state = "all";
  bool include_issues = true;
  bool include_prs = true;
  bool reverse_order = false;  // false=ASC(old->new), true=DESC(new->old)

  // Truncation info.
  int limit = 0;                // 0 means unlimited
  bool truncated = false;       // true if the result set was cut by limit
  int total_available = 0;      // count after filter, before limit

  // Range info for the written output (after limit).
  int min_number = 0;
  int max_number = 0;
  std::string min_created_at;
  std::string max_created_at;
};

struct RenderOptions {
  std::string title = "Issues Export";

  bool include_body = true;
  bool include_comments = true;
  bool include_labels = true;
  bool include_reactions = true;
  bool include_authors = true;
  bool include_timestamps = true;
  bool include_links = true;
  bool include_assignees = true;
  bool include_milestone = true;

  bool include_stats_section = false;  // if true, embed a stats section in the markdown
  bool idempotent = false;
};

void write_repo_preamble(
    std::ostream& os,
    const RepoExport& repo,
    const RenderOptions& opt,
    const StatsSummary* stats,
    const ExportMetadata* meta = nullptr);
void write_item_markdown(std::ostream& os, const Item& it, const RenderOptions& opt);
void write_repo_markdown(
    std::ostream& os,
    const RepoExport& repo,
    const RenderOptions& opt,
    const StatsSummary* stats,
    const ExportMetadata* meta = nullptr);

std::string render_repo_markdown(const RepoExport& repo, const RenderOptions& opt, const StatsSummary* stats, const ExportMetadata* meta = nullptr);
std::string render_item_markdown(const Item& it, const RenderOptions& opt);

}  // namespace ghx


