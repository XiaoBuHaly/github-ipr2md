#pragma once

#include "json_model.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>

namespace ghx {

struct StatsSummary {
  int total_items = 0;
  int total_issues = 0;
  int total_prs = 0;

  std::map<std::string, int> by_state;   // OPEN/CLOSED/MERGED/...
  std::map<std::string, int> by_label;   // label name -> count
  std::map<std::string, int> by_author;  // login -> count
};

StatsSummary compute_stats(const RepoExport& repo);
std::string stats_to_pretty_text(const StatsSummary& s, int top_n_labels = 20);
nlohmann::json stats_to_json(const StatsSummary& s);

}  // namespace ghx


