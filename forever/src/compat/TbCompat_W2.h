// ForeverCore side of the both-core helpers of wide pass 2 (branch port/wide-pass-2). Owned by that pass: no other package edits
// this file. Turtle side: ai/cmangos-compat-w2.h (same names and signatures). Both are found through ai/playerbot/TbCompatW2.h
// (__has_include), so the two shared shim headers stay untouched. Notes: forever/docs/WIDE-PASS-R2-NOTES.md.
#pragma once

// =====================================================================================================================
// Family 1: names. Turtle's name for a constant ForeverCore spells another way, same number, same meaning. The macros are only
// seen by the files that include the wrapper header. Every row is verified by forever/tools/w2_names.py against both cores.
// =====================================================================================================================
// BEGIN GENERATED NAMES (forever/tools/w2_names.py --gen)
// TEXTEMOTE_*: Turtle TextEmotes (SharedDefines.h:394+) -> ForeverCore TextEmotes (SharedDefines.h:3299+), same name with
// TEXT_EMOTE_ and the same number, checked row by row by the script. 167 names.
#define TEXTEMOTE_AGREE                TEXT_EMOTE_AGREE                   // 1  T:396 F:3301
#define TEXTEMOTE_AMAZE                TEXT_EMOTE_AMAZE                   // 2  T:397 F:3302
#define TEXTEMOTE_ANGRY                TEXT_EMOTE_ANGRY                   // 3  T:398 F:3303
#define TEXTEMOTE_APOLOGIZE            TEXT_EMOTE_APOLOGIZE               // 4  T:399 F:3304
#define TEXTEMOTE_APPLAUD              TEXT_EMOTE_APPLAUD                 // 5  T:400 F:3305
#define TEXTEMOTE_BASHFUL              TEXT_EMOTE_BASHFUL                 // 6  T:401 F:3306
#define TEXTEMOTE_BECKON               TEXT_EMOTE_BECKON                  // 7  T:402 F:3307
#define TEXTEMOTE_BEG                  TEXT_EMOTE_BEG                     // 8  T:403 F:3308
#define TEXTEMOTE_BITE                 TEXT_EMOTE_BITE                    // 9  T:404 F:3309
#define TEXTEMOTE_BLEED                TEXT_EMOTE_BLEED                   // 10  T:405 F:3310
#define TEXTEMOTE_BLINK                TEXT_EMOTE_BLINK                   // 11  T:406 F:3311
#define TEXTEMOTE_BLUSH                TEXT_EMOTE_BLUSH                   // 12  T:407 F:3312
#define TEXTEMOTE_BONK                 TEXT_EMOTE_BONK                    // 13  T:408 F:3313
#define TEXTEMOTE_BORED                TEXT_EMOTE_BORED                   // 14  T:409 F:3314
#define TEXTEMOTE_BOUNCE               TEXT_EMOTE_BOUNCE                  // 15  T:410 F:3315
#define TEXTEMOTE_BRB                  TEXT_EMOTE_BRB                     // 16  T:411 F:3316
#define TEXTEMOTE_BOW                  TEXT_EMOTE_BOW                     // 17  T:412 F:3317
#define TEXTEMOTE_BURP                 TEXT_EMOTE_BURP                    // 18  T:413 F:3318
#define TEXTEMOTE_BYE                  TEXT_EMOTE_BYE                     // 19  T:414 F:3319
#define TEXTEMOTE_CACKLE               TEXT_EMOTE_CACKLE                  // 20  T:415 F:3320
#define TEXTEMOTE_CHICKEN              TEXT_EMOTE_CHICKEN                 // 22  T:417 F:3322
#define TEXTEMOTE_CHUCKLE              TEXT_EMOTE_CHUCKLE                 // 23  T:418 F:3323
#define TEXTEMOTE_CLAP                 TEXT_EMOTE_CLAP                    // 24  T:419 F:3324
#define TEXTEMOTE_CONFUSED             TEXT_EMOTE_CONFUSED                // 25  T:420 F:3325
#define TEXTEMOTE_CONGRATULATE         TEXT_EMOTE_CONGRATULATE            // 26  T:421 F:3326
#define TEXTEMOTE_COUGH                TEXT_EMOTE_COUGH                   // 27  T:422 F:3327
#define TEXTEMOTE_COWER                TEXT_EMOTE_COWER                   // 28  T:423 F:3328
#define TEXTEMOTE_CRACK                TEXT_EMOTE_CRACK                   // 29  T:424 F:3329
#define TEXTEMOTE_CRINGE               TEXT_EMOTE_CRINGE                  // 30  T:425 F:3330
#define TEXTEMOTE_CRY                  TEXT_EMOTE_CRY                     // 31  T:426 F:3331
#define TEXTEMOTE_CURIOUS              TEXT_EMOTE_CURIOUS                 // 32  T:427 F:3332
#define TEXTEMOTE_CURTSEY              TEXT_EMOTE_CURTSEY                 // 33  T:428 F:3333
#define TEXTEMOTE_DANCE                TEXT_EMOTE_DANCE                   // 34  T:429 F:3334
#define TEXTEMOTE_DRINK                TEXT_EMOTE_DRINK                   // 35  T:430 F:3335
#define TEXTEMOTE_DROOL                TEXT_EMOTE_DROOL                   // 36  T:431 F:3336
#define TEXTEMOTE_EAT                  TEXT_EMOTE_EAT                     // 37  T:432 F:3337
#define TEXTEMOTE_EYE                  TEXT_EMOTE_EYE                     // 38  T:433 F:3338
#define TEXTEMOTE_FART                 TEXT_EMOTE_FART                    // 39  T:434 F:3339
#define TEXTEMOTE_FIDGET               TEXT_EMOTE_FIDGET                  // 40  T:435 F:3340
#define TEXTEMOTE_FLEX                 TEXT_EMOTE_FLEX                    // 41  T:436 F:3341
#define TEXTEMOTE_FROWN                TEXT_EMOTE_FROWN                   // 42  T:437 F:3342
#define TEXTEMOTE_GASP                 TEXT_EMOTE_GASP                    // 43  T:438 F:3343
#define TEXTEMOTE_GAZE                 TEXT_EMOTE_GAZE                    // 44  T:439 F:3344
#define TEXTEMOTE_GIGGLE               TEXT_EMOTE_GIGGLE                  // 45  T:440 F:3345
#define TEXTEMOTE_GLARE                TEXT_EMOTE_GLARE                   // 46  T:441 F:3346
#define TEXTEMOTE_GLOAT                TEXT_EMOTE_GLOAT                   // 47  T:442 F:3347
#define TEXTEMOTE_GREET                TEXT_EMOTE_GREET                   // 48  T:443 F:3348
#define TEXTEMOTE_GRIN                 TEXT_EMOTE_GRIN                    // 49  T:444 F:3349
#define TEXTEMOTE_GROAN                TEXT_EMOTE_GROAN                   // 50  T:445 F:3350
#define TEXTEMOTE_GROVEL               TEXT_EMOTE_GROVEL                  // 51  T:446 F:3351
#define TEXTEMOTE_GUFFAW               TEXT_EMOTE_GUFFAW                  // 52  T:447 F:3352
#define TEXTEMOTE_HAIL                 TEXT_EMOTE_HAIL                    // 53  T:448 F:3353
#define TEXTEMOTE_HAPPY                TEXT_EMOTE_HAPPY                   // 54  T:449 F:3354
#define TEXTEMOTE_HELLO                TEXT_EMOTE_HELLO                   // 55  T:450 F:3355
#define TEXTEMOTE_HUG                  TEXT_EMOTE_HUG                     // 56  T:451 F:3356
#define TEXTEMOTE_HUNGRY               TEXT_EMOTE_HUNGRY                  // 57  T:452 F:3357
#define TEXTEMOTE_KISS                 TEXT_EMOTE_KISS                    // 58  T:453 F:3358
#define TEXTEMOTE_KNEEL                TEXT_EMOTE_KNEEL                   // 59  T:454 F:3359
#define TEXTEMOTE_LAUGH                TEXT_EMOTE_LAUGH                   // 60  T:455 F:3360
#define TEXTEMOTE_LAYDOWN              TEXT_EMOTE_LAYDOWN                 // 61  T:456 F:3361
#define TEXTEMOTE_MESSAGE              TEXT_EMOTE_MESSAGE                 // 62  T:457 F:3362
#define TEXTEMOTE_MOAN                 TEXT_EMOTE_MOAN                    // 63  T:458 F:3363
#define TEXTEMOTE_MOON                 TEXT_EMOTE_MOON                    // 64  T:459 F:3364
#define TEXTEMOTE_MOURN                TEXT_EMOTE_MOURN                   // 65  T:460 F:3365
#define TEXTEMOTE_NOSEPICK             TEXT_EMOTE_NOSEPICK                // 68  T:463 F:3368
#define TEXTEMOTE_PANIC                TEXT_EMOTE_PANIC                   // 69  T:464 F:3369
#define TEXTEMOTE_PEER                 TEXT_EMOTE_PEER                    // 70  T:465 F:3370
#define TEXTEMOTE_PLEAD                TEXT_EMOTE_PLEAD                   // 71  T:466 F:3371
#define TEXTEMOTE_POINT                TEXT_EMOTE_POINT                   // 72  T:467 F:3372
#define TEXTEMOTE_POKE                 TEXT_EMOTE_POKE                    // 73  T:468 F:3373
#define TEXTEMOTE_PRAY                 TEXT_EMOTE_PRAY                    // 74  T:469 F:3374
#define TEXTEMOTE_ROAR                 TEXT_EMOTE_ROAR                    // 75  T:470 F:3375
#define TEXTEMOTE_ROFL                 TEXT_EMOTE_ROFL                    // 76  T:471 F:3376
#define TEXTEMOTE_RUDE                 TEXT_EMOTE_RUDE                    // 77  T:472 F:3377
#define TEXTEMOTE_SALUTE               TEXT_EMOTE_SALUTE                  // 78  T:473 F:3378
#define TEXTEMOTE_SCRATCH              TEXT_EMOTE_SCRATCH                 // 79  T:474 F:3379
#define TEXTEMOTE_SEXY                 TEXT_EMOTE_SEXY                    // 80  T:475 F:3380
#define TEXTEMOTE_SHAKE                TEXT_EMOTE_SHAKE                   // 81  T:476 F:3381
#define TEXTEMOTE_SHOUT                TEXT_EMOTE_SHOUT                   // 82  T:477 F:3382
#define TEXTEMOTE_SHRUG                TEXT_EMOTE_SHRUG                   // 83  T:478 F:3383
#define TEXTEMOTE_SHY                  TEXT_EMOTE_SHY                     // 84  T:479 F:3384
#define TEXTEMOTE_SIGH                 TEXT_EMOTE_SIGH                    // 85  T:480 F:3385
#define TEXTEMOTE_SIT                  TEXT_EMOTE_SIT                     // 86  T:481 F:3386
#define TEXTEMOTE_SLEEP                TEXT_EMOTE_SLEEP                   // 87  T:482 F:3387
#define TEXTEMOTE_SNARL                TEXT_EMOTE_SNARL                   // 88  T:483 F:3388
#define TEXTEMOTE_SPIT                 TEXT_EMOTE_SPIT                    // 89  T:484 F:3389
#define TEXTEMOTE_STARE                TEXT_EMOTE_STARE                   // 90  T:485 F:3390
#define TEXTEMOTE_SURPRISED            TEXT_EMOTE_SURPRISED               // 91  T:486 F:3391
#define TEXTEMOTE_SURRENDER            TEXT_EMOTE_SURRENDER               // 92  T:487 F:3392
#define TEXTEMOTE_TALK                 TEXT_EMOTE_TALK                    // 93  T:488 F:3393
#define TEXTEMOTE_TALKEX               TEXT_EMOTE_TALKEX                  // 94  T:489 F:3394
#define TEXTEMOTE_TALKQ                TEXT_EMOTE_TALKQ                   // 95  T:490 F:3395
#define TEXTEMOTE_TAP                  TEXT_EMOTE_TAP                     // 96  T:491 F:3396
#define TEXTEMOTE_THANK                TEXT_EMOTE_THANK                   // 97  T:492 F:3397
#define TEXTEMOTE_THREATEN             TEXT_EMOTE_THREATEN                // 98  T:493 F:3398
#define TEXTEMOTE_TIRED                TEXT_EMOTE_TIRED                   // 99  T:494 F:3399
#define TEXTEMOTE_VICTORY              TEXT_EMOTE_VICTORY                 // 100  T:495 F:3400
#define TEXTEMOTE_WAVE                 TEXT_EMOTE_WAVE                    // 101  T:496 F:3401
#define TEXTEMOTE_WELCOME              TEXT_EMOTE_WELCOME                 // 102  T:497 F:3402
#define TEXTEMOTE_WHINE                TEXT_EMOTE_WHINE                   // 103  T:498 F:3403
#define TEXTEMOTE_WHISTLE              TEXT_EMOTE_WHISTLE                 // 104  T:499 F:3404
#define TEXTEMOTE_WORK                 TEXT_EMOTE_WORK                    // 105  T:500 F:3405
#define TEXTEMOTE_YAWN                 TEXT_EMOTE_YAWN                    // 106  T:501 F:3406
#define TEXTEMOTE_BOGGLE               TEXT_EMOTE_BOGGLE                  // 107  T:502 F:3407
#define TEXTEMOTE_CALM                 TEXT_EMOTE_CALM                    // 108  T:503 F:3408
#define TEXTEMOTE_COLD                 TEXT_EMOTE_COLD                    // 109  T:504 F:3409
#define TEXTEMOTE_COMFORT              TEXT_EMOTE_COMFORT                 // 110  T:505 F:3410
#define TEXTEMOTE_CUDDLE               TEXT_EMOTE_CUDDLE                  // 111  T:506 F:3411
#define TEXTEMOTE_DUCK                 TEXT_EMOTE_DUCK                    // 112  T:507 F:3412
#define TEXTEMOTE_INSULT               TEXT_EMOTE_INSULT                  // 113  T:508 F:3413
#define TEXTEMOTE_INTRODUCE            TEXT_EMOTE_INTRODUCE               // 114  T:509 F:3414
#define TEXTEMOTE_JK                   TEXT_EMOTE_JK                      // 115  T:510 F:3415
#define TEXTEMOTE_LICK                 TEXT_EMOTE_LICK                    // 116  T:511 F:3416
#define TEXTEMOTE_LISTEN               TEXT_EMOTE_LISTEN                  // 117  T:512 F:3417
#define TEXTEMOTE_LOST                 TEXT_EMOTE_LOST                    // 118  T:513 F:3418
#define TEXTEMOTE_MOCK                 TEXT_EMOTE_MOCK                    // 119  T:514 F:3419
#define TEXTEMOTE_PONDER               TEXT_EMOTE_PONDER                  // 120  T:515 F:3420
#define TEXTEMOTE_POUNCE               TEXT_EMOTE_POUNCE                  // 121  T:516 F:3421
#define TEXTEMOTE_PRAISE               TEXT_EMOTE_PRAISE                  // 122  T:517 F:3422
#define TEXTEMOTE_PURR                 TEXT_EMOTE_PURR                    // 123  T:518 F:3423
#define TEXTEMOTE_PUZZLE               TEXT_EMOTE_PUZZLE                  // 124  T:519 F:3424
#define TEXTEMOTE_RAISE                TEXT_EMOTE_RAISE                   // 125  T:520 F:3425
#define TEXTEMOTE_READY                TEXT_EMOTE_READY                   // 126  T:521 F:3426
#define TEXTEMOTE_SHIMMY               TEXT_EMOTE_SHIMMY                  // 127  T:522 F:3427
#define TEXTEMOTE_SHIVER               TEXT_EMOTE_SHIVER                  // 128  T:523 F:3428
#define TEXTEMOTE_SHOO                 TEXT_EMOTE_SHOO                    // 129  T:524 F:3429
#define TEXTEMOTE_SLAP                 TEXT_EMOTE_SLAP                    // 130  T:525 F:3430
#define TEXTEMOTE_SMIRK                TEXT_EMOTE_SMIRK                   // 131  T:526 F:3431
#define TEXTEMOTE_SNIFF                TEXT_EMOTE_SNIFF                   // 132  T:527 F:3432
#define TEXTEMOTE_SNUB                 TEXT_EMOTE_SNUB                    // 133  T:528 F:3433
#define TEXTEMOTE_SOOTHE               TEXT_EMOTE_SOOTHE                  // 134  T:529 F:3434
#define TEXTEMOTE_STINK                TEXT_EMOTE_STINK                   // 135  T:530 F:3435
#define TEXTEMOTE_TAUNT                TEXT_EMOTE_TAUNT                   // 136  T:531 F:3436
#define TEXTEMOTE_TEASE                TEXT_EMOTE_TEASE                   // 137  T:532 F:3437
#define TEXTEMOTE_THIRSTY              TEXT_EMOTE_THIRSTY                 // 138  T:533 F:3438
#define TEXTEMOTE_VETO                 TEXT_EMOTE_VETO                    // 139  T:534 F:3439
#define TEXTEMOTE_SNICKER              TEXT_EMOTE_SNICKER                 // 140  T:535 F:3440
#define TEXTEMOTE_STAND                TEXT_EMOTE_STAND                   // 141  T:536 F:3441
#define TEXTEMOTE_TICKLE               TEXT_EMOTE_TICKLE                  // 142  T:537 F:3442
#define TEXTEMOTE_VIOLIN               TEXT_EMOTE_VIOLIN                  // 143  T:538 F:3443
#define TEXTEMOTE_SMILE                TEXT_EMOTE_SMILE                   // 163  T:539 F:3444
#define TEXTEMOTE_RASP                 TEXT_EMOTE_RASP                    // 183  T:540 F:3445
#define TEXTEMOTE_PITY                 TEXT_EMOTE_PITY                    // 203  T:541 F:3446
#define TEXTEMOTE_GROWL                TEXT_EMOTE_GROWL                   // 204  T:542 F:3447
#define TEXTEMOTE_BARK                 TEXT_EMOTE_BARK                    // 205  T:543 F:3448
#define TEXTEMOTE_SCARED               TEXT_EMOTE_SCARED                  // 223  T:544 F:3449
#define TEXTEMOTE_FLOP                 TEXT_EMOTE_FLOP                    // 224  T:545 F:3450
#define TEXTEMOTE_LOVE                 TEXT_EMOTE_LOVE                    // 225  T:546 F:3451
#define TEXTEMOTE_MOO                  TEXT_EMOTE_MOO                     // 226  T:547 F:3452
#define TEXTEMOTE_COMMEND              TEXT_EMOTE_COMMEND                 // 243  T:551 F:3453
#define TEXTEMOTE_FLIRT                TEXT_EMOTE_FLIRT                   // 328  T:549 F:3465
#define TEXTEMOTE_JOKE                 TEXT_EMOTE_JOKE                    // 329  T:550 F:3466
#define TEXTEMOTE_WINK                 TEXT_EMOTE_WINK                    // 363  T:552 F:3468
#define TEXTEMOTE_PAT                  TEXT_EMOTE_PAT                     // 364  T:553 F:3469
#define TEXTEMOTE_SERIOUS              TEXT_EMOTE_SERIOUS                 // 365  T:554 F:3470
#define TEXTEMOTE_GOODLUCK             TEXT_EMOTE_GOODLUCK                // 367  T:556 F:3472
#define TEXTEMOTE_BLAME                TEXT_EMOTE_BLAME                   // 368  T:557 F:3473
#define TEXTEMOTE_BLANK                TEXT_EMOTE_BLANK                   // 369  T:558 F:3474
#define TEXTEMOTE_BRANDISH             TEXT_EMOTE_BRANDISH                // 370  T:559 F:3475
#define TEXTEMOTE_BREATH               TEXT_EMOTE_BREATH                  // 371  T:560 F:3476
#define TEXTEMOTE_DISAGREE             TEXT_EMOTE_DISAGREE                // 372  T:561 F:3477
#define TEXTEMOTE_DOUBT                TEXT_EMOTE_DOUBT                   // 373  T:562 F:3478
#define TEXTEMOTE_EMBARRASS            TEXT_EMOTE_EMBARRASS               // 374  T:563 F:3479
#define TEXTEMOTE_ENCOURAGE            TEXT_EMOTE_ENCOURAGE               // 375  T:564 F:3480
#define TEXTEMOTE_ENEMY                TEXT_EMOTE_ENEMY                   // 376  T:565 F:3481
#define TEXTEMOTE_EYEBROW              TEXT_EMOTE_EYEBROW                 // 377  T:566 F:3482
#define TEXTEMOTE_TOAST                TEXT_EMOTE_TOAST                   // 378  T:567 F:3483

// Other names: same meaning, same number in both cores (table with file:line in forever/docs/WIDE-PASS-R2-NOTES.md).
#define ITEM_CLASS_JUNK                              ITEM_CLASS_MISCELLANEOUS
#define TRADE_STATUS_BEGIN_TRADE                     TRADE_STATUS_PROPOSED
#define TRADE_STATUS_OPEN_WINDOW                     TRADE_STATUS_INITIATED
#define TRADE_STATUS_TRADE_CANCELED                  TRADE_STATUS_CANCELLED
#define TRADE_STATUS_TRADE_ACCEPT                    TRADE_STATUS_ACCEPTED
#define BIND_QUEST_ITEM                              BIND_QUEST
#define BIND_WHEN_USE                                BIND_ON_USE
#define BIND_WHEN_PICKED_UP                          BIND_ON_ACQUIRE
#define BIND_WHEN_EQUIPPED                           BIND_ON_EQUIP
#define SKILL_2H_MACES                               SKILL_TWO_HANDED_MACES
#define SKILL_2H_AXES                                SKILL_TWO_HANDED_AXES
#define SKILL_2H_SWORDS                              SKILL_TWO_HANDED_SWORDS
#define FORM_BEAR                                    FORM_BEAR_FORM
#define FORM_CAT                                     FORM_CAT_FORM
#define FORM_MOONKIN                                 FORM_MOONKIN_FORM
#define FORM_DIREBEAR                                FORM_DIRE_BEAR_FORM
#define FORM_DEFENSIVESTANCE                         FORM_DEFENSIVE_STANCE
#define GAMEOBJECT_TYPE_SUMMONING_RITUAL             GAMEOBJECT_TYPE_RITUAL
#define GAMEOBJECT_TYPE_MO_TRANSPORT                 GAMEOBJECT_TYPE_MAP_OBJ_TRANSPORT
#define QUEST_TYPE_RAID                              QUEST_INFO_RAID
#define QUEST_TYPE_DUNGEON                           QUEST_INFO_DUNGEON
#define QUEST_TYPE_PVP                               QUEST_INFO_PVP
#define UNIT_FLAG_IMMUNE_TO_PLAYER                   UNIT_FLAG_IMMUNE_TO_PC
#define CHAT_TAG_NONE                                CHAT_FLAG_NONE
#define TARGET_ICON_COUNT                            TARGET_ICONS_COUNT
#define CAST_FLAG_AMMO                               CAST_FLAG_PROJECTILE
#define EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT             EQUIP_ERR_WRONG_SLOT
#define EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG        EQUIP_ERR_BAG_IN_BAG
#define EQUIP_ERR_CANT_TRADE_EQUIP_BAGS              EQUIP_ERR_TRADE_EQUIPPED_BAG
#define EQUIP_ERR_ONLY_AMMO_CAN_GO_HERE              EQUIP_ERR_AMMO_ONLY
#define EQUIP_ERR_NO_REQUIRED_PROFICIENCY            EQUIP_ERR_PROFICIENCY_NEEDED
#define EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE        EQUIP_ERR_NO_SLOT_AVAILABLE
#define EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM        EQUIP_ERR_CANT_EQUIP_EVER
#define EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM2       EQUIP_ERR_CANT_EQUIP_EVER_2
#define EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE2       EQUIP_ERR_NO_SLOT_AVAILABLE_2
#define EQUIP_ERR_CANT_EQUIP_WITH_TWOHANDED          EQUIP_ERR_2HANDED_EQUIPPED
#define EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG            EQUIP_ERR_WRONG_BAG_TYPE
#define EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG2           EQUIP_ERR_WRONG_BAG_TYPE_2
#define EQUIP_ERR_CANT_CARRY_MORE_OF_THIS            EQUIP_ERR_ITEM_MAX_COUNT
#define EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE3       EQUIP_ERR_NO_SLOT_AVAILABLE_3
#define EQUIP_ERR_ITEM_CANT_STACK                    EQUIP_ERR_CANT_STACK
#define EQUIP_ERR_ITEM_CANT_BE_EQUIPPED              EQUIP_ERR_NOT_EQUIPPABLE
#define EQUIP_ERR_ITEMS_CANT_BE_SWAPPED              EQUIP_ERR_CANT_SWAP
#define EQUIP_ERR_SLOT_IS_EMPTY                      EQUIP_ERR_SLOT_EMPTY
#define EQUIP_ERR_CANT_DROP_SOULBOUND                EQUIP_ERR_DROP_BOUND_ITEM
#define EQUIP_ERR_TRIED_TO_SPLIT_MORE_THAN_COUNT     EQUIP_ERR_TOO_FEW_TO_SPLIT
#define EQUIP_ERR_COULDNT_SPLIT_ITEMS                EQUIP_ERR_SPLIT_FAILED
// END GENERATED NAMES

// =====================================================================================================================
// Family 2: server settings. Turtle `sWorld.getConfig(KEY)` is one overloaded member per value type (uint32, int32, bool, float);
// ForeverCore keeps four separate stores (getIntConfig, getBoolConfig, getFloatConfig, and the rates behind getRate). One helper
// per Turtle value type; the key is passed as the Turtle name, which resolves on ForeverCore to one of:
//   1. the core's own enumerator of the same setting (the CONFIG_* macros of forever-compat-shim.h:265-282, plus the rate macros
//      below): the helper reads the core's value, so the bot sees what the operator set in worldserver.conf;
//   2. a key of this file's enums (TbW2U32Key, TbW2BoolKey, TbW2FloatKey) for a setting ForeverCore does not have: the helper returns
//      the Turtle default (the second argument of setConfig in the Turtle World.cpp, cited per key) and logs it once. Never a value
//      of our own. "Rate.Talent" is the one exception that has a ForeverCore spelling as a config-file key and not as a World
//      member: TraitMgr.cpp:95-105 reads it with sConfigMgr->GetFloatDefault, same meaning (talent points per level times it).
// The per-key table (Turtle key and default, ForeverCore enumerator, its default, file:line) is in WIDE-PASS-R2-NOTES.md.
// =====================================================================================================================
#include <atomic>

// Rates the core keeps behind World::getRate (World.h:482 enum Rates, World.cpp:1017-1068 each name and default 1.0). Same name
// without the type token, same meaning, same default 1.0 as the Turtle rate.
#ifndef CONFIG_FLOAT_RATE_AUCTION_DEPOSIT
#define CONFIG_FLOAT_RATE_AUCTION_DEPOSIT              RATE_AUCTION_DEPOSIT
#endif
#ifndef CONFIG_FLOAT_RATE_CREATURE_AGGRO
#define CONFIG_FLOAT_RATE_CREATURE_AGGRO               RATE_CREATURE_AGGRO
#endif
#ifndef CONFIG_FLOAT_RATE_XP_KILL
#define CONFIG_FLOAT_RATE_XP_KILL                      RATE_XP_KILL
#endif
#ifndef CONFIG_FLOAT_RATE_XP_QUEST
#define CONFIG_FLOAT_RATE_XP_QUEST                     RATE_XP_QUEST
#endif
#ifndef CONFIG_FLOAT_RATE_XP_EXPLORE
#define CONFIG_FLOAT_RATE_XP_EXPLORE                   RATE_XP_EXPLORE
#endif
#ifndef CONFIG_FLOAT_RATE_DROP_MONEY
#define CONFIG_FLOAT_RATE_DROP_MONEY                   RATE_DROP_MONEY
#endif
#ifndef CONFIG_FLOAT_RATE_DROP_ITEM_POOR
#define CONFIG_FLOAT_RATE_DROP_ITEM_POOR               RATE_DROP_ITEM_POOR
#endif
#ifndef CONFIG_FLOAT_RATE_DROP_ITEM_NORMAL
#define CONFIG_FLOAT_RATE_DROP_ITEM_NORMAL             RATE_DROP_ITEM_NORMAL
#endif
#ifndef CONFIG_FLOAT_RATE_DROP_ITEM_UNCOMMON
#define CONFIG_FLOAT_RATE_DROP_ITEM_UNCOMMON           RATE_DROP_ITEM_UNCOMMON
#endif
#ifndef CONFIG_FLOAT_RATE_DROP_ITEM_RARE
#define CONFIG_FLOAT_RATE_DROP_ITEM_RARE               RATE_DROP_ITEM_RARE
#endif
#ifndef CONFIG_FLOAT_RATE_DROP_ITEM_EPIC
#define CONFIG_FLOAT_RATE_DROP_ITEM_EPIC               RATE_DROP_ITEM_EPIC
#endif
#ifndef CONFIG_FLOAT_RATE_DROP_ITEM_LEGENDARY
#define CONFIG_FLOAT_RATE_DROP_ITEM_LEGENDARY          RATE_DROP_ITEM_LEGENDARY
#endif
#ifndef CONFIG_FLOAT_RATE_HONOR
#define CONFIG_FLOAT_RATE_HONOR                        RATE_HONOR
#endif
#ifndef CONFIG_FLOAT_RATE_REPUTATION_GAIN
#define CONFIG_FLOAT_RATE_REPUTATION_GAIN              RATE_REPUTATION_GAIN
#endif
#ifndef CONFIG_FLOAT_RATE_REPUTATION_LOWLEVEL_KILL
#define CONFIG_FLOAT_RATE_REPUTATION_LOWLEVEL_KILL     RATE_REPUTATION_LOWLEVEL_KILL
#endif
#ifndef CONFIG_FLOAT_RATE_REPUTATION_LOWLEVEL_QUEST
#define CONFIG_FLOAT_RATE_REPUTATION_LOWLEVEL_QUEST    RATE_REPUTATION_LOWLEVEL_QUEST
#endif

// Settings ForeverCore has no member for. The enumerator is the Turtle name; the helper below returns the Turtle default.
enum TbW2U32Key : uint32
{
    CONFIG_UINT32_AUCTION_DEPOSIT_MIN,              // Turtle World.cpp:1005 "Auction.Deposit.Min", default 0 (a floor on the deposit, in copper)
    CONFIG_UINT32_ACCOUNT_CONCURRENT_AUCTION_LIMIT, // Turtle World.cpp:1006 "Auction.AccountConcurrentLimit", default 0 (0 = no limit)
    CONFIG_UINT32_AV_MIN_PLAYERS_IN_QUEUE,          // Turtle World.cpp:1234 "Alterac.MinPlayersInQueue", default 0
    TBW2_U32_KEYS
};
enum TbW2BoolKey : uint32
{
    CONFIG_BOOL_ALLOW_TWO_SIDE_ACCOUNTS,            // Turtle World.cpp:1070 "AllowTwoSide.Accounts", default false
    TBW2_BOOL_KEYS
};
enum TbW2FloatKey : uint32
{
    CONFIG_FLOAT_RATE_XP_KILL_ELITE,                // Turtle World.cpp:976 "Rate.XP.Kill.Elite", default 1.0 (ForeverCore has one kill rate, Rate.XP.Kill)
    CONFIG_FLOAT_RATE_TALENT,                       // Turtle World.cpp:1013 "Rate.Talent", default 1.0; ForeverCore: the config key, TraitMgr.cpp:105
    TBW2_FLOAT_KEYS
};

namespace tbw2
{
// True the first time a bit is seen; the log line is printed once per key and per process.
inline bool FirstTime(std::atomic<uint32>& seen, uint32 key)
{
    return !(seen.fetch_or(1u << key) & (1u << key));
}
inline void NoteDefault(char const* turtleKey, char const* turtleValue)
{
    // Through the core logger directly, as CmangosLogProxy does (forever-compat-shim.h:495): the shim renames `sLog`, which TC_LOG_WARN
    // expands, so the macro does not compile in a file that includes the shim.
    Log::instance()->OutMessage("server.loading", LOG_LEVEL_WARN, "{}", std::string("TortoiseBots: server setting ") + turtleKey +
        " has no ForeverCore equivalent; the bot uses the Turtle default " + turtleValue);
}
}

inline uint32 WorldU32ConfigW2(WorldIntConfigs key) { return sWorld.getIntConfig(key); }
inline uint32 WorldU32ConfigW2(TbW2U32Key key)
{
    static std::atomic<uint32> seen{0};
    char const* name = "?";
    switch (key)
    {
        case CONFIG_UINT32_AUCTION_DEPOSIT_MIN:              name = "Auction.Deposit.Min"; break;
        case CONFIG_UINT32_ACCOUNT_CONCURRENT_AUCTION_LIMIT: name = "Auction.AccountConcurrentLimit"; break;
        case CONFIG_UINT32_AV_MIN_PLAYERS_IN_QUEUE:          name = "Alterac.MinPlayersInQueue"; break;
        default: break;
    }
    if (tbw2::FirstTime(seen, key))
        tbw2::NoteDefault(name, "0");
    return 0;   // all three Turtle defaults are 0
}

// The Turtle int32 store. ForeverCore keeps every integer setting as uint32; Quests.LowLevelHideDiff is the one int32 key read
// (default 4, World.cpp:847), so the value is read back as signed, as the Turtle getter returns it.
inline int32 WorldI32ConfigW2(WorldIntConfigs key) { return int32(sWorld.getIntConfig(key)); }

inline bool WorldBoolConfigW2(WorldBoolConfigs key) { return sWorld.getBoolConfig(key); }
inline bool WorldBoolConfigW2(TbW2BoolKey key)
{
    static std::atomic<uint32> seen{0};
    if (tbw2::FirstTime(seen, key))
        tbw2::NoteDefault("AllowTwoSide.Accounts", "false");
    return false;   // the Turtle default
}

inline float WorldFloatConfigW2(WorldFloatConfigs key) { return sWorld.getFloatConfig(key); }
inline float WorldFloatConfigW2(Rates key) { return sWorld.getRate(key); }
inline float WorldFloatConfigW2(TbW2FloatKey key)
{
    switch (key)
    {
        case CONFIG_FLOAT_RATE_TALENT:
            return sConfigMgr->GetFloatDefault("Rate.Talent", 1.0f);   // TraitMgr.cpp:105, default 1 (worldserver.conf.dist:2758)
        default:
            break;
    }
    static std::atomic<uint32> seen{0};
    if (tbw2::FirstTime(seen, key))
        tbw2::NoteDefault("Rate.XP.Kill.Elite", "1");
    return 1.0f;   // the Turtle default of Rate.XP.Kill.Elite
}

// =====================================================================================================================
// Family 3: a group's id as an in-memory identity. T `Group::GetId()` is the groups-table id (persisted, reused across restarts);
// F has no such member. F's group guid (Group.h:312 GetGUID(), HighGuid::Party, set in Group.cpp:150 and :224 from
// GroupMgr::GenerateGroupId, GroupMgr.cpp:91-98: starts at 1, counts up, never reused in a process, not stored) is the same kind of
// number for the lifetime of the session: one per live group, unique, non-zero. Offered ONLY to sites that compare it, keep it in a
// struct or use it as a map key and never write it to a database, a text, a log or a packet; the dashboard JSON and the telemetry
// keep the sites that do (not rewritten). The counter round-trips through GroupMgr::GetGroupByGUID(ObjectGuid::Create<HighGuid::Party>(id))
// (GroupMgr.cpp:124, GroupStore keyed by the counter), the lookup a later `GetGroupById` helper needs.
// =====================================================================================================================
inline uint32 GroupIdW2(Group const* group) { return uint32(group->GetGUID().GetCounter()); }

// =====================================================================================================================
// Family 4: one component of a spawn record's position. T: CreatureData and GameObjectData hold `WorldLocation position`
// (Creature.h:286) and the sites read one field of it. F: the record is a SpawnData (Maps/SpawnData.h:132-149): the map is
// SpawnMetadata::mapId (:121), the point `Position spawnPoint` (:136) read through GetPositionX/Y/Z and GetOrientation. The whole
// record has LocationOfCompat (TbCompat_Position.h:87); these answer the sites that read one number. The map id is the fifth
// component the same sites read (`position.mapId`).
// =====================================================================================================================
inline uint32 SpawnMapIdW2(SpawnData const& data) { return data.mapId; }
inline float SpawnXW2(SpawnData const& data) { return data.spawnPoint.GetPositionX(); }
inline float SpawnYW2(SpawnData const& data) { return data.spawnPoint.GetPositionY(); }
inline float SpawnZW2(SpawnData const& data) { return data.spawnPoint.GetPositionZ(); }
inline float SpawnOW2(SpawnData const& data) { return data.spawnPoint.GetOrientation(); }
