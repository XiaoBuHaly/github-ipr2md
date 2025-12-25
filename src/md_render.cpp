#include "md_render.h"

#include <algorithm>
#include <ctime>
#include <iostream>
#include <sstream>

namespace ghx {

static std::string one_line(std::string s) {
  for (auto& c : s) {
    if (c == '\n' || c == '\r') c = ' ';
  }
  return s;
}

static std::string kind_str(ItemKind k) {
  return (k == ItemKind::PullRequest) ? "PR" : "Issue";
}

static std::string join_backticked_sorted_labels(const std::vector<Label>& labels) {
  std::vector<std::string> names;
  names.reserve(labels.size());
  for (const auto& l : labels) if (!l.name.empty()) names.push_back(l.name);
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  std::ostringstream oss;
  for (size_t i = 0; i < names.size(); i++) {
    if (i) oss << ", ";
    oss << "`" << names[i] << "`";
  }
  return oss.str();
}

static std::string join_users_sorted(const std::vector<User>& users) {
  std::vector<std::string> names;
  for (const auto& u : users) if (!u.login.empty()) names.push_back(u.login);
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  std::ostringstream oss;
  for (size_t i = 0; i < names.size(); i++) {
    if (i) oss << ", ";
    oss << names[i];
  }
  return oss.str();
}

static std::string reactions_inline(const std::vector<ReactionGroup>& rgs) {
  std::ostringstream oss;
  bool first = true;
  for (const auto& rg : rgs) {
    if (rg.total_count <= 0 || rg.content.empty()) continue;
    if (!first) oss << ", ";
    first = false;
    oss << rg.content << " " << rg.total_count;
  }
  return oss.str();
}

void write_item_markdown(std::ostream& out, const Item& it, const RenderOptions& opt) {

  out << "## #" << it.number << " [" << kind_str(it.kind) << "] " << one_line(it.title) << "\n\n";

  // Meta block
  out << "- Type: " << kind_str(it.kind) << "\n";
  if (!it.state.empty()) out << "- State: " << it.state << "\n";
  if (opt.include_links && !it.url.empty()) out << "- URL: " << it.url << "\n";

  if (opt.include_authors && it.author && !it.author->login.empty()) {
    out << "- Author: " << it.author->login;
    if (!it.author_association.empty()) out << " (" << it.author_association << ")";
    if (opt.include_links && !it.author->url.empty()) out << " · " << it.author->url;
    out << "\n";
  }

  if (opt.include_assignees && !it.assignees.empty()) {
    out << "- Assignees: " << join_users_sorted(it.assignees) << "\n";
  }

  if (opt.include_milestone && it.milestone) {
    out << "- Milestone: " << it.milestone->title;
    if (!it.milestone->state.empty()) out << " (" << it.milestone->state << ")";
    out << "\n";
  }

  if (opt.include_timestamps) {
    if (!it.created_at.empty()) out << "- CreatedAt: " << it.created_at << "\n";
    if (!it.updated_at.empty()) out << "- UpdatedAt: " << it.updated_at << "\n";
    if (!it.closed_at.empty()) out << "- ClosedAt: " << it.closed_at << "\n";
    if (it.kind == ItemKind::PullRequest && !it.merged_at.empty()) out << "- MergedAt: " << it.merged_at << "\n";
  }

  if (opt.include_labels && !it.labels.empty()) {
    out << "- Labels: " << join_backticked_sorted_labels(it.labels) << "\n";
  }

  if (opt.include_reactions && !it.reactions.empty()) {
    auto r = reactions_inline(it.reactions);
    if (!r.empty()) out << "- Reactions: " << r << "\n";
  }

  out << "\n";

  if (opt.include_body) {
    out << "### Body\n\n";
    out << (it.body.empty() ? "(empty)\n" : it.body + "\n");
    out << "\n";
  }

  if (opt.include_comments) {
    out << "### Comments (" << (it.comments_total_count > 0 ? it.comments_total_count : (int)it.comments.size()) << ")\n\n";
    if (it.comments.empty()) {
      out << "(none)\n\n";
    } else {
      int idx = 1;
      for (const auto& c : it.comments) {
        out << idx++ << ". ";
        if (opt.include_authors && c.author && !c.author->login.empty()) {
          out << "**" << c.author->login << "**";
          if (!c.author_association.empty()) out << " (" << c.author_association << ")";
        } else {
          out << "**(unknown)**";
        }
        if (opt.include_timestamps && !c.created_at.empty()) out << " · " << c.created_at;
        if (opt.include_links && !c.url.empty()) out << " · " << c.url;
        out << "\n\n";
        if (opt.include_body) {
          out << (c.body.empty() ? "(empty)\n" : c.body + "\n");
        } else {
          out << "(body omitted)\n";
        }
        if (opt.include_reactions && !c.reactions.empty()) {
          auto cr = reactions_inline(c.reactions);
          if (!cr.empty()) out << "\nReactions: " << cr << "\n";
        }
        out << "\n";
      }
    }
  }

  out << "\n---\n\n";
}

static std::string now_string() {
  std::time_t t = std::time(nullptr);
  char buf[64];
  std::tm tm{};
  #if defined(_WIN32)
  localtime_s(&tm, &t);
  #else
  localtime_r(&t, &tm);
  #endif
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return std::string(buf);
}

static std::string include_str(const ExportMetadata& m) {
  if (m.include_issues && m.include_prs) return "issues+prs";
  if (m.include_issues) return "issues";
  if (m.include_prs) return "prs";
  return "none";
}

void write_repo_preamble(
    std::ostream& out,
    const RepoExport& repo,
    const RenderOptions& opt,
    const StatsSummary* stats,
    const ExportMetadata* meta) {
  out << "# " << opt.title << "\n\n";

  if (!repo.full_name.empty() && repo.full_name != "unknown/unknown") {
    out << "- Repo: " << repo.full_name;
    if (opt.include_links && !repo.url.empty()) out << " · " << repo.url;
    out << "\n";
  }
  if (!opt.idempotent) {
    out << "- GeneratedAt: " << now_string() << "\n";
  }
  out << "- Items: " << repo.items.size() << "\n\n";

  if (meta) {
    out << "- Filter: state=" << meta->state << " include=" << include_str(*meta) << "\n";
    out << "- Order: createdAt " << (meta->reverse_order ? "DESC" : "ASC") << "\n";
    if (meta->limit > 0) {
      out << "- Limit: " << meta->limit << "\n";
      out << "- Truncated: " << (meta->truncated ? "true" : "false");
      if (meta->total_available > 0) out << " (available=" << meta->total_available << ")";
      out << "\n";
    }
    if (meta->min_number > 0 && meta->max_number > 0) {
      out << "- Range: #" << meta->min_number << "-#" << meta->max_number << "\n";
    }
    if (!meta->min_created_at.empty() && !meta->max_created_at.empty()) {
      out << "- CreatedAtRange: " << meta->min_created_at << " .. " << meta->max_created_at << "\n";
    }
    out << "\n";
  }

  if (opt.include_stats_section && stats) {
    out << "## Stats\n\n";
    out << "```\n" << stats_to_pretty_text(*stats) << "```\n\n";
  }
}

void write_repo_markdown(
    std::ostream& out,
    const RepoExport& repo,
    const RenderOptions& opt,
    const StatsSummary* stats,
    const ExportMetadata* meta) {
  write_repo_preamble(out, repo, opt, stats, meta);

  for (const auto& it : repo.items) {
    write_item_markdown(out, it, opt);
  }
}

std::string render_item_markdown(const Item& it, const RenderOptions& opt) {
  std::ostringstream out;
  write_item_markdown(out, it, opt);
  return out.str();
}

std::string render_repo_markdown(const RepoExport& repo, const RenderOptions& opt, const StatsSummary* stats, const ExportMetadata* meta) {
  std::ostringstream out;
  write_repo_markdown(out, repo, opt, stats, meta);
  return out.str();
}

}  // namespace ghx


