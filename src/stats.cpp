#include "stats.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace ghx {

StatsSummary compute_stats(const RepoExport& repo) {
  StatsSummary s;
  s.total_items = static_cast<int>(repo.items.size());
  for (const auto& it : repo.items) {
    if (it.kind == ItemKind::Issue) s.total_issues++;
    if (it.kind == ItemKind::PullRequest) s.total_prs++;
    if (!it.state.empty()) s.by_state[it.state]++;
    if (it.author && !it.author->login.empty()) s.by_author[it.author->login]++;
    for (const auto& lab : it.labels) {
      if (!lab.name.empty()) s.by_label[lab.name]++;
    }
  }
  return s;
}

static std::vector<std::pair<std::string, int>> sorted_top(
    const std::map<std::string, int>& m,
    int top_n) {
  std::vector<std::pair<std::string, int>> v(m.begin(), m.end());
  std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
    if (a.second != b.second) return a.second > b.second;
    return a.first < b.first;
  });
  if (top_n > 0 && static_cast<int>(v.size()) > top_n) v.resize(static_cast<size_t>(top_n));
  return v;
}

std::string stats_to_pretty_text(const StatsSummary& s, int top_n_labels) {
  std::ostringstream oss;
  oss << "Stats:\n";
  oss << "  total_items: " << s.total_items << "\n";
  oss << "  total_issues: " << s.total_issues << "\n";
  oss << "  total_prs: " << s.total_prs << "\n";

  if (!s.by_state.empty()) {
    oss << "  by_state:\n";
    for (const auto& kv : s.by_state) {
      oss << "    " << kv.first << ": " << kv.second << "\n";
    }
  }

  if (!s.by_label.empty()) {
    oss << "  top_labels:\n";
    for (const auto& kv : sorted_top(s.by_label, top_n_labels)) {
      oss << "    " << kv.first << ": " << kv.second << "\n";
    }
  }

  return oss.str();
}

nlohmann::json stats_to_json(const StatsSummary& s) {
  nlohmann::json j;
  j["total_items"] = s.total_items;
  j["total_issues"] = s.total_issues;
  j["total_prs"] = s.total_prs;
  j["by_state"] = s.by_state;
  j["by_label"] = s.by_label;
  j["by_author"] = s.by_author;
  return j;
}

}  // namespace ghx


