#include "json_convert.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace ghx {

static std::string get_string(const nlohmann::json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) return "";
  if (it->is_string()) return it->get<std::string>();
  return it->dump();
}

static int get_int(const nlohmann::json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) return 0;
  if (it->is_number_integer()) return it->get<int>();
  if (it->is_string()) {
    std::string s = it->get<std::string>();
    // trim ASCII whitespace to be tolerant of CLI/JSON formatting quirks
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), is_space));
    s.erase(std::find_if_not(s.rbegin(), s.rend(), is_space).base(), s.end());
    if (s.empty()) return 0;
    try {
      size_t idx = 0;
      int v = std::stoi(s, &idx, 10);
      if (idx != s.size()) return 0;  // reject partial parses like "123abc"
      return v;
    } catch (const std::invalid_argument&) {
      return 0;
    } catch (const std::out_of_range&) {
      return 0;
    }
  }
  return 0;
}

static std::vector<ReactionGroup> parse_reactions(const nlohmann::json& rg) {
  std::vector<ReactionGroup> out;
  if (!rg.is_array()) return out;
  for (const auto& x : rg) {
    ReactionGroup g;
    g.content = get_string(x, "content");
    auto users = x.find("users");
    if (users != x.end() && users->is_object()) {
      auto tc = users->find("totalCount");
      if (tc != users->end() && tc->is_number_integer()) g.total_count = tc->get<int>();
    }
    if (!g.content.empty() && g.total_count > 0) out.push_back(std::move(g));
  }
  return out;
}

static std::optional<User> parse_author(const nlohmann::json& j_author) {
  if (!j_author.is_object()) return std::nullopt;
  User u;
  u.login = get_string(j_author, "login");
  u.url = get_string(j_author, "url");
  if (u.login.empty() && u.url.empty()) return std::nullopt;
  return u;
}

RepoExport convert_from_issue_list_json(
    const nlohmann::json& root_array,
    const std::string& repo_full_name_hint,
    const ConvertOptions& opt) {
  if (!root_array.is_array()) {
    throw std::runtime_error("--in expects a JSON array from `gh issue list --json ...`");
  }

  RepoExport repo;
  repo.full_name = repo_full_name_hint.empty() ? "unknown/unknown" : repo_full_name_hint;
  repo.url = "";

  for (const auto& issue : root_array) {
    if (!issue.is_object()) continue;

    Item it;
    it.kind = ItemKind::Issue;
    it.number = get_int(issue, "number");
    it.title = get_string(issue, "title");
    it.state = get_string(issue, "state");
    it.url = get_string(issue, "url");

    if (opt.include_authors) {
      auto author_it = issue.find("author");
      if (author_it != issue.end()) it.author = parse_author(*author_it);
      it.author_association = get_string(issue, "authorAssociation");
    }

    if (opt.include_timestamps) {
      it.created_at = get_string(issue, "createdAt");
      it.updated_at = get_string(issue, "updatedAt");
      it.closed_at = get_string(issue, "closedAt");
    }

    if (opt.include_body) {
      it.body = get_string(issue, "body");
    }

    if (opt.include_labels) {
      auto labels = issue.find("labels");
      if (labels != issue.end() && labels->is_array()) {
        for (const auto& lab : *labels) {
          Label l;
          if (lab.is_string()) {
            l.name = lab.get<std::string>();
          } else if (lab.is_object()) {
            l.name = get_string(lab, "name");
            l.color = get_string(lab, "color");
            l.description = get_string(lab, "description");
          }
          if (!l.name.empty()) it.labels.push_back(std::move(l));
        }
      }
    }

    if (opt.include_assignees) {
      auto assignees = issue.find("assignees");
      if (assignees != issue.end() && assignees->is_array()) {
        for (const auto& a : *assignees) {
          if (!a.is_object()) continue;
          User u;
          u.login = get_string(a, "login");
          u.url = get_string(a, "url");
          if (!u.login.empty()) it.assignees.push_back(std::move(u));
        }
      }
    }

    if (opt.include_milestone) {
      auto ms = issue.find("milestone");
      if (ms != issue.end() && ms->is_object()) {
        Milestone m;
        m.title = get_string(*ms, "title");
        m.state = get_string(*ms, "state");
        m.due_on = get_string(*ms, "dueOn");
        if (!m.title.empty()) it.milestone = std::move(m);
      }
    }

    if (opt.include_reactions) {
      auto rg = issue.find("reactionGroups");
      if (rg != issue.end()) it.reactions = parse_reactions(*rg);
    }

    if (opt.include_comments) {
      auto comments = issue.find("comments");
      if (comments != issue.end() && comments->is_array()) {
        it.comments_total_count = static_cast<int>(comments->size());
        for (const auto& c : *comments) {
          if (!c.is_object()) continue;
          Comment cc;
          cc.body = opt.include_body ? get_string(c, "body") : "";
          cc.url = get_string(c, "url");
          cc.created_at = opt.include_timestamps ? get_string(c, "createdAt") : "";
          if (opt.include_authors) {
            auto ca = c.find("author");
            if (ca != c.end()) cc.author = parse_author(*ca);
            cc.author_association = get_string(c, "authorAssociation");
          }
          if (opt.include_reactions) {
            auto crg = c.find("reactionGroups");
            if (crg != c.end()) cc.reactions = parse_reactions(*crg);
          }
          it.comments.push_back(std::move(cc));
        }
      }
    }

    repo.items.push_back(std::move(it));
  }

  std::sort(repo.items.begin(), repo.items.end(), [](const Item& a, const Item& b) {
    return a.number < b.number;
  });

  return repo;
}

}  // namespace ghx


