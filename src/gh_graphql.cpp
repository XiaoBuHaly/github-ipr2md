#include "gh_graphql.h"

#include "process.h"
#include "progress.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ghx {

// #region agent log
static void agent_log(
    const char* hypothesisId,
    const char* location,
    const char* message,
    const nlohmann::json& data) {
  try {
    std::ofstream f(R"(f:\113Code\github-ipr2md\.cursor\debug.log)", std::ios::app);
    if (!f) return;
    nlohmann::json j;
    j["sessionId"] = "debug-session";
    j["runId"] = "limit-run1";
    j["hypothesisId"] = hypothesisId;
    j["location"] = location;
    j["message"] = message;
    j["data"] = data;
    j["timestamp"] = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    f << j.dump() << "\n";
  } catch (...) {
  }
}
// #endregion

static std::string jstr(const nlohmann::json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) return "";
  if (it->is_string()) return it->get<std::string>();
  return it->dump();
}

static int jint(const nlohmann::json& j, const char* key) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) return 0;
  if (it->is_number_integer()) return it->get<int>();
  return 0;
}

static std::optional<User> parse_user(const nlohmann::json& a) {
  if (!a.is_object()) return std::nullopt;
  User u;
  u.login = jstr(a, "login");
  u.url = jstr(a, "url");
  if (u.login.empty() && u.url.empty()) return std::nullopt;
  return u;
}

static std::vector<ReactionGroup> parse_reaction_groups(const nlohmann::json& rg) {
  std::vector<ReactionGroup> out;
  if (!rg.is_array()) return out;
  for (const auto& x : rg) {
    ReactionGroup g;
    g.content = jstr(x, "content");
    auto users = x.find("users");
    if (users != x.end() && users->is_object()) {
      auto tc = users->find("totalCount");
      if (tc != users->end() && tc->is_number_integer()) g.total_count = tc->get<int>();
    }
    if (!g.content.empty() && g.total_count > 0) out.push_back(std::move(g));
  }
  return out;
}

static std::vector<Label> parse_labels_nodes(const nlohmann::json& labels_obj) {
  std::vector<Label> out;
  if (!labels_obj.is_object()) return out;
  auto nodes = labels_obj.find("nodes");
  if (nodes == labels_obj.end() || !nodes->is_array()) return out;
  for (const auto& n : *nodes) {
    if (!n.is_object()) continue;
    Label l;
    l.name = jstr(n, "name");
    l.color = jstr(n, "color");
    l.description = jstr(n, "description");
    if (!l.name.empty()) out.push_back(std::move(l));
  }
  return out;
}

static std::vector<User> parse_users_nodes(const nlohmann::json& users_obj) {
  std::vector<User> out;
  if (!users_obj.is_object()) return out;
  auto nodes = users_obj.find("nodes");
  if (nodes == users_obj.end() || !nodes->is_array()) return out;
  for (const auto& n : *nodes) {
    auto u = parse_user(n);
    if (u && !u->login.empty()) out.push_back(*u);
  }
  return out;
}

static std::optional<Milestone> parse_milestone(const nlohmann::json& ms) {
  if (!ms.is_object()) return std::nullopt;
  Milestone m;
  m.title = jstr(ms, "title");
  m.state = jstr(ms, "state");
  m.due_on = jstr(ms, "dueOn");
  if (m.title.empty()) return std::nullopt;
  return m;
}

static Comment parse_comment_node(const nlohmann::json& c, const FetchOptions& opt) {
  Comment out;
  out.body = opt.include_body ? jstr(c, "body") : "";
  out.url = jstr(c, "url");
  if (opt.include_authors) {
    auto a = c.find("author");
    if (a != c.end()) out.author = parse_user(*a);
    out.author_association = jstr(c, "authorAssociation");
  }
  if (opt.include_timestamps) out.created_at = jstr(c, "createdAt");
  if (opt.include_reactions) {
    auto rg = c.find("reactionGroups");
    if (rg != c.end()) out.reactions = parse_reaction_groups(*rg);
  }
  return out;
}

static PullRequestReview parse_pr_review_node(const nlohmann::json& r, const FetchOptions& opt) {
  PullRequestReview out;
  out.state = jstr(r, "state");
  out.url = jstr(r, "url");
  out.body = opt.include_body ? jstr(r, "body") : "";
  if (opt.include_authors) {
    auto a = r.find("author");
    if (a != r.end()) out.author = parse_user(*a);
    out.author_association = jstr(r, "authorAssociation");
  }
  if (opt.include_timestamps) out.submitted_at = jstr(r, "submittedAt");
  if (opt.include_reactions) {
    auto rg = r.find("reactionGroups");
    if (rg != r.end()) out.reactions = parse_reaction_groups(*rg);
  }
  return out;
}

static PullRequestReviewComment parse_pr_review_comment_node(const nlohmann::json& c, const FetchOptions& opt) {
  PullRequestReviewComment out;
  out.body = opt.include_body ? jstr(c, "body") : "";
  out.url = jstr(c, "url");
  if (opt.include_authors) {
    auto a = c.find("author");
    if (a != c.end()) out.author = parse_user(*a);
    out.author_association = jstr(c, "authorAssociation");
  }
  if (opt.include_timestamps) out.created_at = jstr(c, "createdAt");
  if (opt.include_reactions) {
    auto rg = c.find("reactionGroups");
    if (rg != c.end()) out.reactions = parse_reaction_groups(*rg);
  }
  return out;
}

static PullRequestReviewThread parse_pr_review_thread_node(const nlohmann::json& t, const FetchOptions& opt) {
  PullRequestReviewThread out;
  out.graphql_id = jstr(t, "id");
  out.path = jstr(t, "path");
  out.line = jint(t, "line");
  out.original_line = jint(t, "originalLine");
  out.is_resolved = t.value("isResolved", false);
  out.is_outdated = t.value("isOutdated", false);
  auto comments = t.find("comments");
  if (comments != t.end() && comments->is_object()) {
    out.comments_total_count = jint(*comments, "totalCount");
    auto pi = comments->find("pageInfo");
    if (pi != comments->end() && pi->is_object()) {
      out.comments_has_next_page = pi->value("hasNextPage", false);
      out.comments_end_cursor = jstr(*pi, "endCursor");
    }
    auto nodes = comments->find("nodes");
    if (nodes != comments->end() && nodes->is_array()) {
      for (const auto& c : *nodes) {
        if (!c.is_object()) continue;
        out.comments.push_back(parse_pr_review_comment_node(c, opt));
      }
    }
  }
  return out;
}

static Item parse_item_node(const nlohmann::json& n, ItemKind kind, const FetchOptions& opt) {
  Item it;
  it.kind = kind;
  it.number = jint(n, "number");
  it.url = jstr(n, "url");
  it.title = jstr(n, "title");
  it.state = jstr(n, "state");
  if (opt.include_body) it.body = jstr(n, "body");

  if (opt.include_authors) {
    auto a = n.find("author");
    if (a != n.end()) it.author = parse_user(*a);
    it.author_association = jstr(n, "authorAssociation");
  }

  if (opt.include_timestamps) {
    it.created_at = jstr(n, "createdAt");
    it.updated_at = jstr(n, "updatedAt");
    it.closed_at = jstr(n, "closedAt");
    if (kind == ItemKind::PullRequest) it.merged_at = jstr(n, "mergedAt");
  }

  if (kind == ItemKind::PullRequest) {
    if (opt.include_pr_review_decision) {
      it.pr_review_decision = jstr(n, "reviewDecision");
    }
    if (opt.include_pr_reviews) {
      auto reviews = n.find("reviews");
      if (reviews != n.end() && reviews->is_object()) {
        it.pr_reviews_total_count = jint(*reviews, "totalCount");
        auto pi = reviews->find("pageInfo");
        if (pi != reviews->end() && pi->is_object()) {
          it.pr_reviews_has_next_page = pi->value("hasNextPage", false);
          it.pr_reviews_end_cursor = jstr(*pi, "endCursor");
        }
        auto nodes = reviews->find("nodes");
        if (nodes != reviews->end() && nodes->is_array()) {
          for (const auto& r : *nodes) {
            if (!r.is_object()) continue;
            it.pr_reviews.push_back(parse_pr_review_node(r, opt));
          }
        }
      }
    }
    if (opt.include_pr_review_threads) {
      auto threads = n.find("reviewThreads");
      if (threads != n.end() && threads->is_object()) {
        it.pr_review_threads_total_count = jint(*threads, "totalCount");
        auto pi = threads->find("pageInfo");
        if (pi != threads->end() && pi->is_object()) {
          it.pr_review_threads_has_next_page = pi->value("hasNextPage", false);
          it.pr_review_threads_end_cursor = jstr(*pi, "endCursor");
        }
        auto nodes = threads->find("nodes");
        if (nodes != threads->end() && nodes->is_array()) {
          for (const auto& t : *nodes) {
            if (!t.is_object()) continue;
            it.pr_review_threads.push_back(parse_pr_review_thread_node(t, opt));
          }
        }
      }
    }
  }

  if (opt.include_labels) {
    auto labels = n.find("labels");
    if (labels != n.end()) it.labels = parse_labels_nodes(*labels);
  }

  if (opt.include_assignees) {
    auto assignees = n.find("assignees");
    if (assignees != n.end()) it.assignees = parse_users_nodes(*assignees);
  }

  if (opt.include_milestone) {
    auto ms = n.find("milestone");
    if (ms != n.end()) it.milestone = parse_milestone(*ms);
  }

  if (opt.include_reactions) {
    auto rg = n.find("reactionGroups");
    if (rg != n.end()) it.reactions = parse_reaction_groups(*rg);
  }

  if (opt.include_comments) {
    auto comments = n.find("comments");
    if (comments != n.end() && comments->is_object()) {
      it.comments_total_count = jint(*comments, "totalCount");
      auto pi = comments->find("pageInfo");
      if (pi != comments->end() && pi->is_object()) {
        it.comments_has_next_page = pi->value("hasNextPage", false);
        it.comments_end_cursor = jstr(*pi, "endCursor");
      }
      auto nodes = comments->find("nodes");
      if (nodes != comments->end() && nodes->is_array()) {
        for (const auto& c : *nodes) {
          if (!c.is_object()) continue;
          it.comments.push_back(parse_comment_node(c, opt));
        }
      }
    }
  }

  return it;
}

static std::string repo_query() {
  // Keep query stable. Use directives to avoid fetching large sub-objects when disabled.
  // NOTE: labels/assignees are fetched via nested connections and are capped by `first`.
  // We expose `labelsFirst` / `assigneesFirst` as variables (defaulted in FetchOptions),
  // but we do NOT fully paginate those nested connections yet.
  return R"GRAPHQL(
query(
  $owner: String!
  $repo: String!
  $orderDirection: OrderDirection!
  $issuePerPage: Int!
  $issueCursor: String
  $prPerPage: Int!
  $prCursor: String
  $issueStates: [IssueState!]
  $prStates: [PullRequestState!]
  $includeIssues: Boolean!
  $includePrs: Boolean!
  $includeComments: Boolean!
  $includePrReviewDecision: Boolean!
  $includePrReviews: Boolean!
  $includePrReviewThreads: Boolean!
  $includeLabels: Boolean!
  $includeAssignees: Boolean!
  $includeMilestone: Boolean!
  $includeReactions: Boolean!
  $commentPerPage: Int!
  $labelsFirst: Int!
  $assigneesFirst: Int!
) {
  repository(owner: $owner, name: $repo) {
    nameWithOwner
    url
    issues(
      first: $issuePerPage
      after: $issueCursor
      filterBy: { states: $issueStates }
      orderBy: { field: CREATED_AT, direction: $orderDirection }
    ) @include(if: $includeIssues) {
      totalCount
      pageInfo { endCursor hasNextPage }
      nodes {
        id
        number
        url
        title
        body
        state
        createdAt
        updatedAt
        closedAt
        author { login url }
        authorAssociation
        labels(first: $labelsFirst) @include(if: $includeLabels) { nodes { name color description } }
        assignees(first: $assigneesFirst) @include(if: $includeAssignees) { nodes { login url } }
        milestone @include(if: $includeMilestone) { title state dueOn }
        reactionGroups @include(if: $includeReactions) { content users { totalCount } }
        comments(first: $commentPerPage) @include(if: $includeComments) {
          totalCount
          pageInfo { endCursor hasNextPage }
          nodes {
            body
            createdAt
            url
            author { login url }
            authorAssociation
            reactionGroups @include(if: $includeReactions) { content users { totalCount } }
          }
        }
      }
    }
    pullRequests(
      first: $prPerPage
      after: $prCursor
      states: $prStates
      orderBy: { field: CREATED_AT, direction: $orderDirection }
    ) @include(if: $includePrs) {
      totalCount
      pageInfo { endCursor hasNextPage }
      nodes {
        id
        number
        url
        title
        body
        state
        createdAt
        updatedAt
        closedAt
        mergedAt
        reviewDecision @include(if: $includePrReviewDecision)
        author { login url }
        authorAssociation
        labels(first: $labelsFirst) @include(if: $includeLabels) { nodes { name color description } }
        assignees(first: $assigneesFirst) @include(if: $includeAssignees) { nodes { login url } }
        milestone @include(if: $includeMilestone) { title state dueOn }
        reactionGroups @include(if: $includeReactions) { content users { totalCount } }
        comments(first: $commentPerPage) @include(if: $includeComments) {
          totalCount
          pageInfo { endCursor hasNextPage }
          nodes {
            body
            createdAt
            url
            author { login url }
            authorAssociation
            reactionGroups @include(if: $includeReactions) { content users { totalCount } }
          }
        }
        reviews(first: 100) @include(if: $includePrReviews) {
          totalCount
          pageInfo { endCursor hasNextPage }
          nodes {
            state
            submittedAt
            body
            url
            author { login url }
            authorAssociation
            reactionGroups @include(if: $includeReactions) { content users { totalCount } }
          }
        }
        reviewThreads(first: 100) @include(if: $includePrReviewThreads) {
          totalCount
          pageInfo { endCursor hasNextPage }
          nodes {
            id
            path
            line
            originalLine
            isResolved
            isOutdated
            comments(first: $commentPerPage) {
              totalCount
              pageInfo { endCursor hasNextPage }
              nodes {
                body
                createdAt
                url
                author { login url }
                authorAssociation
                reactionGroups @include(if: $includeReactions) { content users { totalCount } }
              }
            }
          }
        }
      }
    }
  }
}
)GRAPHQL";
}

static std::string single_item_query() {
  // Use IssueOrPullRequest union to avoid errors like:
  // "Could not resolve to a PullRequest with the number of X" when X is an Issue.
  return R"GRAPHQL(
query(
  $owner: String!
  $repo: String!
  $number: Int!
  $includeComments: Boolean!
  $includePrReviewDecision: Boolean!
  $includePrReviews: Boolean!
  $includePrReviewThreads: Boolean!
  $includeLabels: Boolean!
  $includeAssignees: Boolean!
  $includeMilestone: Boolean!
  $includeReactions: Boolean!
  $commentPerPage: Int!
  $labelsFirst: Int!
  $assigneesFirst: Int!
) {
  repository(owner: $owner, name: $repo) {
    nameWithOwner
    url
    issueOrPullRequest(number: $number) {
      __typename
      ... on Issue {
        id
        number
        url
        title
        body
        state
        createdAt
        updatedAt
        closedAt
        author { login url }
        authorAssociation
        labels(first: $labelsFirst) @include(if: $includeLabels) { nodes { name color description } }
        assignees(first: $assigneesFirst) @include(if: $includeAssignees) { nodes { login url } }
        milestone @include(if: $includeMilestone) { title state dueOn }
        reactionGroups @include(if: $includeReactions) { content users { totalCount } }
        comments(first: $commentPerPage) @include(if: $includeComments) {
          totalCount
          pageInfo { endCursor hasNextPage }
          nodes {
            body
            createdAt
            url
            author { login url }
            authorAssociation
            reactionGroups @include(if: $includeReactions) { content users { totalCount } }
          }
        }
      }
      ... on PullRequest {
        id
        number
        url
        title
        body
        state
        createdAt
        updatedAt
        closedAt
        mergedAt
        reviewDecision @include(if: $includePrReviewDecision)
        author { login url }
        authorAssociation
        labels(first: $labelsFirst) @include(if: $includeLabels) { nodes { name color description } }
        assignees(first: $assigneesFirst) @include(if: $includeAssignees) { nodes { login url } }
        milestone @include(if: $includeMilestone) { title state dueOn }
        reactionGroups @include(if: $includeReactions) { content users { totalCount } }
        comments(first: $commentPerPage) @include(if: $includeComments) {
          totalCount
          pageInfo { endCursor hasNextPage }
          nodes {
            body
            createdAt
            url
            author { login url }
            authorAssociation
            reactionGroups @include(if: $includeReactions) { content users { totalCount } }
          }
        }
        reviews(first: 100) @include(if: $includePrReviews) {
          totalCount
          pageInfo { endCursor hasNextPage }
          nodes {
            state
            submittedAt
            body
            url
            author { login url }
            authorAssociation
            reactionGroups @include(if: $includeReactions) { content users { totalCount } }
          }
        }
        reviewThreads(first: 100) @include(if: $includePrReviewThreads) {
          totalCount
          pageInfo { endCursor hasNextPage }
          nodes {
            id
            path
            line
            originalLine
            isResolved
            isOutdated
            comments(first: $commentPerPage) {
              totalCount
              pageInfo { endCursor hasNextPage }
              nodes {
                body
                createdAt
                url
                author { login url }
                authorAssociation
                reactionGroups @include(if: $includeReactions) { content users { totalCount } }
              }
            }
          }
        }
      }
    }
  }
}
)GRAPHQL";
}

static std::string node_comment_query() {
  return R"GRAPHQL(
query(
  $id: ID!
  $commentPerPage: Int!
  $commentCursor: String!
  $includeReactions: Boolean!
) {
  node(id: $id) {
    ... on Issue {
      comments(first: $commentPerPage, after: $commentCursor) {
        totalCount
        pageInfo { endCursor hasNextPage }
        nodes {
          body
          createdAt
          url
          author { login url }
          authorAssociation
          reactionGroups @include(if: $includeReactions) { content users { totalCount } }
        }
      }
    }
    ... on PullRequest {
      comments(first: $commentPerPage, after: $commentCursor) {
        totalCount
        pageInfo { endCursor hasNextPage }
        nodes {
          body
          createdAt
          url
          author { login url }
          authorAssociation
          reactionGroups @include(if: $includeReactions) { content users { totalCount } }
        }
      }
    }
  }
}
)GRAPHQL";
}

static std::string node_pr_reviews_query() {
  return R"GRAPHQL(
query(
  $id: ID!
  $reviewPerPage: Int!
  $reviewCursor: String!
  $includeReactions: Boolean!
) {
  node(id: $id) {
    ... on PullRequest {
      reviews(first: $reviewPerPage, after: $reviewCursor) {
        totalCount
        pageInfo { endCursor hasNextPage }
        nodes {
          state
          submittedAt
          body
          url
          author { login url }
          authorAssociation
          reactionGroups @include(if: $includeReactions) { content users { totalCount } }
        }
      }
    }
  }
}
)GRAPHQL";
}

static std::string node_pr_review_threads_query() {
  return R"GRAPHQL(
query(
  $id: ID!
  $threadPerPage: Int!
  $threadCursor: String!
  $commentPerPage: Int!
  $includeReactions: Boolean!
) {
  node(id: $id) {
    ... on PullRequest {
      reviewThreads(first: $threadPerPage, after: $threadCursor) {
        totalCount
        pageInfo { endCursor hasNextPage }
        nodes {
          id
          path
          line
          originalLine
          isResolved
          isOutdated
          comments(first: $commentPerPage) {
            totalCount
            pageInfo { endCursor hasNextPage }
            nodes {
              body
              createdAt
              url
              author { login url }
              authorAssociation
              reactionGroups @include(if: $includeReactions) { content users { totalCount } }
            }
          }
        }
      }
    }
  }
}
)GRAPHQL";
}

static std::string node_review_thread_comments_query() {
  return R"GRAPHQL(
query(
  $id: ID!
  $commentPerPage: Int!
  $commentCursor: String!
  $includeReactions: Boolean!
) {
  node(id: $id) {
    ... on PullRequestReviewThread {
      comments(first: $commentPerPage, after: $commentCursor) {
        totalCount
        pageInfo { endCursor hasNextPage }
        nodes {
          body
          createdAt
          url
          author { login url }
          authorAssociation
          reactionGroups @include(if: $includeReactions) { content users { totalCount } }
        }
      }
    }
  }
}
)GRAPHQL";
}

static nlohmann::json gh_api_graphql(
    const std::string& query,
    const std::vector<std::pair<std::string, std::string>>& variables,
    const FetchOptions& opt);

static void paginate_comments_for_item(
    const std::string& cquery,
    const std::string& graphql_id,
    Item& item,
    const FetchOptions& opt,
    ProgressPrinter& progress) {
  if (graphql_id.empty()) return;
  if (!opt.include_comments) return;
  if (!item.comments_has_next_page) return;

  std::string cursor = item.comments_end_cursor;
  bool has_page = item.comments_has_next_page;

  while (has_page) {
    progress.tick("Comments paging: #" + std::to_string(item.number) + " ...", false);
    std::vector<std::pair<std::string, std::string>> vars;
    vars.emplace_back("id", graphql_id);
    vars.emplace_back("commentPerPage", std::to_string(opt.comment_per_page));
    vars.emplace_back("commentCursor", cursor);
    vars.emplace_back("includeReactions", opt.include_reactions ? "true" : "false");
    auto json = gh_api_graphql(cquery, vars, opt);

    auto data = json.find("data");
    if (data == json.end() || !data->is_object()) break;
    auto node = data->find("node");
    if (node == data->end() || !node->is_object()) break;
    auto comments = node->find("comments");
    if (comments == node->end() || !comments->is_object()) break;

    auto pi = comments->find("pageInfo");
    if (pi != comments->end() && pi->is_object()) {
      cursor = jstr(*pi, "endCursor");
      has_page = pi->value("hasNextPage", false);
    } else {
      has_page = false;
    }

    auto nodes = comments->find("nodes");
    if (nodes != comments->end() && nodes->is_array()) {
      for (const auto& c : *nodes) {
        if (!c.is_object()) continue;
        item.comments.push_back(parse_comment_node(c, opt));
      }
    }
  }

  // Keep item state consistent post-pagination.
  item.comments_end_cursor = cursor;
  item.comments_has_next_page = has_page;
  item.comments_total_count = std::max(item.comments_total_count, static_cast<int>(item.comments.size()));
}

static void paginate_pr_reviews_for_item(
    const std::string& rquery,
    const std::string& pr_graphql_id,
    Item& item,
    const FetchOptions& opt,
    ProgressPrinter& progress) {
  if (pr_graphql_id.empty()) return;
  if (!opt.include_pr_reviews) return;
  if (item.kind != ItemKind::PullRequest) return;
  if (!item.pr_reviews_has_next_page) return;

  std::string cursor = item.pr_reviews_end_cursor;
  bool has_page = item.pr_reviews_has_next_page;
  while (has_page) {
    progress.tick("PR reviews paging: #" + std::to_string(item.number) + " ...", false);
    std::vector<std::pair<std::string, std::string>> vars;
    vars.emplace_back("id", pr_graphql_id);
    vars.emplace_back("reviewPerPage", "100");
    vars.emplace_back("reviewCursor", cursor);
    vars.emplace_back("includeReactions", opt.include_reactions ? "true" : "false");
    auto json = gh_api_graphql(rquery, vars, opt);

    auto data = json.find("data");
    if (data == json.end() || !data->is_object()) break;
    auto node = data->find("node");
    if (node == data->end() || !node->is_object()) break;
    auto reviews = node->find("reviews");
    if (reviews == node->end() || !reviews->is_object()) break;

    auto pi = reviews->find("pageInfo");
    if (pi != reviews->end() && pi->is_object()) {
      cursor = jstr(*pi, "endCursor");
      has_page = pi->value("hasNextPage", false);
    } else {
      has_page = false;
    }

    auto nodes = reviews->find("nodes");
    if (nodes != reviews->end() && nodes->is_array()) {
      for (const auto& r : *nodes) {
        if (!r.is_object()) continue;
        item.pr_reviews.push_back(parse_pr_review_node(r, opt));
      }
    }
  }

  item.pr_reviews_end_cursor = cursor;
  item.pr_reviews_has_next_page = has_page;
  item.pr_reviews_total_count = std::max(item.pr_reviews_total_count, static_cast<int>(item.pr_reviews.size()));
}

static void paginate_review_thread_comments_for_thread(
    const std::string& cquery,
    PullRequestReviewThread& thread,
    const FetchOptions& opt,
    ProgressPrinter& progress,
    int pr_number_for_progress) {
  if (thread.graphql_id.empty()) return;
  if (!opt.include_pr_review_threads) return;
  if (!thread.comments_has_next_page) return;

  std::string cursor = thread.comments_end_cursor;
  bool has_page = thread.comments_has_next_page;
  while (has_page) {
    progress.tick("PR review thread comments paging: #" + std::to_string(pr_number_for_progress) + " ...", false);
    std::vector<std::pair<std::string, std::string>> vars;
    vars.emplace_back("id", thread.graphql_id);
    vars.emplace_back("commentPerPage", std::to_string(opt.comment_per_page));
    vars.emplace_back("commentCursor", cursor);
    vars.emplace_back("includeReactions", opt.include_reactions ? "true" : "false");
    auto json = gh_api_graphql(cquery, vars, opt);

    auto data = json.find("data");
    if (data == json.end() || !data->is_object()) break;
    auto node = data->find("node");
    if (node == data->end() || !node->is_object()) break;
    auto comments = node->find("comments");
    if (comments == node->end() || !comments->is_object()) break;

    auto pi = comments->find("pageInfo");
    if (pi != comments->end() && pi->is_object()) {
      cursor = jstr(*pi, "endCursor");
      has_page = pi->value("hasNextPage", false);
    } else {
      has_page = false;
    }

    auto nodes = comments->find("nodes");
    if (nodes != comments->end() && nodes->is_array()) {
      for (const auto& c : *nodes) {
        if (!c.is_object()) continue;
        thread.comments.push_back(parse_pr_review_comment_node(c, opt));
      }
    }
  }

  thread.comments_end_cursor = cursor;
  thread.comments_has_next_page = has_page;
  thread.comments_total_count = std::max(thread.comments_total_count, static_cast<int>(thread.comments.size()));
}

static void paginate_pr_review_threads_for_item(
    const std::string& tquery,
    const std::string& thread_comment_query,
    const std::string& pr_graphql_id,
    Item& item,
    const FetchOptions& opt,
    ProgressPrinter& progress) {
  if (pr_graphql_id.empty()) return;
  if (!opt.include_pr_review_threads) return;
  if (item.kind != ItemKind::PullRequest) return;
  if (!item.pr_review_threads_has_next_page) return;

  std::string cursor = item.pr_review_threads_end_cursor;
  bool has_page = item.pr_review_threads_has_next_page;
  while (has_page) {
    progress.tick("PR reviewThreads paging: #" + std::to_string(item.number) + " ...", false);
    std::vector<std::pair<std::string, std::string>> vars;
    vars.emplace_back("id", pr_graphql_id);
    vars.emplace_back("threadPerPage", "100");
    vars.emplace_back("threadCursor", cursor);
    vars.emplace_back("commentPerPage", std::to_string(opt.comment_per_page));
    vars.emplace_back("includeReactions", opt.include_reactions ? "true" : "false");
    auto json = gh_api_graphql(tquery, vars, opt);

    auto data = json.find("data");
    if (data == json.end() || !data->is_object()) break;
    auto node = data->find("node");
    if (node == data->end() || !node->is_object()) break;
    auto threads = node->find("reviewThreads");
    if (threads == node->end() || !threads->is_object()) break;

    auto pi = threads->find("pageInfo");
    if (pi != threads->end() && pi->is_object()) {
      cursor = jstr(*pi, "endCursor");
      has_page = pi->value("hasNextPage", false);
    } else {
      has_page = false;
    }

    auto nodes = threads->find("nodes");
    if (nodes != threads->end() && nodes->is_array()) {
      for (const auto& t : *nodes) {
        if (!t.is_object()) continue;
        auto th = parse_pr_review_thread_node(t, opt);
        // For full export: paginate each thread's comments too.
        paginate_review_thread_comments_for_thread(thread_comment_query, th, opt, progress, item.number);
        item.pr_review_threads.push_back(std::move(th));
      }
    }
  }

  item.pr_review_threads_end_cursor = cursor;
  item.pr_review_threads_has_next_page = has_page;
  item.pr_review_threads_total_count =
      std::max(item.pr_review_threads_total_count, static_cast<int>(item.pr_review_threads.size()));

  // Also ensure the initial threads have their comments fully paginated.
  // (They may have been included in the initial query already.)
  if (!item.pr_review_threads.empty()) {
    for (auto& th : item.pr_review_threads) {
      paginate_review_thread_comments_for_thread(thread_comment_query, th, opt, progress, item.number);
    }
  }
}

static nlohmann::json gh_api_graphql(
    const std::string& query,
    const std::vector<std::pair<std::string, std::string>>& variables,
    const FetchOptions& opt) {
  std::vector<std::string> args = {"gh", "api", "graphql", "-F", "query=@-"};
  if (!opt.hostname.empty()) {
    args.push_back("--hostname");
    args.push_back(opt.hostname);
  }
  for (const auto& kv : variables) {
    args.push_back("-F");
    args.push_back(kv.first + "=" + kv.second);
  }
  auto r = run_process(args, query);
  if (r.exit_code != 0) {
    std::ostringstream oss;
    oss << "gh api graphql failed (exit=" << r.exit_code << ")";
    if (!r.stderr_str.empty()) oss << "\n" << r.stderr_str;
    throw std::runtime_error(oss.str());
  }
  return nlohmann::json::parse(r.stdout_str);
}

RepoExport fetch_repo_via_gh_graphql(const std::string& owner, const std::string& repo_name, const FetchOptions& opt) {
  RepoExport out;

  ProgressPrinter progress(opt.progress_enabled, opt.progress_interval_ms);
  progress.tick("Fetching " + owner + "/" + repo_name + " ...", true);

  // #region agent log
  agent_log(
      "H1",
      "src/gh_graphql.cpp:fetch_repo_via_gh_graphql:entry",
      "enter",
      {{"owner", owner},
       {"repo", repo_name},
       {"limit", opt.limit},
       {"per_page", opt.per_page},
       {"include_issues", opt.include_issues},
       {"include_prs", opt.include_prs},
       {"state", opt.state},
       {"reverse_order", opt.reverse_order}});
  // #endregion

  int fetched_issues = 0;
  int fetched_prs = 0;

  int last_issue_number = 0;
  int last_pr_number = 0;
  std::string last_issue_created_at;
  std::string last_pr_created_at;

  std::string issue_cursor;
  std::string pr_cursor;
  bool has_issue_page = opt.include_issues;
  bool has_pr_page = opt.include_prs;

  const auto query = repo_query();

  // We keep the node IDs around to fetch additional comment pages.
  struct NodeRef { std::string id; size_t item_index; };
  std::vector<NodeRef> needs_comment_pagination;
  std::vector<NodeRef> needs_pr_review_pagination;

  int loop_iter = 0;
  int pr_page_limit_mode = 0;
  if (opt.limit > 0) pr_page_limit_mode = 1;  // start tiny; grow only if PRs matter for the limit window
  while (has_issue_page || has_pr_page) {
    loop_iter++;
    std::vector<std::pair<std::string, std::string>> vars;
    vars.emplace_back("owner", owner);
    vars.emplace_back("repo", repo_name);
    vars.emplace_back("orderDirection", opt.reverse_order ? "DESC" : "ASC");

    int issue_page = opt.per_page;
    int pr_page = opt.per_page;
    if (opt.limit > 0) {
      // Output truncation happens later. Here we aim to reduce "over-fetch" while staying correct.
      // Strategy:
      // - Always fetch up to limit issues in one go (issues often dominate the top-K window).
      // - Fetch PRs in adaptive batches (1 -> 10 -> 20 -> ...), stopping as soon as PRs are proven
      //   irrelevant for the limit window by createdAt. Never jump back to 100, which causes
      //   confusing "limit=100 but fetched 127" regressions.
      issue_page = std::min(issue_page, opt.limit);
      if (has_pr_page) {
        if (pr_page_limit_mode <= 0) pr_page_limit_mode = 1;
        pr_page = std::min(opt.per_page, std::min(opt.limit, pr_page_limit_mode));
      }
    }
    if (!has_issue_page) issue_page = 0;
    if (!has_pr_page) pr_page = 0;

    vars.emplace_back("issuePerPage", has_issue_page ? std::to_string(issue_page) : "0");
    vars.emplace_back("prPerPage", has_pr_page ? std::to_string(pr_page) : "0");
    vars.emplace_back("commentPerPage", std::to_string(opt.comment_per_page));
    vars.emplace_back("labelsFirst", std::to_string(opt.labels_first));
    vars.emplace_back("assigneesFirst", std::to_string(opt.assignees_first));

    // Use has_*_page (not opt.include_*) so we don't keep requesting disabled streams on later iterations.
    vars.emplace_back("includeIssues", has_issue_page ? "true" : "false");
    vars.emplace_back("includePrs", has_pr_page ? "true" : "false");
    vars.emplace_back("includeComments", opt.include_comments ? "true" : "false");
    vars.emplace_back("includePrReviewDecision", opt.include_pr_review_decision ? "true" : "false");
    vars.emplace_back("includePrReviews", opt.include_pr_reviews ? "true" : "false");
    vars.emplace_back("includePrReviewThreads", opt.include_pr_review_threads ? "true" : "false");
    vars.emplace_back("includeLabels", opt.include_labels ? "true" : "false");
    vars.emplace_back("includeAssignees", opt.include_assignees ? "true" : "false");
    vars.emplace_back("includeMilestone", opt.include_milestone ? "true" : "false");
    vars.emplace_back("includeReactions", opt.include_reactions ? "true" : "false");

    if (has_issue_page && !issue_cursor.empty()) vars.emplace_back("issueCursor", issue_cursor);
    if (has_pr_page && !pr_cursor.empty()) vars.emplace_back("prCursor", pr_cursor);

    if (opt.state == "open") {
      // arrays: issueStates[]=OPEN, prStates[]=OPEN
      vars.emplace_back("issueStates[]", "OPEN");
      vars.emplace_back("prStates[]", "OPEN");
    } else if (opt.state == "closed") {
      vars.emplace_back("issueStates[]", "CLOSED");
      vars.emplace_back("prStates[]", "CLOSED");
      vars.emplace_back("prStates[]", "MERGED");
    }

    // #region agent log
    agent_log(
        "H2",
        "src/gh_graphql.cpp:fetch_repo_via_gh_graphql:before_query",
        "loop",
        {{"iter", loop_iter},
         {"issuePerPage", issue_page},
         {"prPerPage", pr_page},
         {"prPageMode", pr_page_limit_mode},
         {"issue_cursor_empty", issue_cursor.empty()},
         {"pr_cursor_empty", pr_cursor.empty()},
         {"has_issue_page", has_issue_page},
         {"has_pr_page", has_pr_page},
         {"fetched_issues", fetched_issues},
         {"fetched_prs", fetched_prs},
         {"items_size", (int)out.items.size()},
         {"limit", opt.limit}});
    // #endregion

    auto json = gh_api_graphql(query, vars, opt);

    auto data = json.find("data");
    if (data == json.end() || !data->is_object()) {
      throw std::runtime_error("unexpected graphql response: missing data");
    }

    auto repo = data->find("repository");
    if (repo == data->end() || !repo->is_object()) {
      throw std::runtime_error("unexpected graphql response: missing repository");
    }

    if (out.full_name.empty()) out.full_name = jstr(*repo, "nameWithOwner");
    if (out.url.empty()) out.url = jstr(*repo, "url");

    if (opt.include_issues) {
      auto issues = repo->find("issues");
      if (issues != repo->end() && issues->is_object()) {
        int total = jint(*issues, "totalCount");
        auto pi = issues->find("pageInfo");
        if (pi != issues->end() && pi->is_object()) {
          issue_cursor = jstr(*pi, "endCursor");
          has_issue_page = pi->value("hasNextPage", false);
        } else {
          has_issue_page = false;
        }
        auto nodes = issues->find("nodes");
        if (nodes != issues->end() && nodes->is_array()) {
          for (const auto& n : *nodes) {
            if (!n.is_object()) continue;
            auto item = parse_item_node(n, ItemKind::Issue, opt);
            auto id = jstr(n, "id");
            if (item.number > 0) last_issue_number = item.number;
            if (!item.created_at.empty()) last_issue_created_at = item.created_at;
            out.items.push_back(std::move(item));
            fetched_issues++;
            progress.tick("Fetched issues=" + std::to_string(fetched_issues) + " prs=" + std::to_string(fetched_prs) +
                              " items=" + std::to_string(out.items.size()),
                          false);
            if (opt.include_comments) {
              size_t idx = out.items.size() - 1;
              if (!id.empty() && out.items[idx].comments_has_next_page) {
                needs_comment_pagination.push_back(NodeRef{id, idx});
              }
            }
          }
        }
        progress.tick("Fetched issues=" + std::to_string(fetched_issues) + "/" + std::to_string(total) +
                          " prs=" + std::to_string(fetched_prs) + " items=" + std::to_string(out.items.size()),
                      false);

        // #region agent log
        agent_log(
            "H4",
            "src/gh_graphql.cpp:fetch_repo_via_gh_graphql:after_issues_page",
            "issues_page",
            {{"iter", loop_iter},
             {"issues_totalCount", total},
             {"issue_has_next_page", has_issue_page},
             {"fetched_issues", fetched_issues},
             {"items_size", (int)out.items.size()},
             {"limit", opt.limit}});
        // #endregion
      } else {
        has_issue_page = false;
      }
    } else {
      has_issue_page = false;
    }

    if (opt.include_prs && has_pr_page) {
      auto prs = repo->find("pullRequests");
      if (prs != repo->end() && prs->is_object()) {
        int total = jint(*prs, "totalCount");
        auto pi = prs->find("pageInfo");
        if (pi != prs->end() && pi->is_object()) {
          pr_cursor = jstr(*pi, "endCursor");
          has_pr_page = pi->value("hasNextPage", false);
        } else {
          has_pr_page = false;
        }
        auto nodes = prs->find("nodes");
        if (nodes != prs->end() && nodes->is_array()) {
          for (const auto& n : *nodes) {
            if (!n.is_object()) continue;
            auto item = parse_item_node(n, ItemKind::PullRequest, opt);
            auto id = jstr(n, "id");
            if (item.number > 0) last_pr_number = item.number;
            if (!item.created_at.empty()) last_pr_created_at = item.created_at;
            out.items.push_back(std::move(item));
            fetched_prs++;
            progress.tick("Fetched issues=" + std::to_string(fetched_issues) + " prs=" + std::to_string(fetched_prs) +
                              " items=" + std::to_string(out.items.size()),
                          false);
            if (opt.include_comments) {
              size_t idx = out.items.size() - 1;
              if (!id.empty() && out.items[idx].comments_has_next_page) {
                needs_comment_pagination.push_back(NodeRef{id, idx});
              }
            }
            if ((opt.include_pr_reviews || opt.include_pr_review_threads) && !id.empty()) {
              size_t idx = out.items.size() - 1;
              if (out.items[idx].pr_reviews_has_next_page || out.items[idx].pr_review_threads_has_next_page) {
                needs_pr_review_pagination.push_back(NodeRef{id, idx});
              }
            }
          }
        }
        progress.tick("Fetched issues=" + std::to_string(fetched_issues) + " prs=" + std::to_string(fetched_prs) +
                          "/" + std::to_string(total) + " items=" + std::to_string(out.items.size()),
                      false);

        // #region agent log
        agent_log(
            "H4",
            "src/gh_graphql.cpp:fetch_repo_via_gh_graphql:after_prs_page",
            "prs_page",
            {{"iter", loop_iter},
             {"prs_totalCount", total},
             {"pr_has_next_page", has_pr_page},
             {"fetched_prs", fetched_prs},
             {"items_size", (int)out.items.size()},
             {"limit", opt.limit}});
        // #endregion
      } else {
        has_pr_page = false;
      }
    } else {
      has_pr_page = false;
    }

    // If user asked for an output limit, we can often stop fetching early without changing output.
    // We stop only when it's provably safe for number-based truncation: once we have >= limit items,
    // and the next possible numbers in both streams are already greater (ASC) / smaller (DESC) than
    // the current threshold number of the top-K items.
    if (opt.limit > 0 && (int)out.items.size() >= opt.limit && (has_issue_page || has_pr_page)) {
      // IMPORTANT: output truncation uses createdAt ordering when available (GraphQL mode),
      // so the only safe early-stop criterion is based on createdAt bounds, not issue/PR numbers.
      std::string threshold_ts;
      bool threshold_known = false;
      {
        std::vector<std::string> ts;
        ts.reserve(out.items.size());
        for (const auto& it : out.items) {
          if (!it.created_at.empty()) ts.push_back(it.created_at);
        }
        if ((int)ts.size() >= opt.limit) {
          threshold_known = true;
          if (!opt.reverse_order) {
            std::nth_element(ts.begin(), ts.begin() + (opt.limit - 1), ts.end());
            threshold_ts = ts[opt.limit - 1];
          } else {
            // reverse_order: keep top-K newest timestamps
            std::nth_element(ts.begin(), ts.begin() + (opt.limit - 1), ts.end(),
                             [](const std::string& a, const std::string& b) { return a > b; });
            threshold_ts = ts[opt.limit - 1];
          }
        }
      }

      bool can_stop = false;
      bool bound_issue_known = has_issue_page && !last_issue_created_at.empty();
      bool bound_pr_known = has_pr_page && !last_pr_created_at.empty();
      std::string bound_issue = last_issue_created_at;
      std::string bound_pr = last_pr_created_at;

      if (threshold_known) {
        if (!opt.reverse_order) {
          // ASC: future pages are newer; lower bound is last_seen_createdAt in each stream.
          bool ok = true;
          std::string next_min;
          bool next_min_set = false;
          if (has_issue_page) {
            if (!bound_issue_known) ok = false;
            else { next_min = bound_issue; next_min_set = true; }
          }
          if (has_pr_page) {
            if (!bound_pr_known) ok = false;
            else if (!next_min_set || bound_pr < next_min) { next_min = bound_pr; next_min_set = true; }
          }
          can_stop = ok && next_min_set && (next_min > threshold_ts);  // strict for tie safety
        } else {
          // DESC: future pages are older; upper bound is last_seen_createdAt in each stream.
          bool ok = true;
          std::string next_max;
          bool next_max_set = false;
          if (has_issue_page) {
            if (!bound_issue_known) ok = false;
            else { next_max = bound_issue; next_max_set = true; }
          }
          if (has_pr_page) {
            if (!bound_pr_known) ok = false;
            else if (!next_max_set || bound_pr > next_max) { next_max = bound_pr; next_max_set = true; }
          }
          can_stop = ok && next_max_set && (next_max < threshold_ts);  // strict for tie safety
        }
      }

      bool stop_issues = false;
      bool stop_prs = false;
      if (threshold_known) {
        if (!opt.reverse_order) {
          // ASC: once a stream's next lower-bound is beyond threshold, it can't contribute to top-K.
          stop_issues = has_issue_page && bound_issue_known && (bound_issue > threshold_ts);
          stop_prs = has_pr_page && bound_pr_known && (bound_pr > threshold_ts);
        } else {
          // DESC: once a stream's next upper-bound is below threshold, it can't contribute to top-K.
          stop_issues = has_issue_page && bound_issue_known && (bound_issue < threshold_ts);
          stop_prs = has_pr_page && bound_pr_known && (bound_pr < threshold_ts);
        }
      }

      // #region agent log
      agent_log(
          "H2",
          "src/gh_graphql.cpp:fetch_repo_via_gh_graphql:limit_stop_check",
          "stop_check",
          {{"iter", loop_iter},
           {"items_size", (int)out.items.size()},
           {"limit", opt.limit},
           {"reverse_order", opt.reverse_order},
           {"threshold_ts", threshold_ts},
           {"threshold_known", threshold_known},
           {"last_issue_number", last_issue_number},
           {"last_pr_number", last_pr_number},
           {"last_issue_created_at", last_issue_created_at},
           {"last_pr_created_at", last_pr_created_at},
           {"has_issue_page", has_issue_page},
           {"has_pr_page", has_pr_page},
           {"bound_issue", bound_issue},
           {"bound_pr", bound_pr},
           {"bound_issue_known", bound_issue_known},
           {"bound_pr_known", bound_pr_known},
           {"stop_issues", stop_issues},
           {"stop_prs", stop_prs},
           {"can_stop", can_stop}});
      // #endregion

      if (stop_issues) has_issue_page = false;
      if (stop_prs) has_pr_page = false;
      // Defensive: if both are safe, stop loop immediately.
      if (can_stop) { has_issue_page = false; has_pr_page = false; }

      // If PR stream still matters for the limit window, increase PR batch for next loop.
      if (opt.limit > 0 && !stop_prs && has_pr_page) {
        // grow: 1 -> 10 -> 20 -> 40 -> 80 -> 100 (clamped)
        int next = pr_page_limit_mode * 2;
        if (next < 10) next = 10;
        pr_page_limit_mode = std::min(opt.per_page, std::min(opt.limit, next));
      }
    }
  }

  // Fetch additional comment pages per node, like gh2md.
  if (opt.include_comments && !needs_comment_pagination.empty()) {
    const auto cquery = node_comment_query();
    progress.tick("Fetching additional comment pages: " + std::to_string(needs_comment_pagination.size()) + " items ...", true);
    size_t done_cnt = 0;
    for (const auto& ref : needs_comment_pagination) {
      auto& item = out.items[ref.item_index];
      paginate_comments_for_item(cquery, ref.id, item, opt, progress);
      done_cnt++;
      progress.tick("Additional comments done: " + std::to_string(done_cnt) + "/" + std::to_string(needs_comment_pagination.size()) +
                        " (current #" + std::to_string(item.number) + ")",
                    false);
    }
  }

  // Fetch additional PR review pages per node (reviews / reviewThreads).
  if ((opt.include_pr_reviews || opt.include_pr_review_threads) && !needs_pr_review_pagination.empty()) {
    const auto rquery = node_pr_reviews_query();
    const auto tquery = node_pr_review_threads_query();
    const auto tcquery = node_review_thread_comments_query();
    progress.tick("Fetching additional PR review pages: " + std::to_string(needs_pr_review_pagination.size()) + " PRs ...", true);
    size_t done_cnt = 0;
    for (const auto& ref : needs_pr_review_pagination) {
      auto& item = out.items[ref.item_index];
      if (item.kind == ItemKind::PullRequest) {
        paginate_pr_reviews_for_item(rquery, ref.id, item, opt, progress);
        paginate_pr_review_threads_for_item(tquery, tcquery, ref.id, item, opt, progress);
      }
      done_cnt++;
      progress.tick("Additional PR review done: " + std::to_string(done_cnt) + "/" +
                        std::to_string(needs_pr_review_pagination.size()) +
                        " (current #" + std::to_string(item.number) + ")",
                    false);
    }
  }

  progress.done("Fetch complete. fetched_items=" + std::to_string(out.items.size()));

  return out;
}

RepoExport fetch_item_by_number_via_gh_graphql(
    const std::string& owner,
    const std::string& repo_name,
    int number,
    const FetchOptions& opt) {
  if (number <= 0) {
    throw std::invalid_argument("fetch_item_by_number_via_gh_graphql: number must be > 0");
  }

  RepoExport out;

  ProgressPrinter progress(opt.progress_enabled, opt.progress_interval_ms);
  progress.tick(
      "Fetching " + owner + "/" + repo_name + " #" + std::to_string(number) + " ...",
      true);

  const auto query = single_item_query();

  std::vector<std::pair<std::string, std::string>> vars;
  vars.emplace_back("owner", owner);
  vars.emplace_back("repo", repo_name);
  vars.emplace_back("number", std::to_string(number));
  vars.emplace_back("commentPerPage", std::to_string(opt.comment_per_page));
  vars.emplace_back("labelsFirst", std::to_string(opt.labels_first));
  vars.emplace_back("assigneesFirst", std::to_string(opt.assignees_first));

  vars.emplace_back("includeComments", opt.include_comments ? "true" : "false");
  vars.emplace_back("includePrReviewDecision", opt.include_pr_review_decision ? "true" : "false");
  vars.emplace_back("includePrReviews", opt.include_pr_reviews ? "true" : "false");
  vars.emplace_back("includePrReviewThreads", opt.include_pr_review_threads ? "true" : "false");
  vars.emplace_back("includeLabels", opt.include_labels ? "true" : "false");
  vars.emplace_back("includeAssignees", opt.include_assignees ? "true" : "false");
  vars.emplace_back("includeMilestone", opt.include_milestone ? "true" : "false");
  vars.emplace_back("includeReactions", opt.include_reactions ? "true" : "false");

  auto json = gh_api_graphql(query, vars, opt);

  auto data = json.find("data");
  if (data == json.end() || !data->is_object()) {
    throw std::runtime_error("unexpected graphql response: missing data");
  }
  auto repo = data->find("repository");
  if (repo == data->end() || !repo->is_object()) {
    throw std::runtime_error("unexpected graphql response: missing repository");
  }

  out.full_name = jstr(*repo, "nameWithOwner");
  out.url = jstr(*repo, "url");

  auto iopr = repo->find("issueOrPullRequest");
  if (iopr == repo->end() || iopr->is_null() || !iopr->is_object()) {
    throw std::runtime_error(std::string("not found: ") + owner + "/" + repo_name + " #" + std::to_string(number));
  }

  const auto type = jstr(*iopr, "__typename");
  ItemKind kind = ItemKind::Issue;
  if (type == "PullRequest") {
    kind = ItemKind::PullRequest;
  } else if (type == "Issue") {
    kind = ItemKind::Issue;
  } else {
    throw std::runtime_error(std::string("unexpected __typename for issueOrPullRequest: ") + type);
  }

  auto id = jstr(*iopr, "id");
  out.items.push_back(parse_item_node(*iopr, kind, opt));

  // Additional comment pagination for this single node.
  if (opt.include_comments && !id.empty()) {
    const auto cquery = node_comment_query();
    paginate_comments_for_item(cquery, id, out.items[0], opt, progress);
  }

  // Additional PR review pagination for this single PR node.
  if ((opt.include_pr_reviews || opt.include_pr_review_threads) && !id.empty() && kind == ItemKind::PullRequest) {
    const auto rquery = node_pr_reviews_query();
    const auto tquery = node_pr_review_threads_query();
    const auto tcquery = node_review_thread_comments_query();
    paginate_pr_reviews_for_item(rquery, id, out.items[0], opt, progress);
    paginate_pr_review_threads_for_item(tquery, tcquery, id, out.items[0], opt, progress);
  }

  progress.done("Fetch complete. fetched_items=" + std::to_string(out.items.size()));
  return out;
}

}  // namespace ghx


