#pragma once

#include "json_model.h"

#include <nlohmann/json.hpp>

#include <string>

// nlohmann/json version gate:
// This project pins nlohmann/json via CMake FetchContent, but keep a compile-time
// guard so accidental downgrades (system package overrides, etc.) fail loudly.
#ifndef NLOHMANN_JSON_VERSION_MAJOR
#error "nlohmann/json version macros not found; please use nlohmann/json >= 3.12.0"
#endif

#if (NLOHMANN_JSON_VERSION_MAJOR < 3) || \
    (NLOHMANN_JSON_VERSION_MAJOR == 3 && NLOHMANN_JSON_VERSION_MINOR < 12)
#error "github-ipr2md requires nlohmann/json >= 3.12.0"
#endif

namespace ghx {

struct ConvertOptions {
  bool include_body = true;
  bool include_comments = true;
  bool include_labels = true;
  bool include_reactions = true;
  bool include_authors = true;
  bool include_timestamps = true;
  bool include_links = true;
  bool include_assignees = true;
  bool include_milestone = true;
};

// Converts the JSON produced by:
//   gh issue list --json number,title,body,labels,comments,state
// (optionally augmented with more fields) into RepoExport.
RepoExport convert_from_issue_list_json(
    const nlohmann::json& root_array,
    const std::string& repo_full_name_hint,
    const ConvertOptions& opt);

}  // namespace ghx


