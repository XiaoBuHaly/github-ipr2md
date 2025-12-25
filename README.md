# github-ipr2md

A high-performance C++ CLI tool to export **GitHub Issues and Pull Requests** to Markdown, leveraging the native **`gh`** (GitHub CLI) for seamless authentication and data retrieval.

Supports exporting bodies, comments, labels, reactions, milestones, assignees, and metadata with extreme speed and flexibility.

## Features

- **Fast & Native**: Built with C++17. Uses your existing `gh auth` login session—no new tokens to manage.
- **Comprehensive**: Exports full history including comments, reactions, labels, milestones, and extensive metadata.
- **Flexible Output**:
    - **Single File**: Consolidate everything into one `issues.md`.
    - **Split Files**: Paginate into chunks (e.g., 100 items per file).
    - **One-per-Item**: Generate individual Markdown files for every issue and PR.
- **Highly Customizable**: Toggles for every field (e.g., `--no-comments`, `--no-reactions`).
- **Offline/JSON Support**: Convert existing JSON dumps (from `gh issue list --json`) directly to Markdown without fetching from GitHub.
- **CI/CD Ready**: Supports idempotent mode for stable git-tracked backups.

---

## Quick Start

No manual compilation required. The included wrapper script automatically handles CMake configuration, building, and execution.

### Linux / macOS

```bash
chmod +x ./run.sh
./run.sh
```

By default, this infers the repository from your current git directory and exports everything to `./issues.md`.

### Windows (CMD/PowerShell)

```bat
run.bat
```

> **Note**: The first run will compile the project. Subsequent runs skip compilation if the executable is already present.

---

## Installation

### Prerequisites
- **[GitHub CLI (`gh`)](https://cli.github.com/)** (Logged in via `gh auth login`)
- **C++17 Compiler** (GCC/Clang/MSVC)
- **CMake**

### Manual Build

If you prefer to build manually instead of using the wrapper scripts:

```bash
cmake -S . -B build
cmake --build build -j
# Binary located at: ./build/github-ipr2md
```

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
./run.sh --repo owner/repo --idempotent --out issues.md
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
./run.sh --in issues.json --out issues.md
```

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
| `--repo owner/name` | Specify repository (defaults to git remote origin). |
| `--in PATH.json` | Convert an existing JSON file (offline mode). |
| `--out PATH` | Output file path (or directory for split/per-item modes). |

### Filtering & Scope
| Flag | Description |
| :--- | :--- |
| `--state [all/open/closed]` | Filter by state (Default: `all`). |
| `--limit N` | Max number of items to export (0 = unlimited). |
| `--reverse` | Reverse sort order (Default: Ascending by number). |
| `--no-issues` | Skip issues. |
| `--no-prs` | Skip pull requests. |

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
- `--no-stats` (Suppress stdout stats)

### Advanced
| Flag | Description |
| :--- | :--- |
| `--split N` | Split output into files containing N items each. |
| `--per-item` | Create one Markdown file per Issue/PR. |
| `--idempotent` | Remove generation timestamps for stable git diffs. |
| `--labels-first N` | Max labels to fetch per item (GraphQL limit, default 100). |
| `--assignees-first N` | Max assignees to fetch per item (GraphQL limit, default 20). |
| `--progress-interval-ms N` | Refresh rate for progress bar (default 100ms). |
