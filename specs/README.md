# Specs

Specs for upcoming features. You write them here; Claude builds them in the order of the
[TreeEngine roadmap](https://github.com/users/lasermike/projects/1) project board on GitHub (use its Board layout:
columns Backlog, Ready, In progress, In review, Done).

## Writing one
1. Copy `TEMPLATE.md` to `specs/<short-name>.md` and fill it in. Rough is fine: put what you're unsure of under
   **Open questions**.
2. Register it: `python TreeEngine/Tools/specs/specs.py sync` (or tell Claude "sync specs"). Every spec without an
   `issue:` in its header gets a GitHub issue (labelled `spec`, linking the file, showing the Goal), is added to the
   board in **Backlog**, and gets the issue number written into its header.
3. Prioritize on the board: drag specs into **Ready** in the order you want them; the top of Ready is next. Backlog is
   for specs still being written.
4. Edit freely until the spec moves to **In progress**. After that, comment on its issue (or edit the file and say so),
   so the change isn't missed.

## How Claude picks them up
- After each merge (or when you say "next spec"), Claude syncs, takes the top **Ready** spec, moves it to
  **In progress**, and says which one it started.
- Questions that block the work are asked (and recorded on the issue); the rest are decided and noted in the PR.
- The work goes on `feature/<issue>-<short-name>` branches. A large spec becomes several PRs ("Part of #N"), the last
  one "Closes #N". An open PR moves the item to **In review**; merging the closing PR closes the issue and the board
  moves it to **Done**.
- If Ready is empty, Claude says so and waits.

## The tool
`python TreeEngine/Tools/specs/specs.py <command>` (needs the GitHub CLI, logged in with the `project` scope:
`gh auth refresh -s project`):
- `sync`: issues and board items for new specs, issue numbers written back; closed issues moved to Done.
- `queue`: the board's Ready items in order, then Backlog.
- `next`: the top Ready spec.
- `status N "In progress"`: move issue N's item to a column (Backlog, Ready, In progress, In review, Done).
