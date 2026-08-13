# Git Repository Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不修改当前最新 `src/` 内容的前提下，降低工作区噪声，明确本地分支/工作树状态，并为后续安全提交到 GitHub 做准备。

**Architecture:** 先建立只读基线并验证 `src/` 文件清单与哈希；再仅修改 Git 管理文件（`.gitignore`、可选的 `.gitattributes`）来隔离构建产物和本地工具状态；最后按功能/文档/测试/资源分类盘点未跟踪内容。任何提交、分支推送、远程分支创建或删除都作为单独的用户确认点。

**Tech Stack:** Git、PowerShell、CMake/Qt 项目约定、GitHub 远程仓库。

---

### Task 1: 建立源码保护基线

**Files:**
- Read-only: `src/**`
- Read-only: `.gitignore`
- Read-only: `.git/config`
- Create outside repository if needed: temporary status/hash report

- [ ] **Step 1: Record current repository state**

Run:
```powershell
git status --short --branch
git branch -vv --all
git worktree list --porcelain
git remote -v
```

Expected: 只读输出当前分支、第二个 worktree、远程 URL 和未提交状态；不执行任何清理或重置。

- [ ] **Step 2: Record `src/` file inventory and hashes**

Run:
```powershell
Get-ChildItem -LiteralPath src -Recurse -File | Get-FileHash -Algorithm SHA256
```

Expected: 只读生成当前源码基线，后续整理完成后再次执行并比较；任何哈希变化都停止后续操作并报告。

### Task 2: 隔离构建产物和本地状态

**Files:**
- Modify: `.gitignore`
- Create: `.gitattributes`
- Read-only: `src/**`

- [ ] **Step 1: Preserve existing `.gitignore` edits**

Run:
```powershell
git diff -- .gitignore
```

Expected: 先确认用户已有修改，再以补丁方式追加规则，不覆盖已有内容。

- [ ] **Step 2: Add only repository-management rules**

追加以下类别：构建目录（`build*/`、`DIMM_release/`）、本地 agent/worktree 状态（`.superpowers/`、`.agents/`、`.codex/`）、网络文档解包/渲染目录、Python 缓存和 CMake 临时文件。不得添加 `src/`、`tests/`、`docs/`、`resources/` 或 `scripts/` 的整体忽略规则。

- [ ] **Step 3: Add line-ending policy without rewriting files**

创建 `.gitattributes`，对 C/C++、Qt UI、Python、CMake、Markdown 和文本文件统一声明 LF；Windows 批处理文件声明 CRLF。只创建策略文件，不运行会批量改写工作区文件的格式化命令。

- [ ] **Step 4: Verify ignore behavior**

Run:
```powershell
git check-ignore -v build build-comm-protocol-check build-comm-protocol-mingw build_codex_vs18 DIMM_release network_comm_render network_comm_unpacked tests/__pycache__ .superpowers
git status --short --branch
```

Expected: 这些本地目录不再作为未跟踪项出现；`src/`、`tests/`、`docs/`、`resources/`、`scripts/` 中的真实项目文件仍然可见。

### Task 3: 盘点并保护最新源码集合

**Files:**
- Read-only: `src/**`, `tests/**`, `docs/**`, `resources/**`, `scripts/**`, `tools/**`
- No source edits

- [ ] **Step 1: Classify untracked paths**

按 `src/`、`tests/`、`docs/`、`resources/`、`scripts/`、`tools/`、根目录资料分别统计，不移动、不删除、不改名。

- [ ] **Step 2: Separate generated files from project files**

只生成清单和建议，不自动删除或移动任何文件。重点识别构建输出、发布目录、解包目录、缓存、外部文档与实际源码/测试。

- [ ] **Step 3: Recompute `src/` hashes**

Expected: 与 Task 1 基线完全一致；若不一致，停止并报告。

### Task 4: 准备 GitHub 同步决策

**Files:**
- Read-only: `.gitignore`, `.gitattributes`, `src/**`
- No remote writes without confirmation

- [ ] **Step 1: Show candidate commit groups**

至少拆成：仓库治理文件、核心源码拆分、功能特性、测试、文档、资源/工具。不得自动使用 `git add .`。

- [ ] **Step 2: Inspect branch relationships**

确认当前功能分支、本地 `master`、`origin/master` 的差异和是否配置 upstream。

- [ ] **Step 3: Stop before external GitHub mutation**

在创建远程分支、推送、创建 PR、修改默认分支或保护规则前，向用户展示准确目标和文件范围并获取确认。

