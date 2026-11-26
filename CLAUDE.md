# Working in this repository

Orientation: `ARCHITECTURE.md` (overview), `.github/copilot-instructions.md` (build details), and the READMEs under
`TreeEngine/Tests/RenderRegression`, `TreeEngine/Tests/Performance`, `TreeEngine/Game/Resources/Scenes` and
`TreeEngine/Tools/terrain`.

## Branches and pull requests

- `master` is the trunk. Never commit or push to it directly.
- Start every task on a new branch from an up-to-date `origin/master` (`git fetch`, then
  `git checkout -b <type>/<name> origin/master`), with `<type>` one of `feature`, `fix`, `perf`, `docs`, `tools`.
- Keep pull requests small enough to review. A multi-step plan becomes a sequence of PRs, each building and passing
  the tests on its own, not one long-lived branch.
- Open PRs with the GitHub CLI (`gh pr create --base master`). The description says what changed and why, how it was
  tested (below), known failures, regenerated golds and why they changed, and risks.
- Answer review comments with new commits on the branch; don't force-push unless asked.
- The owner reviews and squash-merges on GitHub. Afterwards: `git checkout master`, `git pull --ff-only`, and delete
  the local branch.

## Specs and the queue

- Feature specs live in `specs/` (see `specs/README.md`); each has a GitHub issue labelled `spec` on the
  "TreeEngine roadmap" project board. `TreeEngine/Tools/specs/specs.py` syncs, lists and moves them.
- After each merge (or when asked for the next spec), sync, take the top **Ready** item, move it to **In progress**,
  and say which spec was started. If Ready is empty, say so and wait.
- Branch `feature/<issue>-<short-name>`. A large spec becomes several PRs ("Part of #N"), the last "Closes #N"; move
  the item to **In review** when its PR opens. Ask about open questions that block the work (and record the answer on
  the issue); decide the rest and note them in the PR.

## Before opening a PR

- Build `TreeEngine/TreeEngine.sln` in `Debug|x64` and `Release|x64`.
- Run `TreeEngine/Tests/bin/x64/Debug/SceneDefinitionTests.exe` (the Debug solution build builds it).
- Run the render suite, `TreeEngine/Tests/RenderRegression/run-render-tests.ps1`, which uses the Debug build, plus the
  targeted `test-*.ps1` scripts for the area changed (listed in that folder's README).
- Measure with `TreeEngine/Tests/Performance/run-performance-tests.ps1` when the change is about performance, and
  commit the run it records. GPU timings drift between sessions, so compare builds run back to back.
- `test-scene-persistence.ps1` fails on `LoadSimpleBox` raster (RMS about 0.002) and has since well before these rules;
  mention it rather than fixing it in an unrelated PR.

## Conventions

- Scene files and resources under `TreeEngine/Game/Resources` are copied next to the executable when the game builds,
  so a new or changed scene needs a rebuild before tests see it.
- Binary assets (PNG, DDS, GLB, terrain packages) are Git LFS; see `.gitattributes`.
- Put a PIX marker on every new GPU pass.
