---
id: server-settings-hiring
title: Mercenary Hiring & Recruiter Settings
category: server
summary: Hired companions, their fees, level, cap and PvP rules, and the AiPlayerbot.Hire settings that control them.
tags: [config, hiring, mercenaries]
relates_to:
  - server-bot-pool
  - play-commands-roster-and-hiring
  - server-settings-party
---

# Mercenary Hiring & Recruiter Settings

A player hires a temporary companion at a `<Mercenary Hire>` recruiter in an inn. The companion is a new character on a hire account, levelled and geared to the request, and it joins the hirer's party. When the hire ends — the owner logs it out, the hirer leaves the party, the disconnect grace expires, or a battleground is entered — the character is deleted, so a hire never becomes a permanent bot.

The fast path, `.bot hire`, works anywhere only when `AiPlayerbot.HireAnywhere = 1`; by default the chat command points the player at a recruiter. See [Roster and character commands](../../play/commands/roster-and-hiring.md#hiring-a-companion) for the words.

## Settings

Every key is `AiPlayerbot.*` in `aiplayerbot.conf`. The shipped file lists all of them with their defaults.

| Key | Default | What it does |
| :--- | :--- | :--- |
| `HireEnabled` | `1` | Master switch. `0` refuses every hire and every recruiter. |
| `HireMinAccountSecurity` | `0` | Minimum account security that may hire (0 = every player). |
| `HireMaxBotsPerPlayer` | `4` | Live companions per player. |
| `HireMaxLevelAboveHirer` | `0` | How far above the hirer a requested `level=<n>` may go (0 = never above). A GM is not limited by it. |
| `HireFreeMinSecurity` | `0` | Accounts at this security or above hire for free (0 = off). |
| `HireAnywhere` | `0` | `1` lets `.bot hire` work away from a recruiter. |
| `HireAllowPvP` | `0` | `1` lets companions enter battlegrounds; `0` makes queueing a warning and entering one end the hire. |
| `HireCrossFactionNeutralInns` | `0` | `1` lets neutral-inn recruiters hire the other faction (also needs the core's two-side group interaction). |
| `HireBaseCostCopper` | `15000` | Fee for the first companion at level 60, in copper (1.5g). |
| `HirePartyMult2/3/4` | `1.66 / 2.66 / 4.66` | Fee multipliers for the 2nd, 3rd and 4th live companion. |
| `HireDisconnectGracePeriod` | `300` | Seconds a companion waits while its hirer is offline before it is dismissed. |
| `HireGearTiersEnabled` | `1` | Offers the contract gear tiers at level 60. |
| `HireGearStartTier` | `0` | Tier every player starts with, before the unlock contracts (`0`..`7`). |
| `HireGearTierCostMultipliers` | `1,1.5,2,3,4,5,6,6` | Fee multiplier per gear tier 0..7. |
| `HireRaidFlatCostCopper` | `10000` | Flat fee per companion from the 5th onward; raid hiring itself is not available yet. |

The fee is charged only after the character exists. A hire that fails later (the login, the party join) is undone and the fee returned, by mail if the hirer has already logged out.

## Not on ForeverCore Yet

- Raid hiring (`.bot hire raid`, whole-composition hires) is a later issue.
- The recruiter gear-tier contracts and their unlock quests, and the capital recruiters' world-buff purchases, are separate pieces of the hire work.
