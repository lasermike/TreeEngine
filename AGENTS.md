# Working in this repository

- Start with `git status --short`. If a file you need to edit already has changes, inspect its staged and unstaged diffs and build on the current contents. Continue past unrelated changes. Ask only if the requested edit would overwrite existing work and the intended result is unclear.
- Read [CLAUDE.md](CLAUDE.md) for the shared TreeEngine workflow before starting work. Codex uses the same branch and PR conventions, roadmap board and spec commands, validation, and engine conventions. The working-tree and authorization rules below govern when Codex takes those steps.
- Apply requested changes directly in the current working tree. Do not create before/after directories, duplicate source trees, or temporary review branches. For a requested new spec or new PR branch, use the shared branch naming and base. Continue existing branch work in place.
- Leave edits in the working tree for review. Stage and commit only when explicitly asked, and include only authorized changes, even if other changes are already staged. Open or update a PR when authorized. Follow the requested scope for remote actions.
- When asked to sync specs, use `python TreeEngine/Tools/specs/specs.py sync`. When asked for the next spec, or when continuing an authorized queue workflow after a merge, follow the shared roadmap procedure in [CLAUDE.md](CLAUDE.md), including the board status transitions. See [specs/README.md](specs/README.md) for the tool commands.
- Keep edits focused. Use four spaces per indentation level and put opening and closing braces on their own lines in brace-delimited code.
- Run relevant checks for local changes. Use the shared pre-PR validation when preparing a PR, and report any known failures or checks you could not run.
- Review edits in place using Git. Check newly created files separately because ordinary diffs omit untracked files. Summarize what changed and which checks passed, failed, or were skipped.
- When asked for Beyond Compare, use the configured Git difftool for the relevant paths. If it is unavailable, show the Git diff and report that it was unavailable.
