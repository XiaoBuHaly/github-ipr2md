#include "md_render.h"
#include "i18n.h"

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

static std::string yes_no(bool v) { return std::string(I18n::t(v ? "md.value.true" : "md.value.false")); }

static void write_pr_review_section(std::ostream& out, const Item& it, const RenderOptions& opt) {
  if (it.kind != ItemKind::PullRequest) return;

  if (opt.include_pr_review_decision) {
    out << "### " << I18n::t("md.pr_review_decision") << "\n\n";
    out << (it.pr_review_decision.empty() ? std::string(I18n::t("md.none")) + "\n\n"
                                         : (it.pr_review_decision + "\n\n"));
  }

  if (opt.include_pr_reviews) {
    out << "### " << I18n::t("md.pr_reviews") << " ("
        << (it.pr_reviews_total_count > 0 ? it.pr_reviews_total_count : (int)it.pr_reviews.size()) << ")\n\n";
    if (it.pr_reviews.empty()) {
      out << I18n::t("md.none") << "\n\n";
    } else {
      int idx = 1;
      for (const auto& r : it.pr_reviews) {
        out << idx++ << ". ";
        if (opt.include_authors && r.author && !r.author->login.empty()) {
          out << "**" << r.author->login << "**";
          if (!r.author_association.empty()) out << " (" << r.author_association << ")";
        } else {
          out << "**" << I18n::t("md.unknown") << "**";
        }
        if (!r.state.empty()) out << " · " << r.state;
        if (opt.include_timestamps && !r.submitted_at.empty()) out << " · " << r.submitted_at;
        if (opt.include_links && !r.url.empty()) out << " · " << r.url;
        out << "\n\n";
        if (opt.include_body) {
          out << (r.body.empty() ? std::string(I18n::t("md.empty")) + "\n" : r.body + "\n");
        } else {
          out << I18n::t("md.body_omitted") << "\n";
        }
        if (opt.include_reactions && !r.reactions.empty()) {
          auto rr = reactions_inline(r.reactions);
          if (!rr.empty()) out << "\n" << I18n::tf("md.reactions", {{"reactions", rr}}) << "\n";
        }
        out << "\n";
      }
    }
  }

  if (opt.include_pr_review_threads) {
    out << "### " << I18n::t("md.pr_review_threads") << " ("
        << (it.pr_review_threads_total_count > 0 ? it.pr_review_threads_total_count : (int)it.pr_review_threads.size())
        << ")\n\n";
    if (it.pr_review_threads.empty()) {
      out << I18n::t("md.none") << "\n\n";
    } else {
      int tidx = 1;
      for (const auto& t : it.pr_review_threads) {
        out << tidx++ << ". ";
        out << "`" << (t.path.empty() ? std::string(I18n::t("md.unknown")) : t.path) << "`";
        if (t.line > 0) out << " " << I18n::t("md.thread.line") << "=" << t.line;
        if (t.original_line > 0) out << " " << I18n::t("md.thread.original_line") << "=" << t.original_line;
        out << " " << I18n::t("md.thread.resolved") << "=" << yes_no(t.is_resolved)
            << " " << I18n::t("md.thread.outdated") << "=" << yes_no(t.is_outdated) << "\n\n";

        const int cc = (t.comments_total_count > 0 ? t.comments_total_count : (int)t.comments.size());
        out << I18n::tf("md.thread_comments", {{"count", std::to_string(cc)}}) << "\n\n";
        if (t.comments.empty()) {
          out << I18n::t("md.none") << "\n\n";
        } else {
          int cidx = 1;
          for (const auto& c : t.comments) {
            out << cidx++ << ". ";
            if (opt.include_authors && c.author && !c.author->login.empty()) {
              out << "**" << c.author->login << "**";
              if (!c.author_association.empty()) out << " (" << c.author_association << ")";
            } else {
              out << "**" << I18n::t("md.unknown") << "**";
            }
            if (opt.include_timestamps && !c.created_at.empty()) out << " · " << c.created_at;
            if (opt.include_links && !c.url.empty()) out << " · " << c.url;
            out << "\n\n";
            if (opt.include_body) {
              out << (c.body.empty() ? std::string(I18n::t("md.empty")) + "\n" : c.body + "\n");
            } else {
              out << I18n::t("md.body_omitted") << "\n";
            }
            if (opt.include_reactions && !c.reactions.empty()) {
              auto cr = reactions_inline(c.reactions);
              if (!cr.empty()) out << "\n" << I18n::tf("md.reactions", {{"reactions", cr}}) << "\n";
            }
            out << "\n";
          }
        }
      }
    }
  }
}

void write_item_markdown(std::ostream& out, const Item& it, const RenderOptions& opt) {

  out << "## #" << it.number << " [" << kind_str(it.kind) << "] " << one_line(it.title) << "\n\n";

  // Meta block
  out << "- " << I18n::t("md.meta.type") << ": " << kind_str(it.kind) << "\n";
  if (!it.state.empty()) out << "- " << I18n::t("md.meta.state") << ": " << it.state << "\n";
  if (opt.include_links && !it.url.empty()) out << "- " << I18n::t("md.meta.url") << ": " << it.url << "\n";

  if (opt.include_authors && it.author && !it.author->login.empty()) {
    out << "- " << I18n::t("md.meta.author") << ": " << it.author->login;
    if (!it.author_association.empty()) out << " (" << it.author_association << ")";
    if (opt.include_links && !it.author->url.empty()) out << " · " << it.author->url;
    out << "\n";
  }

  if (opt.include_assignees && !it.assignees.empty()) {
    out << "- " << I18n::t("md.meta.assignees") << ": " << join_users_sorted(it.assignees) << "\n";
  }

  if (opt.include_milestone && it.milestone) {
    out << "- " << I18n::t("md.meta.milestone") << ": " << it.milestone->title;
    if (!it.milestone->state.empty()) out << " (" << it.milestone->state << ")";
    out << "\n";
  }

  if (opt.include_timestamps) {
    if (!it.created_at.empty()) out << "- " << I18n::t("md.meta.created_at") << ": " << it.created_at << "\n";
    if (!it.updated_at.empty()) out << "- " << I18n::t("md.meta.updated_at") << ": " << it.updated_at << "\n";
    if (!it.closed_at.empty()) out << "- " << I18n::t("md.meta.closed_at") << ": " << it.closed_at << "\n";
    if (it.kind == ItemKind::PullRequest && !it.merged_at.empty()) out << "- " << I18n::t("md.meta.merged_at") << ": " << it.merged_at << "\n";
  }

  if (opt.include_labels && !it.labels.empty()) {
    out << "- " << I18n::t("md.meta.labels") << ": " << join_backticked_sorted_labels(it.labels) << "\n";
  }

  if (opt.include_reactions && !it.reactions.empty()) {
    auto r = reactions_inline(it.reactions);
    if (!r.empty()) out << "- " << I18n::tf("md.reactions", {{"reactions", r}}) << "\n";
  }

  out << "\n";

  if (opt.include_body) {
    out << "### " << I18n::t("md.body") << "\n\n";
    out << (it.body.empty() ? std::string(I18n::t("md.empty")) + "\n" : it.body + "\n");
    out << "\n";
  }

  write_pr_review_section(out, it, opt);

  if (opt.include_comments) {
    out << "### " << I18n::t("md.comments") << " ("
        << (it.comments_total_count > 0 ? it.comments_total_count : (int)it.comments.size()) << ")\n\n";
    if (it.comments.empty()) {
      out << I18n::t("md.none") << "\n\n";
    } else {
      int idx = 1;
      for (const auto& c : it.comments) {
        out << idx++ << ". ";
        if (opt.include_authors && c.author && !c.author->login.empty()) {
          out << "**" << c.author->login << "**";
          if (!c.author_association.empty()) out << " (" << c.author_association << ")";
        } else {
          out << "**" << I18n::t("md.unknown") << "**";
        }
        if (opt.include_timestamps && !c.created_at.empty()) out << " · " << c.created_at;
        if (opt.include_links && !c.url.empty()) out << " · " << c.url;
        out << "\n\n";
        if (opt.include_body) {
          out << (c.body.empty() ? std::string(I18n::t("md.empty")) + "\n" : c.body + "\n");
        } else {
          out << I18n::t("md.body_omitted") << "\n";
        }
        if (opt.include_reactions && !c.reactions.empty()) {
          auto cr = reactions_inline(c.reactions);
          if (!cr.empty()) out << "\n" << I18n::tf("md.reactions", {{"reactions", cr}}) << "\n";
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
  if (m.include_issues && m.include_prs) return std::string(I18n::t("md.include.issues_prs"));
  if (m.include_issues) return std::string(I18n::t("md.include.issues"));
  if (m.include_prs) return std::string(I18n::t("md.include.prs"));
  return std::string(I18n::t("md.include.none"));
}

void write_repo_preamble(
    std::ostream& out,
    const RepoExport& repo,
    const RenderOptions& opt,
    const StatsSummary* stats,
    const ExportMetadata* meta) {
  out << "# " << opt.title << "\n\n";

  if (!repo.full_name.empty() && repo.full_name != "unknown/unknown") {
    out << "- " << I18n::t("md.preamble.repo") << ": " << repo.full_name;
    if (opt.include_links && !repo.url.empty()) out << " · " << repo.url;
    out << "\n";
  }
  if (!opt.idempotent) {
    out << "- " << I18n::t("md.preamble.generated_at") << ": " << now_string() << "\n";
  }
  out << "- " << I18n::t("md.preamble.items") << ": " << repo.items.size() << "\n\n";

  if (meta) {
    out << "- " << I18n::t("md.preamble.filter") << ": state=" << meta->state << " include=" << include_str(*meta) << "\n";
    out << "- " << I18n::t("md.preamble.order") << ": createdAt " << (meta->reverse_order ? "DESC" : "ASC") << "\n";
    if (meta->limit > 0) {
      out << "- " << I18n::t("md.preamble.limit") << ": " << meta->limit << "\n";
      out << "- " << I18n::t("md.preamble.truncated") << ": "
          << I18n::t(meta->truncated ? "md.value.true" : "md.value.false");
      if (meta->total_available > 0) out << " (" << I18n::t("md.preamble.available") << "=" << meta->total_available << ")";
      out << "\n";
    }
    if (meta->min_number > 0 && meta->max_number > 0) {
      out << "- " << I18n::t("md.preamble.range") << ": #" << meta->min_number << "-#" << meta->max_number << "\n";
    }
    if (!meta->min_created_at.empty() && !meta->max_created_at.empty()) {
      out << "- " << I18n::t("md.preamble.created_at_range") << ": " << meta->min_created_at << " .. " << meta->max_created_at << "\n";
    }
    out << "\n";
  }

  if (opt.include_stats_section && stats) {
    out << "## " << I18n::t("md.preamble.stats") << "\n\n";
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


