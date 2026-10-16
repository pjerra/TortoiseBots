# Agentic guidelines

AI agents (Claude Code, Codex, OpenCode and the like) are welcome here. A change
made with one meets the same bar as any other, and the person who opens the
pull request answers for it.

Agents working in this repository also follow [`AGENTS.md`](AGENTS.md); people
follow [`CONTRIBUTING.md`](CONTRIBUTING.md).

## Ground rules

1. **Own the code.** Read and understand every line before you open the pull
   request. In review you answer the questions, not the agent.
2. **Test for real.** Compiling is not testing. Run it, and check the
   behaviour in game where it matters. Say what you did not test.
3. **Say that an agent helped.** Tick the box in the pull request template.
4. **Write the issue and the description yourself.** Use the agent's draft as
   a start, then cut it down to what a reader needs. No pasted walls of
   generated text.
5. **Respect the reviewer's time.** A pull request that looks like unread
   agent output is closed without a detailed review.

## Workflow

Each step ends with a short text you read and correct before the next one.
Start every step in a fresh agent session: a focused context gives better
results than one long conversation.

1. **Issue (the "what").** Have the agent ask you questions until the issue
   says what the feature does, how a player turns it on, its default, and what
   it does not cover. This is the contract for the change.
2. **Plan (the "how").** The agent reads the issue and the code and writes a
   plan: which files change, which config key, which tests. Fix the plan
   before any code is written.
3. **Build.** The agent works through the plan. Keep the change to what the
   issue asks; nothing extra.
4. **Self-review.** Before the pull request, a fresh session with a strong
   model reviews the whole diff against the issue. Fix what you agree with.
5. **Pull request.** Fill in the template (see below), read the final diff
   once more, then open it against `dev` or the feature branch.

A feature that needs several rounds of this lives on its own feature branch
until it is complete. Only complete features go to `dev`.

## The pull request description

It is read by a person who has not seen your code, often the owner deciding
what goes into a release. It must be short and clear:

- **Issue:** which issue it closes, or which bug it fixes.
- **Added / Changed / Removed:** one line per item, in player or operator
  words. "Warlocks soulstone the group healer out of combat", not
  "add UseSoulstoneHealerAction to GenericWarlockNonCombatStrategy".
- **How to use it:** the command or config key, and its default.
- **Testing:** what was built and checked, what was not.

Example:

```markdown
## Issue
Closes #936

## What this does
Added:
- `.bot hire <class> ... level=<n>` hires a companion at a chosen level.

Changed:
- A hire costs the fee for the chosen level, not the hirer's level.

## How to use it
`.bot hire mage level=20`. Players can hire up to their own level;
`AiPlayerbot.HireMaxLevelAboveHirer` (default 0) allows more.

## Testing
Module build and `tools/verify_all.sh`; hired a level 20 mage from a level 40
character in game. Not tested: a GM hiring at level 60.
```
