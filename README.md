# github-ipr2md

A high-performance C++ CLI tool to export **GitHub Issues and Pull Requests** to Markdown, leveraging the native **`gh`** (GitHub CLI) for seamless authentication and data retrieval.

Supports exporting bodies, comments, labels, reactions, milestones, assignees, and metadata with extreme speed and flexibility.

## Features

- **Fast & Native**: Built with C++17. Uses your existing `gh auth` login session—no new tokens to manage.
- **Comprehensive**: Exports full history including comments, reactions, labels, milestones, and extensive metadata.
- **Flexible Output**:
    - **Single File**: Consolidate everything into one `output.md`.
    - **Split Files**: Paginate into chunks (e.g., 100 items per file).
    - **One-per-Item**: Generate individual Markdown files for every issue and PR.
- **Highly Customizable**: Toggles for every field (e.g., `--no-comments`, `--no-reactions`).
- **Offline/JSON Support**: Convert an existing JSON array (e.g. from `gh issue list --json ...`) to Markdown without fetching from GitHub (issues-only; fields depend on what your JSON contains).
- **CI/CD Ready**: Supports idempotent mode for stable git-tracked backups.

---

## Quick Start

No manual compilation required. The included wrapper script automatically handles CMake configuration, building, and execution.

### Linux / macOS

```bash
chmod +x ./run.sh
./run.sh -- --help
```

By default, this infers the repository from your current git directory and exports everything to `./output.md` (if you run without args).

### Windows (CMD/PowerShell)

```bat
run.bat -- --help
```

> **Note**: The first run will compile the project. Subsequent runs skip compilation if the executable is already present.

### Authentication sanity check

This tool shells out to `gh api graphql`, so make sure `gh` is installed and logged in:

```bash
gh auth status
```

---

## Wrapper Script Flags (`run.sh` / `run.bat`)

Both `run.sh` (Linux/macOS) and `run.bat` (Windows) accept the same `--run-*` wrapper flags. These flags are **not** forwarded to `github-ipr2md`. Use `--` to separate wrapper flags from program flags if needed.

### Common Flags

- `--run-build-dir <dir>`: Build directory (default: `./build`)
- `--run-build-type <type>`: `Debug|Release|RelWithDebInfo|MinSizeRel` (default: `Release`)
- `--run-build`: Force **configure + build** (no directory deletion)
- `--run-clean`: **Clean rebuild** (delete build dir, then configure + build)
- `--run-no-build`: Run only, do not build (the executable must already exist)
- `--run-werror`: Configure with `-DENABLE_WERROR=ON` (also enabled in CI)
- `--run-help` (Windows also supports `-h`): Show wrapper help

### Examples

Linux/macOS:

```bash
./run.sh --run-clean -- --help
./run.sh --run-build-type Debug -- --help
./run.sh --run-build --repo owner/repo --out output.md
```

Windows:

```bat
run.bat --run-clean -- --help
run.bat --run-build-type Debug -- --help
run.bat --run-build --repo owner/repo --out output.md
run.bat --run-build --repo https://github.com/owner/repo/issues/123 --out issue-123.md
```

---

## Installation

### Prerequisites
- **[GitHub CLI (`gh`)](https://cli.github.com/)** (Logged in via `gh auth login`)
- **C++17 Compiler** (GCC/Clang/MSVC)
- **CMake**
- **Git** (optional; only needed if you rely on `--repo` inference from the current directory)

### Manual Build

If you prefer to build manually instead of using the wrapper scripts:

```bash
cmake -S . -B build
cmake --build build -j
# Binary located at: ./build/github-ipr2md
```

On Windows with Visual Studio generators, the binary is typically under `.\build\Release\github-ipr2md.exe` (or `Debug/`), depending on `--config`.

---

## Usage Examples

### 1. Standard Export
Export a specific repository to a single file:

```bash
./run.sh --repo owner/repo --out export.md
```

### 2. Archival / Backup (Idempotent)
Generate output suitable for version control (git). This mode disables generation timestamps and enforces stable sorting to minimize diff noise.

```bash
./run.sh --repo owner/repo --idempotent --out output.md
```

### 3. Migration (One File Per Issue)
Export each issue and PR as a separate Markdown file in a directory:

```bash
# Creates ./archive/issue-1.md, ./archive/pr-2.md, etc.
./run.sh --repo owner/repo --per-item --out ./archive/
```

### 4. Split / Pagination
Avoid huge files by splitting output into chunks (e.g., 100 items per file):

```bash
# Creates ./export/chunk-01.md, ./export/chunk-02.md...
./run.sh --repo owner/repo --split 100 --out ./export/
```

### 5. Convert Local JSON
Convert a JSON file exported by `gh` (e.g., `gh issue list --json ...`) into Markdown without network requests.

```bash
gh issue list --repo owner/repo --state all --limit 1000 \
  --json number,title,state,url,body,createdAt,updatedAt,closedAt,author,authorAssociation,labels,assignees,milestone \
  > issues.json
./run.sh --in issues.json --out output.md
```

Notes:
- `--in` currently treats all items as **Issues** (no PRs).
- `gh issue list` JSON does **not** include comment bodies; `--in` will only include comments if your JSON has a `comments: [...]` array of comment objects.

### 6. Minimal Export
Export only the core content, skipping supplementary data to reduce noise.

```bash
./run.sh --repo owner/repo \
  --no-comments \
  --no-reactions \
  --no-labels \
  --out minimal.md
```

---

## Command Line Reference

### Input / Output
| Flag | Description |
| :--- | :--- |
| `--repo owner/name` | Specify repository (defaults to git remote origin). Also accepts `/owner/name`, `https://github.com/owner/name`, or even an Issue/PR URL like `https://github.com/owner/name/issues/123` (auto-extracts repo+id). |
| `--in PATH.json` | Convert an existing JSON file (offline mode). |
| `--out PATH` | Output file path (or directory for split/per-item modes). |
| `--id N` | Export a single Issue/PR by number (requires `--repo` or git remote inference). |
| `--title TEXT` | Markdown document title (default: `Issues Export`). |
| `--stats-json PATH.json` | Write stats summary as JSON to this path. |
| `--hostname HOST` | Pass `--hostname` through to `gh api` (useful for GitHub Enterprise). |

### Filtering & Scope
| Flag | Description |
| :--- | :--- |
| `--state [all/open/closed]` | Filter by state (Default: `all`). |
| `--limit N` | Max number of items to export (0 = unlimited). |
| `--reverse` | Reverse sort order (default is oldest-first / ascending). |
| `--no-issues` | Skip issues. |
| `--no-prs` | Skip pull requests. |
| `--no-progress` | Disable progress output (stderr) for fetch/select/write phases. Still prints final `Wrote:` and Stats. |
| `--quiet` | Quiet mode: no output except errors. |
| `--pr-review` | Export PR review data: `none|decision|reviews|threads` (default: `none`). `threads` fetches reviewThreads + inline comments and can be expensive. |

### Content Toggles
Disable specific sections with these flags:
- `--no-body`
- `--no-comments`
- `--no-labels`
- `--no-reactions`
- `--no-authors`
- `--no-timestamps`
- `--no-links`
- `--no-assignees`
- `--no-milestone`

PR review export:
- `--pr-review none|decision|reviews|threads` (default: `none`)
  - `decision`: export PR `reviewDecision` summary
  - `reviews`: export `reviewDecision` + each review record
  - `threads`: export `reviewDecision` + reviews + inline reviewThreads (Files changed comments), fully paginated
  - Not supported with `--in` (offline JSON conversion); use online GraphQL fetch.

### Advanced
| Flag | Description |
| :--- | :--- |
| `--split N` | Split output into files containing N items each. |
| `--per-item` | Create one Markdown file per Issue/PR. |
| `--idempotent` | Remove generation timestamps for stable git diffs. |
| `--labels-first N` | Max labels to fetch per item (GraphQL connection cap, default 100; not fully paginated). |
| `--assignees-first N` | Max assignees to fetch per item (GraphQL connection cap, default 20; not fully paginated). |
| `--progress-interval-ms N` | Refresh rate for progress bar (default 100ms). |

---

## Notes / Troubleshooting

- **Not in a git repo / cannot infer repo**: pass `--repo owner/name` explicitly. Repo inference uses `git remote get-url origin` (or the first remote).
- **`gh api graphql failed`**: run `gh auth status` and (re)login. For GitHub Enterprise, login with `gh auth login --hostname HOST` and run with `--hostname HOST`.
- **`--split` / `--per-item` output path**: `--out` must be a directory. If you accidentally pass `--out something.md` with split/per-item, the tool will treat it as a directory base name (e.g. `something/`).
- **Offline `--in` mode expectations**: it expects a JSON **array**. Only fields present in your JSON will be rendered.
- **Windows UNC paths**: if the repo is accessed via UNC (e.g. `\\wsl.localhost\...`), `run.bat` may choose a local `%TEMP%` build directory to avoid toolchain issues.
