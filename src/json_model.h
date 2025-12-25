#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ghx {

struct ReactionGroup {
  std::string content;  // e.g. THUMBS_UP
  int total_count = 0;
};

struct User {
  std::string login;
  std::string url;
};

struct Label {
  std::string name;
  std::string color;
  std::string description;
};

struct Milestone {
  std::string title;
  std::string state;
  std::string due_on;  // ISO8601 or empty
};

struct Comment {
  std::string body;
  std::string url;
  std::optional<User> author;
  std::string author_association;  // OWNER/MEMBER/NONE/...
  std::string created_at;          // ISO8601 or empty
  std::vector<ReactionGroup> reactions;
};

enum class ItemKind { Issue, PullRequest };

struct Item {
  ItemKind kind = ItemKind::Issue;
  int number = 0;
  std::string title;
  std::string state;  // OPEN/CLOSED/MERGED/...
  std::string url;

  std::optional<User> author;
  std::string author_association;

  std::string created_at;
  std::string updated_at;
  std::string closed_at;
  std::string merged_at;  // PR only

  std::vector<Label> labels;
  std::vector<User> assignees;
  std::optional<Milestone> milestone;

  std::string body;
  std::vector<ReactionGroup> reactions;

  std::vector<Comment> comments;
  int comments_total_count = 0;
  bool comments_has_next_page = false;
  std::string comments_end_cursor;
};

struct RepoExport {
  std::string full_name;
  std::string url;
  std::vector<Item> items;
};

}  // namespace ghx


