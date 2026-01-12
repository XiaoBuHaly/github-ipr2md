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

// PR review record (not the same as Conversation comment).
struct PullRequestReview {
  std::string state;         // APPROVED/CHANGES_REQUESTED/COMMENTED/...
  std::string submitted_at;  // ISO8601 or empty
  std::string body;
  std::string url;
  std::optional<User> author;
  std::string author_association;
  std::vector<ReactionGroup> reactions;
};

// PR review thread comment (inline comment in "Files changed").
// Schema: PullRequestReviewComment.
struct PullRequestReviewComment {
  std::string body;
  std::string url;
  std::optional<User> author;
  std::string author_association;
  std::string created_at;  // ISO8601 or empty
  std::vector<ReactionGroup> reactions;
};

struct PullRequestReviewThread {
  std::string graphql_id;  // node id for pagination
  std::string path;        // file path
  int line = 0;
  int original_line = 0;
  bool is_resolved = false;
  bool is_outdated = false;

  std::vector<PullRequestReviewComment> comments;
  int comments_total_count = 0;
  bool comments_has_next_page = false;
  std::string comments_end_cursor;
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

  // PR review data (optional; may be expensive).
  std::string pr_review_decision;  // REVIEW_REQUIRED/APPROVED/CHANGES_REQUESTED or empty

  std::vector<PullRequestReview> pr_reviews;
  int pr_reviews_total_count = 0;
  bool pr_reviews_has_next_page = false;
  std::string pr_reviews_end_cursor;

  std::vector<PullRequestReviewThread> pr_review_threads;
  int pr_review_threads_total_count = 0;
  bool pr_review_threads_has_next_page = false;
  std::string pr_review_threads_end_cursor;
};

struct RepoExport {
  std::string full_name;
  std::string url;
  std::vector<Item> items;
};

}  // namespace ghx


