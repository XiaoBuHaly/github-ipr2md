#include "i18n.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace ghx {

namespace {

// NOTE: Keep the keys stable; use them across the project.
struct Entry {
  const char* key;
  const char* en;
  const char* zh_cn;
};

static const Entry kEntries[] = {
    // Generic
    {"cli.error.prefix", "Error: ", "错误: "},
    {"cli.wrote", "Wrote: {path}", "已写入: {path}"},
    {"cli.wrote_dir", "Wrote directory: {path}", "已写入目录: {path}"},

    // Main stages
    {"stage.selecting_items", "Selecting items: keep {keep}/{total}{limit_suffix}", "筛选条目: 保留 {keep}/{total}{limit_suffix}"},
    {"stage.writing_markdown", "Writing markdown: {cur}/{total}", "写入 Markdown: {cur}/{total}"},
    {"stage.writing_per_item", "Writing per-item: {cur}/{total}", "逐条写入: {cur}/{total}"},
    {"stage.wrote_chunk", "Wrote chunk {idx} ({count} items)", "已写入分片 {idx}（{count} 条）"},

    // Fetching/progress
    {"progress.fetching_repo", "Fetching {owner_repo} ...", "正在抓取 {owner_repo} ..."},
    {"progress.fetching_single", "Fetching {owner_repo} #{number} ...", "正在抓取 {owner_repo} #{number} ..."},
    {"progress.fetched_counts", "Fetched issues={issues} prs={prs} items={items}", "已抓取 issue={issues} pr={prs} 条目={items}"},
    {"progress.fetched_counts_total", "Fetched issues={issues}/{total} prs={prs} items={items}", "已抓取 issue={issues}/{total} pr={prs} 条目={items}"},
    {"progress.fetched_prs_total", "Fetched issues={issues} prs={prs}/{total} items={items}", "已抓取 issue={issues} pr={prs}/{total} 条目={items}"},
    {"progress.fetch_more_comments", "Fetching additional comment pages: {count} items ...", "正在抓取更多评论分页: {count} 条 ..."},
    {"progress.comments_paging", "Comments paging: #{number} ...", "评论分页中: #{number} ..."},
    {"progress.comments_done",
     "Additional comments done: {done}/{total} (current #{number})",
     "评论追加分页完成: {done}/{total}（当前 #{number}）"},
    {"progress.fetch_more_pr_reviews", "Fetching additional PR review pages: {count} PRs ...", "正在抓取更多 PR Review 分页: {count} 个 PR ..."},
    {"progress.fetch_complete", "Fetch complete. fetched_items={count}", "抓取完成。fetched_items={count}"},

    // PR review pagination
    {"progress.pr_reviews_paging", "PR reviews paging: #{number} ...", "PR reviews 分页中: #{number} ..."},
    {"progress.pr_threads_paging", "PR reviewThreads paging: #{number} ...", "PR reviewThreads 分页中: #{number} ..."},
    {"progress.pr_thread_comments_paging", "PR review thread comments paging: #{number} ...", "PR review thread 评论分页中: #{number} ..."},
    {"progress.pr_review_done",
     "Additional PR review done: {done}/{total} (current #{number})",
     "PR review 追加分页完成: {done}/{total}（当前 #{number}）"},

    // Markdown headings / labels
    {"md.body", "Body", "正文"},
    {"md.pr_review_decision", "PR Review Decision", "PR Review 决策"},
    {"md.pr_reviews", "PR Reviews", "PR Reviews"},
    {"md.pr_review_threads", "PR Review Threads", "PR Review Threads"},
    {"md.comments", "Comments", "评论"},
    {"md.none", "(none)", "(无)"},
    {"md.empty", "(empty)", "(空)"},
    {"md.body_omitted", "(body omitted)", "(正文已省略)"},
    {"md.unknown", "(unknown)", "(未知)"},
    {"md.reactions", "Reactions: {reactions}", "回应: {reactions}"},
    {"md.thread_comments", "Comments ({count})", "评论（{count}）"},

    {"md.meta.type", "Type", "类型"},
    {"md.meta.state", "State", "状态"},
    {"md.meta.url", "URL", "链接"},
    {"md.meta.author", "Author", "作者"},
    {"md.meta.assignees", "Assignees", "负责人"},
    {"md.meta.milestone", "Milestone", "里程碑"},
    {"md.meta.created_at", "CreatedAt", "创建时间"},
    {"md.meta.updated_at", "UpdatedAt", "更新时间"},
    {"md.meta.closed_at", "ClosedAt", "关闭时间"},
    {"md.meta.merged_at", "MergedAt", "合并时间"},
    {"md.meta.labels", "Labels", "标签"},

    {"md.preamble.repo", "Repo", "仓库"},
    {"md.preamble.generated_at", "GeneratedAt", "生成时间"},
    {"md.preamble.items", "Items", "条目数"},
    {"md.preamble.filter", "Filter", "筛选"},
    {"md.preamble.order", "Order", "排序"},
    {"md.preamble.limit", "Limit", "限制"},
    {"md.preamble.truncated", "Truncated", "已截断"},
    {"md.preamble.available", "available", "可用"},
    {"md.preamble.range", "Range", "范围"},
    {"md.preamble.created_at_range", "CreatedAtRange", "创建时间范围"},
    {"md.preamble.stats", "Stats", "统计"},

    {"md.value.true", "true", "是"},
    {"md.value.false", "false", "否"},

    {"md.include.issues_prs", "issues+prs", "issues+prs"},
    {"md.include.issues", "issues", "issues"},
    {"md.include.prs", "prs", "prs"},
    {"md.include.none", "none", "none"},

    {"md.thread.line", "line", "行"},
    {"md.thread.original_line", "originalLine", "原始行"},
    {"md.thread.resolved", "resolved", "已解决"},
    {"md.thread.outdated", "outdated", "已过期"},

    // CLI help / descriptions
    {"cli.app.desc",
     "Export GitHub issues/PRs (and comments/metadata) to Markdown via `gh api graphql`.",
     "通过 `gh api graphql` 将 GitHub Issue/PR（含评论/元数据）导出为 Markdown。"},
    {"cli.help.repo", "Repo in owner/name format. Default: infer from git remote.", "仓库：owner/name 格式。默认从 git remote 推断。"},
    {"cli.help.in", "Convert existing JSON (from `gh issue list --json ...`) to Markdown.", "离线转换：把现有 JSON（如 `gh issue list --json ...`）转换为 Markdown。"},
    {"cli.help.out", "Output path. Default: ./output.md (or a directory when --split/--per-item).", "输出路径。默认：./output.md（--split/--per-item 时为目录）。"},
    {"cli.help.id", "Export a single Issue/PR by number (requires --repo or infer from git remote).", "按编号导出单个 Issue/PR（需要 --repo 或从 git remote 推断）。"},
    {"cli.help.state", "Filter state: all|open|closed. Default: all.", "状态筛选：all|open|closed。默认：all。"},
    {"cli.help.limit", "Max number of items (issues+prs). 0 means unlimited. Default: 0.", "最多导出条目数（issues+prs）。0 表示不限。默认：0。"},
    {"cli.help.reverse", "Reverse output order (default is ascending by number).", "反转输出顺序（默认按编号升序）。"},
    {"cli.help.no_progress", "Disable progress output (fetch/select/write). Still prints final Wrote/Stats. Use --quiet to silence everything except errors.", "禁用进度输出（抓取/筛选/写入）。仍会输出最终 Wrote/Stats。用 --quiet 可仅保留错误输出。"},
    {"cli.help.quiet", "Quiet mode: no output except errors.", "静默模式：除错误外不输出。"},
    {"cli.help.progress_interval", "Progress refresh interval in ms. Default: 100.", "进度刷新间隔（毫秒）。默认：100。"},
    {"cli.help.split", "Split output into multiple files, each file max N items. Requires --out be a directory.", "分片输出：每个文件最多 N 条。要求 --out 为目录。"},
    {"cli.help.per_item", "Write one file per issue/PR. Requires --out be a directory. Mutually exclusive with --split.", "每条一个文件。要求 --out 为目录。与 --split 互斥。"},
    {"cli.help.idempotent", "Deterministic output: no generated timestamps; stable sorting where applicable.", "稳定输出：不写生成时间戳；在可行处使用稳定排序。"},
    {"cli.help.title", "Markdown document title. Default: \"Issues Export\".", "Markdown 文档标题。默认：\"Issues Export\"。"},
    {"cli.help.stats_json", "Write stats as JSON to this path.", "把统计信息写入该路径（JSON）。"},
    {"cli.help.hostname", "GitHub hostname for `gh api` (default: github.com).", "`gh api` 的 GitHub hostname（默认：github.com）。"},
    {"cli.help.lang", "UI language: en|zh-CN. Default: infer from env (GHX_LANG/LANG/LC_ALL) or en.", "界面语言：en|zh-CN。默认从环境变量（GHX_LANG/LANG/LC_ALL）推断，否则 en。"},
    {"cli.help.pr_review", "Export PR review data: none|decision|reviews|threads. Default: none. Note: reviewThreads can be expensive.", "导出 PR review 数据：none|decision|reviews|threads。默认：none。注意：threads 可能较慢。"},
    {"cli.help.labels_first", "Per-item GraphQL limit: labels(first: N). Default: 100 (may truncate if more).", "每条 GraphQL 限制：labels(first: N)。默认：100（超过可能截断）。"},
    {"cli.help.assignees_first", "Per-item GraphQL limit: assignees(first: N). Default: 20 (may truncate if more).", "每条 GraphQL 限制：assignees(first: N)。默认：20（超过可能截断）。"},
    {"cli.help.no_issues", "Do not include issues.", "不导出 issues。"},
    {"cli.help.no_prs", "Do not include pull requests.", "不导出 pull requests。"},
    {"cli.help.no_body", "Do not export body text.", "不导出正文。"},
    {"cli.help.no_comments", "Do not export comments.", "不导出评论。"},
    {"cli.help.no_labels", "Do not export labels.", "不导出标签。"},
    {"cli.help.no_reactions", "Do not export reactions.", "不导出回应（reactions）。"},
    {"cli.help.no_authors", "Do not export author/authorAssociation.", "不导出作者/关联关系。"},
    {"cli.help.no_timestamps", "Do not export timestamps.", "不导出时间戳。"},
    {"cli.help.no_links", "Do not export URLs/links in metadata.", "不导出元数据里的链接。"},
    {"cli.help.no_assignees", "Do not export assignees.", "不导出负责人。"},
    {"cli.help.no_milestone", "Do not export milestone.", "不导出里程碑。"},
    {"md.default_title", "Issues Export", "Issues 导出"},
};

static std::string g_locale = "en";  // normalized: "en" or "zh-CN"

static const Entry* find_entry(std::string_view key) {
  for (const auto& e : kEntries) {
    if (key == e.key) return &e;
  }
  return nullptr;
}

}  // namespace

std::string I18n::normalize_locale(std::string_view locale) {
  std::string s(locale);
  s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; }), s.end());
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });

  if (s == "zh" || s == "zh-cn" || s == "zh_cn" || s == "zh-hans" || s == "zh-hans-cn") {
    return "zh-CN";
  }
  return "en";
}

void I18n::set_locale(std::string locale) { g_locale = normalize_locale(locale); }

std::string I18n::locale() { return g_locale; }

std::string_view I18n::t(std::string_view key) {
  const auto* e = find_entry(key);
  if (!e) return key;
  if (g_locale == "zh-CN") return e->zh_cn ? std::string_view(e->zh_cn) : std::string_view(e->en);
  return e->en ? std::string_view(e->en) : std::string_view(key);
}

std::string I18n::replace_all(std::string s, std::string_view from, std::string_view to) {
  if (from.empty()) return s;
  size_t pos = 0;
  while (true) {
    pos = s.find(from, pos);
    if (pos == std::string::npos) break;
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

std::string I18n::tf(
    std::string_view key,
    const std::vector<std::pair<std::string_view, std::string>>& vars) {
  std::string out(t(key));
  for (const auto& kv : vars) {
    const auto placeholder = std::string("{") + std::string(kv.first) + "}";
    out = replace_all(std::move(out), placeholder, kv.second);
  }
  return out;
}

}  // namespace ghx

