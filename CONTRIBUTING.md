# Contributing to TortoiseBots

Thanks for helping make PlayerBots better for Tortoise WoW. Bug reports,
feature ideas, documentation improvements, testing, and code contributions are
all welcome.

## How work flows

```text
issue  ->  your branch  ->  PR into dev (owner approves)  ->  dev into main (owner, release)
```

1. **Every change has an issue.** Before a pull request, there is a
   **Feature spec** issue (what it does, how a player turns it on, its
   default, what it does not cover) or a **Bug report**. A **Feature request**
   is an idea; it becomes work once someone writes the spec for it. Search the
   existing issues first. A pull request without an issue is not merged.
2. **Work on your own branch**, named after what it is:

   | Branch | For |
   | :--- | :--- |
   | `feat/<topic>` | a new feature or improvement, e.g. `feat/hire-level` |
   | `fix/<topic>` | a bug fix, e.g. `fix/wand-on-fresh-mob` |
   | `docs/<topic>` | documentation only |

   Collaborators with write access push these branches to this repository;
   everybody else uses the same names on a fork. Never push to `dev` or
   `main` directly.
3. **Only finished features go to `dev`.** A pull request into `dev` brings a
   whole, working feature or a whole fix, never a first part of one. A feature
   that takes several pull requests lives on its own feature branch (for
   example `feat/dungeon-self-clear`) and the parts merge there; it comes to
   `dev` in one pull request once it is complete.
4. **The owner approves every merge into `dev`.** `dev` is where finished work
   waits for the next release; only the owner merges `dev` into `main` and
   makes releases.
5. **Big new features are opt-in.** A big new feature or system (a new mode,
   a new way of controlling bots) ships behind a config key that is off by
   default. Fixes and smaller behaviour improvements do not need one.
6. **Test it and say how.** Build, run, and check in game where it matters.
   Say clearly what you did not test.

Small fixes and documentation changes still need their issue, but the issue
can be short.

Using an AI agent? Read [`AGENTIC_GUIDELINES.md`](AGENTIC_GUIDELINES.md).

## Current core target

TortoiseBots targets the `main` branch of
[`tortoise-wow`](https://github.com/tortoise-wow/tortoise-wow). Build and test
integration changes against `main`.

Pull requests for this repository target TortoiseBots' `dev` branch (or a
feature branch, see above), never `main`.

## Project guidelines

- Keep TortoiseBots optional: the core must still build without the module.
- Prefer module-only changes. If a core change is necessary, explain the
  generic host capability it provides and why an existing seam is insufficient.
- Do not reintroduce direct bot coupling such as `WorldSession::GetBot()` or
  `m_bot` into normal core gameplay code.
- Keep SQL migrations in the appropriate `data/sql/world` or `data/sql/character`
  directory, use a timestamped filename, and comment non-obvious statements.
- If behavior is adapted from another project, record the source and commit in
  [`docs/PROVENANCE.md`](docs/PROVENANCE.md).

## Pull requests

The description is for a person who has not seen your code. Keep it short and
plain, and fill in the template:

- **Issue:** the issue it belongs to (`Closes #123`) or the bug it fixes
  (`Fixes #123`). Required.
- **Added / Changed / Removed:** one line per item, in player or operator
  words ("Warlocks soulstone the healer"), not code words. Leave out a list
  that is empty.
- **How to use it:** the command or config key, and its default.
- **Testing:** what you built and checked, and what you did not.

If a full build or runtime test is not available, say so clearly rather than
leaving the reviewer to guess. A description that does not say what changes
will be sent back.

## Licence of contributions

TortoiseBots is licensed under **AGPL-3.0-or-later** (see [`LICENSE`](LICENSE)
and [`LICENCE.md`](LICENCE.md)). By opening a pull request or otherwise
submitting a contribution, you agree that:

- your contribution is licensed under AGPL-3.0-or-later, the same terms as the
  rest of the project;
- you wrote it yourself, or you have the right to submit it under these terms;
- code copied or ported from another project (porting is welcome) keeps its original copyright and licence notice, is
  compatible with AGPL-3.0, and is recorded in
  [`docs/PROVENANCE.md`](docs/PROVENANCE.md).

Do not submit code whose licence forbids this (proprietary, "non-commercial
only", GPL-2.0-only without an "or later" grant, or no licence at all).
