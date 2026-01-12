# github-ipr2md

一个高性能的 C++ 命令行工具，用于将 **GitHub Issues 和 Pull Requests** 导出为 Markdown。工具通过调用 **`gh`**（GitHub CLI）的 GraphQL 接口获取数据，复用你现有的 `gh auth` 登录状态，无需单独管理 token。

支持导出正文、评论、标签、回应（reactions）、里程碑、负责人、作者/关联关系等丰富元数据；也支持 PR Review（包括 reviewThreads/行内评论）。

---

## 快速开始

项目自带 `run.sh` / `run.bat` 包装脚本，自动执行 CMake 配置、编译与运行。

### Linux / macOS

```bash
chmod +x ./run.sh
./run.sh -- --help
```

### Windows（CMD/PowerShell）

```bat
run.bat -- --help
```

> 提示：第一次运行会编译项目；后续如果可执行文件已存在且未变更，则会跳过编译。

---

## 认证检查

本工具会调用 `gh api graphql`，请确保已安装并登录 `gh`：

```bash
gh auth status
```

---

## 用法示例

### 1) 标准导出

```bash
./run.sh --repo owner/repo --out export.md
```

### 2) 归档/备份（Idempotent）

用于版本控制（git）友好的稳定输出（无生成时间戳，排序更稳定）：

```bash
./run.sh --repo owner/repo --idempotent --out output.md
```

### 3) 每条一个文件（per-item）

```bash
./run.sh --repo owner/repo --per-item --out ./archive/
```

### 4) 分片输出（split）

```bash
./run.sh --repo owner/repo --split 100 --out ./export/
```

### 5) 离线 JSON 转换（--in）

把本地 JSON（例如由 `gh issue list --json ...` 导出的数组）转换为 Markdown，不发起网络请求：

```bash
gh issue list --repo owner/repo --state all --limit 1000 \
  --json number,title,state,url,body,createdAt,updatedAt,closedAt,author,authorAssociation,labels,assignees,milestone \
  > issues.json
./run.sh --in issues.json --out output.md
```

注意：
- `--in` 当前将所有条目视为 **Issue**（不包含 PR）。
- `gh issue list` 的 JSON 默认不包含评论正文；只有当你的 JSON 自带 `comments: [...]` 时，`--in` 才能输出评论。

---

## 关键参数

### 语言（g11n）

工具支持英文（`en`）与简体中文（`zh-CN`）。

```bash
./run.sh --repo owner/repo --lang zh-CN --out output.md
```

也可以通过环境变量设置（优先级：`GHX_LANG` > `LC_ALL` > `LANG`）：

```bash
set GHX_LANG=zh-CN
```

### PR Review 导出

```bash
./run.sh --repo owner/repo --pr-review threads --out output.md
```

可选值：
- `none`：不导出 PR Review（默认）
- `decision`：只导出 `reviewDecision`
- `reviews`：导出 `reviewDecision` + reviews
- `threads`：导出 `reviewDecision` + reviews + reviewThreads（Files changed 行内评论，包含分页；可能较慢）

> `--in`（离线 JSON）不支持 `--pr-review`，需要在线 GraphQL 抓取。

---

## 其它

如遇到 `gh api graphql failed`，请先执行 `gh auth status` 检查登录状态；GitHub Enterprise 请配合 `--hostname` 使用。

