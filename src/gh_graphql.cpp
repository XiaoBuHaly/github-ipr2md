#include "gh_graphql.h"

#include "process.h"
#include "progress.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ghx {

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

  int fetched_issues = 0;
  int fetched_prs = 0;

  std::string issue_cursor;
  std::string pr_cursor;
  bool has_issue_page = opt.include_issues;
  bool has_pr_page = opt.include_prs;

  const auto query = repo_query();

  // We keep the node IDs around to fetch additional comment pages.
  struct NodeRef { std::string id; size_t item_index; };
  std::vector<NodeRef> needs_comment_pagination;

  while (has_issue_page || has_pr_page) {
    std::vector<std::pair<std::string, std::string>> vars;
    vars.emplace_back("owner", owner);
    vars.emplace_back("repo", repo_name);
    vars.emplace_back("orderDirection", opt.reverse_order ? "DESC" : "ASC");
    vars.emplace_back("issuePerPage", opt.include_issues ? std::to_string(opt.per_page) : "0");
    vars.emplace_back("prPerPage", opt.include_prs ? std::to_string(opt.per_page) : "0");
    vars.emplace_back("commentPerPage", std::to_string(opt.comment_per_page));
    vars.emplace_back("labelsFirst", std::to_string(opt.labels_first));
    vars.emplace_back("assigneesFirst", std::to_string(opt.assignees_first));

    vars.emplace_back("includeIssues", opt.include_issues ? "true" : "false");
    vars.emplace_back("includePrs", opt.include_prs ? "true" : "false");
    vars.emplace_back("includeComments", opt.include_comments ? "true" : "false");
    vars.emplace_back("includeLabels", opt.include_labels ? "true" : "false");
    vars.emplace_back("includeAssignees", opt.include_assignees ? "true" : "false");
    vars.emplace_back("includeMilestone", opt.include_milestone ? "true" : "false");
    vars.emplace_back("includeReactions", opt.include_reactions ? "true" : "false");

    if (!issue_cursor.empty()) vars.emplace_back("issueCursor", issue_cursor);
    if (!pr_cursor.empty()) vars.emplace_back("prCursor", pr_cursor);

    if (opt.state == "open") {
      // arrays: issueStates[]=OPEN, prStates[]=OPEN
      vars.emplace_back("issueStates[]", "OPEN");
      vars.emplace_back("prStates[]", "OPEN");
    } else if (opt.state == "closed") {
      vars.emplace_back("issueStates[]", "CLOSED");
      vars.emplace_back("prStates[]", "CLOSED");
      vars.emplace_back("prStates[]", "MERGED");
    }

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
          }
        }
        progress.tick("Fetched issues=" + std::to_string(fetched_issues) + " prs=" + std::to_string(fetched_prs) +
                          "/" + std::to_string(total) + " items=" + std::to_string(out.items.size()),
                      false);
      } else {
        has_pr_page = false;
      }
    } else {
      has_pr_page = false;
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

  progress.done("Fetch complete. items=" + std::to_string(out.items.size()));

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

  progress.done("Fetch complete. items=" + std::to_string(out.items.size()));
  return out;
}

}  // namespace ghx


