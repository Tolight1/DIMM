# DIMM Codex 开发规则

## 规则优先级

- 本文件定义仓库级安全规则和不可违反的边界。
- 构建与验证的具体命令见项目根目录的 `CODEX_BUILD_HANDOFF.md`；如该文件存在，构建前必须先读取。
- 分支、提交、推送和 Pull Request 的具体流程见项目根目录的 `CODEX_GITHUB_WORKFLOW.md`。
- 如果流程文件与本文件冲突，以本文件的安全规则为准。

## 仓库约定

- `master` 是主分支，应保持可用并与 GitHub 上的 `origin/master` 同步。
- 开始任务前先检查工作区状态；如果存在用户的已修改文件或未跟踪文件，必须保留，不得强行覆盖、清理或回退。
- 代码开发默认从最新 `master` 创建功能分支，分支统一使用 `codex/` 前缀，例如 `codex/fix-star-detection`。
- 当前正式源码目录是 `src/`，测试目录是 `tests/`。不要复制或创建 `src1`、`src2`、`src_new` 等完整源码副本；历史版本通过 Git 提交、分支和 Pull Request 查看。
- `build/`、`.pytest_cache/`、`__pycache__/`、可执行文件、DLL 和观测数据等生成物不应提交；以仓库根目录的 `.gitignore` 为准。
- 开发前后都要检查 `git status`，保留用户已有的未跟踪文件。除非用户明确要求，不要删除、覆盖、批量整理或顺手提交这些文件。
- 只修改与当前任务相关的文件，避免无关格式化和大范围重写。
- 不要把个人 GitHub 令牌、密码、绝对路径配置或其他敏感信息写入项目文件、脚本或聊天内容。

## 推荐开发流程

1. 先读取本文件，并检查工作区：

   ```powershell
   git status --short --branch
   ```

2. 需要从主线开始时，同步并创建功能分支：

   ```powershell
   git switch master
   git pull --ff-only origin master
   git switch -c codex/<功能名称>
   ```

3. 修改代码并运行与任务相关的测试；完成后检查：

   ```powershell
   git status
   git diff
   ```

4. 只暂存当前任务相关文件并提交，提交信息使用清晰的类型前缀，例如 `feat:`、`fix:`、`docs:`、`test:` 或 `chore:`。

5. 验证通过后推送功能分支并创建目标为 `master` 的 Pull Request。PR 描述应说明改动内容、验证命令和结果、已知警告或限制。

6. 未经用户明确确认，不要合并 Pull Request。PR 合并后回到本地 `master` 并同步；确认分支不再需要后，再删除已合并的本地和远程分支。

## 构建与验证入口

- 构建或发布验证前，必须先读取 `CODEX_BUILD_HANDOFF.md`，按其中的固定工具、生成器、构建目录和 Release 检查流程执行。
- 不要混用 Visual Studio、MinGW 或其他 CMake 生成器；不要因为普通构建失败就删除整个 `build/` 目录。
- Python 测试优先使用仓库已有的测试入口；当前标准库后备命令为：

  ```powershell
  python -m unittest discover -s tests -p "test*.py" -q
  ```

- 如果环境已安装 pytest，也可运行：

  ```powershell
  python -m pytest -q
  ```

- Qt/C++ 改动应根据风险执行相应的 CMake 配置、Release 构建、CTest 或程序启动检查；涉及部署时还要检查 `build/Release` 的完整运行库和输出文件。
- 只有实际运行并观察到成功结果后，才能在交付说明中描述测试、构建或启动检查为通过。

## 完成任务前

- 根据任务风险运行适当的验证，不要把未运行的测试描述为已通过。
- 如果测试或构建失败，区分本次改动引入的问题和已有基线问题，并在交付说明中如实报告。
- 提交或创建 PR 前再次确认 `src/` 没有未经任务要求的变化，并确认只提交了相关文件。
