# Codex 开发规则

## 仓库约定

- `master` 是主分支，应保持可用并与 GitHub 上的 `origin/master` 同步。
- 开始开发前先更新 `master`，再创建新的功能分支。功能分支默认使用 `codex/` 前缀，例如 `codex/fix-star-detection`。
- 当前正式源码目录是 `src/`。不要再复制或创建 `src1`、`src2`、`src_new` 等完整源码副本；历史版本通过 Git 提交和分支查看。
- 开发前后都要检查 `git status`，保留用户已有的未跟踪文件。除非用户明确要求，不要删除、覆盖或批量整理这些文件，也不要把它们顺手加入提交。
- 只修改与当前任务相关的文件，避免无关格式化和大范围重写。

## 推荐开发流程

1. 同步主分支：

   ```powershell
   git switch master
   git pull --ff-only origin master
   ```

2. 创建功能分支：

   ```powershell
   git switch -c codex/<功能名称>
   ```

3. 修改代码并运行与任务相关的测试，检查：

   ```powershell
   git status
   git diff
   ```

4. 只暂存相关文件并提交。提交信息使用清晰的类型前缀，例如 `feat:`、`fix:`、`docs:`、`test:` 或 `chore:`。

5. 推送功能分支并创建 Pull Request，目标分支为 `master`。PR 描述应说明改动内容、验证方式和已知问题；不要未经用户确认直接合并 PR。

6. PR 合并后回到本地 `master` 并同步；确认不再需要后，再删除已合并的本地和远程功能分支。

## 完成任务前

- 必须根据任务风险运行适当的验证，不要把未运行的测试描述为已通过。
- 如果测试失败，区分本次改动引入的问题和已有基线问题，并在交付说明中如实报告。
- 提交或创建 PR 前再次确认 `src/` 没有未经任务要求的变化。
