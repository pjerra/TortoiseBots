"""Wiring pins for the world slice W0 (the pool: registry, creation and the fix round).
…
"""
import csv
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "forever" / "tools"
MGR_CPP = (ROOT / "forever/src/TbBotMgr.cpp").read_text()
SCRIPTS = (ROOT / "forever/src/TbScripts.cpp").read_text()
REG_H = (ROOT / "forever/src/host/pool/TbPoolRegistry.h").read_text()
REG = (ROOT / "forever/src/host/pool/TbPoolRegistry.cpp").read_text()
CREATE_H = (ROOT / "forever/src/host/pool/TbPoolCreate.h").read_text()
CREATE = (ROOT / "forever/src/host/pool/TbPoolCreate.cpp").read_text()
POLICY = (ROOT / "ai/playerbot/policy/world/PoolCompositionPolicy.h").read_text()
POLICY_TEST = (ROOT / "forever/tools/test_pool_composition_policy.cpp").read_text()
SCHED_H = (ROOT / "forever/src/host/pool/TbPoolScheduler.h").read_text()
SCHED = (ROOT / "forever/src/host/pool/TbPoolScheduler.cpp").read_text()
SCHED_POLICY = (ROOT / "ai/playerbot/policy/world/PoolSchedulePolicy.h").read_text()
SCHED_TEST = (ROOT / "forever/tools/test_pool_schedule_policy.cpp").read_text()
CONFIG_CPP = (ROOT / "ai/playerbot/PlayerbotAIConfig.cpp").read_text()
MGR_H = (ROOT / "forever/src/TbBotMgr.h").read_text()
REGISTRY_POLICY = (ROOT / "ai/playerbot/policy/world/PoolRegistryPolicy.h").read_text()
REGISTRY_POLICY_TEST = (ROOT / "forever/tools/test_pool_registry_policy.cpp").read_text()
MAPCAP_POLICY = (ROOT / "ai/playerbot/policy/world/FreeMapCapPolicy.h").read_text()
COMPAT_F = (ROOT / "forever/src/compat/TbCompat_WL0.h").read_text()
COMPAT_T = (ROOT / "ai/cmangos-compat-wl0.h").read_text()
WRAPPER = (ROOT / "ai/playerbot/TbCompatWL0.h").read_text()
NAMES = (ROOT / "runtime/BotNameService.cpp").read_text()
SQL = (ROOT / "forever/sql/characters/2026_11_20_01_tb_pool.sql").read_text()
SQL_ROWS = (ROOT / "forever/sql/characters/2026_11_20_02_tb_pool_rows.sql").read_text()
CONF = (ROOT / "forever/conf/tortoisebots.conf.dist").read_text()
LIST = (ROOT / "forever/SLICE-W4.txt").read_text()
CMAKE = (ROOT / "CMakeLists.txt").read_text()
GEN = (ROOT / "forever/tools/slice_gen_context.py").read_text()


def func(text, signature):
    """The text of the function whose definition line contains `signature` (up to its closing brace)."""
    i = text.index(signature)
    j = text.index("{", i)
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[i:k + 1]
    raise AssertionError(signature)


def log_calls(text):
    """The format strings of the core log calls and the command replies in a source text."""
    calls = []
    for m in re.finditer(r"TC_LOG_(?:TRACE|DEBUG|INFO|WARN|ERROR)\(\s*\"[^\"]*\"\s*,\s*(.*?)\);", text, re.S):
        calls.append(m.group(1))
    for m in re.finditer(r"PSendSysMessage\((.*?)\);", text, re.S):
        calls.append(m.group(1))
    return calls


class Creation(unittest.TestCase):
    def test_a_created_character_is_the_core_creation_in_its_race_start_position(self):
        body = func(MGR_CPP, "std::string BotMgr::CreateCharacter(")
        self.assertIn("info.UseNPE = false;", body)
        self.assertLess(body.index("info.UseNPE = false;"), body.index("player->Create("))
        self.assertIn("SetAtLoginFlag(AT_LOGIN_FIRST)", body)
        self.assertIn("player->SaveToDB(", body)
        # The pool creates through that one call, never with its own Player::Create.
        self.assertIn("BotMgr::Instance().CreateCharacter(chosen.id, name", func(CREATE, "Step MakeOne("))
        self.assertNotIn("Player::Create", CREATE)

    def test_the_composition_is_the_policy_and_the_gender_is_rolled(self):
        body = func(CREATE, "Step MakeOne(")
        self.assertIn("world::CompositionAt(", body)
        self.assertIn("world::GenderAt(", body)
        self.assertIn("world::CensusCompositionAt(", body)
        self.assertIn("if (CensusComposition())", body)
        self.assertIn('GetBoolDefault("TortoiseBots.Pool.CensusComposition", true)', REG)
        self.assertIn("TortoiseBots.Pool.CensusComposition = 1", CONF)
        self.assertIn("PickBotCharacterName(composition.race, composition.klass, gender)", body)
        # And the policy is the one the spec pins: the vanilla pairs, the races the test refuses.
        self.assertIn("RaceHuman = 1", POLICY)
        self.assertIn("RaceTroll = 8", POLICY)
        self.assertIn("ClassDruid = 11", POLICY)
        for refused in ("IsVanillaPair(w::RaceHuman, 6 /*death knight*/)", "IsVanillaPair(w::RaceOrc, w::ClassPaladin)",
                        "IsVanillaPair(w::RaceTauren, w::ClassRogue)"):
            self.assertIn(refused, POLICY_TEST)
        self.assertIn("perRace[race] >= 12 && perRace[race] <= 13", POLICY_TEST)

    def test_a_refused_composition_is_skipped_not_fatal(self):
        # A race the realm's client data cannot create must not abort `.tb pool create`: the claim is dropped, the race is
        # retired for the rest of the job, the pair is logged once, and the next tick rolls another composition.
        body = func(CREATE, "Step MakeOne(")
        self.assertIn("world::CensusCompositionAt(s.serial, urand(0, 0xffff), urand(0, 0xffff), urand(0, 999), s.retiredRaces)", body)
        self.assertIn("world::CompositionAt(s.serial, urand(0, 0xffff), s.retiredRaces)", body)
        self.assertIn("if (!composition.race)", body)
        self.assertIn("s.retiredRaces |= world::CompositionRaceBit(composition.race);", body)
        self.assertIn("s.loggedRefusals.insert(key).second", body)
        self.assertIn("s.loggedNoRace.insert(key).second", body)
        self.assertIn("kCompositionRefusalTries", body)
        use = body.index("BotMgr::Instance().CreateCharacter(")
        self.assertLess(body.index("DeleteCharacterRow(name);", use), body.index("return Step::Retry;", use))
        self.assertNotIn('s.stop = "the core refused to create " + name', body)
        start = func(CREATE, "CreateStart StartCreate(")
        for text in ("s.retiredRaces = 0;", "s.refusals = 0;", "s.loggedRefusals.clear();", "s.loggedNoRace.clear();"):
            self.assertIn(text, start)
        self.assertEqual(func(CREATE, "void CreateUpdate(").count("s.refusals = 0;"), 2)   # Made and Queued
        for text in ("CompositionRaceBit(", "retiredRaces", "kCompositionAllRaces"):
            self.assertIn(text, POLICY)
        self.assertIn("CensusCompositionAt(0, 200, 0, 0)", POLICY_TEST)
        self.assertIn("CompositionAt(0, 0, human)", POLICY_TEST)

    def test_accounts_come_from_the_core_and_the_password_is_never_logged_or_stored(self):
        body = func(CREATE, "ChosenAccount AccountFor(")
        # The stamp is the account's e-mail: the one field AccountMgr reads back, so a row can prove its account is still its own.
        self.assertIn("sAccountMgr->CreateAccount(username, password, claim.stamp)", body)
        self.assertIn("AOR_NAME_ALREADY_EXIST", body)
        self.assertIn("AccountCarriesStamp(accountId, username, claim.stamp)", body)
        self.assertLess(body.index("sAccountMgr->CreateAccount(username, password, claim.stamp)"), body.index("AccountMgr::GetId(username)"))
        self.assertIn("std::fill(password.begin(), password.end(), '\\0')", body)
        # A result other than OK, ALREADY_EXIST or DB_INTERNAL stops the job with the code (the spec's U4).
        self.assertIn("result == AccountOpResult::AOR_DB_INTERNAL_ERROR || result == AccountOpResult::AOR_NAME_ALREADY_EXIST", body)
        self.assertIn("std::uint32_t(result)", body)
        for text, what in ((CREATE, "host/pool/TbPoolCreate.cpp"), (REG, "host/pool/TbPoolRegistry.cpp")):
            for call in log_calls(text):
                self.assertNotIn("password", call.lower(), what + ": a log or reply names the password")
        self.assertNotIn("password", REG_H.lower())
        # Never stored: the registry's own row carries no password field.
        self.assertNotIn("password", func(REG, "bool StoreAccount("))

    def test_the_pool_places_one_character_per_account_and_paces_creation(self):
        # One session per account: a second character on an account could never be online, so the pool keeps one per account
        # and its account rows follow its characters.
        self.assertIn("kPoolCharactersPerAccount = 1", REGISTRY_POLICY)
        self.assertIn("kCharactersPerAccount = world::kPoolCharactersPerAccount", REG_H)
        room = func(REG, "std::uint32_t AccountWithRoom(")
        self.assertIn("world::PoolHasRoomForCharacter(std::max(CharactersOnAccount(account.id), present), kCharactersPerAccount)", room)
        self.assertIn("CharactersOnAccountNow(account.id, present)", room)
        self.assertIn("60000 / rate", func(CREATE, "CreateStart StartCreate("))
        self.assertIn("s.nextMs = s.intervalMs;", func(CREATE, "void CreateUpdate("))
        # One character per tick at most: MakeOne is called once, from the update, and the job never loops inside it.
        self.assertEqual(CREATE.count("MakeOne(s)"), 1)
        body = func(CREATE, "void CreateUpdate(")
        self.assertEqual(body.count("MakeOne(s)"), 1)

    def test_the_rate_and_the_total_cap_reach_the_creation_from_the_config(self):
        self.assertIn('GetBoolDefault("TortoiseBots.Pool.Enable", false)', REG)
        self.assertIn('GetIntDefault("TortoiseBots.Pool.CreatePerMinute", 30)', REG)
        self.assertIn('GetIntDefault("TortoiseBots.Pool.MaxCharacters", int32(world::DefaultMaxPoolCharacters))', REG)
        self.assertIn("TortoiseBots.Pool.Enable = 0", CONF)
        self.assertIn("TortoiseBots.Pool.CreatePerMinute = 30", CONF)
        self.assertIn("TortoiseBots.Pool.MaxCharacters = 50", CONF)
        self.assertIn("return s.enabled ? s.createPerMinute : 0;", REG)
        self.assertIn("return s.enabled ? s.maxCharacters : 0;", REG)
        # The create path asks the cap, the allowance and the present size before it starts.
        start = func(CREATE, "CreateStart StartCreate(")
        self.assertIn("std::uint32_t const cap = MaxCharacters();", start)
        self.assertIn("std::uint32_t const present = PoolCharacters();", start)
        self.assertIn("present == kPoolCountUnknown", start)
        self.assertIn("world::AllowedPoolCharacters(count, present, cap)", start)
        self.assertIn("its cap is {}", start)
        self.assertIn("TortoiseBots.Pool.MaxCharacters", start)
        # And it is enforced while the job runs, too.
        self.assertIn("if (present >= s.cap)", func(CREATE, "Step MakeOne("))
        # The job keeps the cap it started with: a reload never changes a running job.
        self.assertIn("s.cap = cap;", start)
        self.assertNotIn("MaxCharacters()", func(CREATE, "Step MakeOne("))

    def test_a_creation_writes_its_claim_row_before_each_irreversible_step(self):
        body = func(CREATE, "ChosenAccount AccountFor(")
        self.assertLess(body.index("if (!StoreAccount(claim))"), body.index("sAccountMgr->CreateAccount(username, password, claim.stamp)"))
        make = func(CREATE, "Step MakeOne(")
        self.assertLess(make.index("if (!StoreCharacter(pending))"), make.index("BotMgr::Instance().CreateCharacter("))
        # A queued creation is not a creation: MakeOne never marks the row ready, it hands it to the confirmation, which
        # asks the characters database first (never the cache).
        self.assertNotIn("done.state = world::PoolRowState::Ready;", make)
        self.assertIn("s.pending = Pending{", make)
        confirm = func(CREATE, "bool ConfirmPending(")
        self.assertIn("CharacterOwner(p.name", confirm)
        self.assertLess(confirm.index("CharacterOwner(p.name"), confirm.index("FinishCharacter(claim"))
        # The cache entry follows the row's `ready` write (PublishCharacter), so a queued creation is never published.
        finish = func(CREATE, "bool FinishCharacter(")
        self.assertLess(finish.index("UpdateCharacter(claim)"), finish.index("PublishCharacter(claim)"))
        self.assertIn("sCharacterCache->AddCharacterCacheEntry", func(CREATE, "void PublishCharacter("))
        self.assertLess(body.index("sAccountMgr->CreateAccount("), body.index("pool account {} (#{}) created"))
        # The account row is finished when the character row is.
        finish = func(CREATE, "bool FinishCharacter(")
        self.assertIn("account.state = world::PoolRowState::Ready;", finish)
        self.assertLess(finish.index("UpdateCharacter(claim)"), finish.index("account.state = world::PoolRowState::Ready;"))

    def test_reconciliation_finishes_or_marks_and_never_deletes(self):
        body = func(CREATE, "void Reconcile(")
        self.assertIn("world::DecidePoolCharacter(", body)
        self.assertIn("world::DecidePoolAccount(", body)
        self.assertNotIn("world::PoolAccountAction::RemoveAccount", body)
        # The reconciliation deletes nothing, and decides from the characters database, never the character cache.
        for forbidden in ("AccountMgr::DeleteAccount", "DeleteAccountRow", "DeleteCharacterRow", "sCharacterCache", "GetCharacterGuidByName"):
            self.assertNotIn(forbidden, body)
        self.assertIn("CharacterOwner(claim.name", body)
        self.assertIn("world::PoolRowState::Failed", body)
        # At module start and before every create.
        self.assertIn("TortoiseBots::pool::Reconcile();", SCRIPTS)
        self.assertIn("Reconcile();", func(CREATE, "CreateStart StartCreate("))
        # It runs before the cap check, and never on top of a running job.
        start = func(CREATE, "CreateStart StartCreate(")
        self.assertLess(start.index("if (s.remaining)"), start.index("Reconcile();"))
        self.assertLess(start.index("Reconcile();"), start.index("MaxCharacters()"))
        # No claim is made while a job runs, so a start cannot touch a row its own job is using.
        self.assertIn("if (s.remaining)", start)

    def test_a_created_character_is_the_cores_creation_with_its_bookkeeping(self):
        body = func(MGR_CPP, "std::string BotMgr::CreateCharacter(")
        # The saves are queued and neither commit's answer is known: nothing is published here, and the login side is not committed.
        self.assertNotIn("AddCharacterCacheEntry", body)
        self.assertNotIn("LoginDatabase.CommitTransaction", body)
        self.assertNotIn("CharacterDatabase.CommitTransaction", body)
        self.assertIn("CharacterDatabase.AsyncCommitTransaction(characterTransaction)", body)
        self.assertIn("_creating.push_back(std::move(pending))", body)
        # The core's own bookkeeping (CharacterHandler.cpp): the realm's character count rides in the login transaction.
        self.assertIn("LoginDatabase.GetPreparedStatement(LOGIN_REP_REALM_CHARACTERS)", body)
        self.assertIn("sRealmList->GetCurrentRealmId().Realm", body)
        self.assertLess(body.index("player->SaveToDB("), body.index("loginTransaction->Append(realmStmt)"))
        self.assertIn("PoolRealmCharacterCountCompat(accountId, existing)", body)
        confirm = func(MGR_CPP, "void BotMgr::ConfirmCreations(")
        # Order: the character commit's answer first; a failure returns before the login commit, the hook and the cache.
        failed = confirm.index("if (!creation.commit.get())")
        self.assertLess(failed, confirm.index("LoginDatabase.CommitTransaction(creation.loginTransaction)"))
        self.assertIn("continue;", confirm[failed:confirm.index("LoginDatabase.CommitTransaction(")])
        self.assertLess(confirm.index("LoginDatabase.CommitTransaction("), confirm.index("sScriptMgr->OnPlayerCreate(player)"))
        self.assertLess(confirm.index("sScriptMgr->OnPlayerCreate(player)"), confirm.index("AddCharacterCacheEntry"))
        update = func(MGR_CPP, "void BotMgr::Update(")
        self.assertIn("ConfirmCreations();", update)
        # The player is destroyed before the session it was made on.
        record = MGR_CPP[MGR_CPP.index("struct BotMgr::PendingCreation"):]
        record = record[:record.index("};")]
        self.assertLess(record.index("std::shared_ptr<BotSession> session;"), record.index("std::shared_ptr<Player> player;"))
        # `.tb create` says the same thing: the saves are queued, not done.
        create = func(SCRIPTS, "static bool HandleCreate(")
        self.assertIn("its saves are queued", create)

    def test_prune_is_an_explicit_confirmed_clean_up(self):
        body = func(CREATE, "PruneReport Prune(")
        # A dry run unless `confirm`: it lists and removes nothing else.
        self.assertIn("report.applied = confirm;", body)
        self.assertIn("if (confirm)", body)
        self.assertIn("AccountMgr::DeleteAccount(accountId)", body)
        self.assertIn("world::DecideAccountPrune(facts)", body)
        self.assertIn("world::PoolAccountCheck::Trusted", body)
        self.assertIn("AccountCharacters(accountId, characters)", body)
        self.assertIn("FindCharacterRow(character.name, registryRow)", body)
        # Only failed rows are ever touched, and a running job defers the command.
        self.assertIn("world::PoolRowState::Failed", body)
        self.assertIn("if (Creating())", body)
        # The command: `.tb pool prune` alone is the dry run.
        handler = func(SCRIPTS, "static bool HandlePoolPrune(")
        self.assertIn('Lower(*argument) != "confirm"', handler)
        self.assertIn("TortoiseBots::pool::Prune(", handler)


class Registry(unittest.TestCase):
    def test_every_statement_is_the_wl0_guard_and_its_two_tables(self):
        for name in ("ReadAccounts", "ReadCharacters", "FindAccountRow", "FindCharacterRow", "StoreAccount", "UpdateAccount", "DeleteAccountRow",
                     "StoreCharacter", "UpdateCharacter", "DeleteCharacterRow", "CheckSchema"):
            body = func(REG, name + "(")
            self.assertNotIn("CharacterDatabase.Query", body, name)
            self.assertNotIn("CharacterDatabase.PQuery", body, name)
            self.assertNotIn("CharacterDatabase.DirectExecute", body, name)
            self.assertNotRegex(body, r"LoginDatabase", name)
        self.assertIn("PoolCharacterQueryCompat(", REG)
        self.assertIn("PoolCharacterExecuteCompat(", REG)
        self.assertIn("CharacterSchemaHasCompat(", REG)
        # The whitelist of the guard is exactly the two tables of the migration, and the writes are the registry's four shapes.
        self.assertIn('table == "tb_pool_account" || table == "tb_pool_character"', COMPAT_F)
        for prefix in ('"INSERT INTO `"', '"UPDATE `"', '"DELETE FROM `"', '"CREATE TABLE IF NOT EXISTS `"'):
            self.assertIn(prefix, COMPAT_F)

    def test_a_missing_table_or_column_is_pool_off_with_one_line(self):
        body = func(REG, "bool CheckSchema(")
        self.assertIn("s.tableWarned", body)
        self.assertIn("the bot pool is off", body)
        self.assertIn("forever/sql/characters/2026_11_20_01_tb_pool.sql", body)
        self.assertIn("2026_11_20_02_tb_pool_rows.sql", body)
        # Never an abort: the guard asks information_schema first and answers null.
        self.assertIn("CharacterSchemaHasCompat(table, columns)", COMPAT_F)
        self.assertIn("return QueryResult();", COMPAT_F)

    def test_nothing_under_host_pool_reads_a_database_directly(self):
        for path in (ROOT / "forever/src/host/pool").glob("*"):
            text = path.read_text()
            for forbidden in ("CharacterDatabase.Query(", "CharacterDatabase.PQuery(", "CharacterDatabase.DirectExecute(", "LoginDatabase"):
                self.assertNotIn(forbidden, text, str(path))

    def test_the_accounts_are_validated_by_name_id_and_creation_stamp(self):
        body = func(REG, "world::PoolAccountFacts FactsOf(")
        self.assertIn("AccountMgr::GetName(accountId, name)", body)
        self.assertIn("AccountMgr::GetId(username)", body)
        self.assertIn("AccountMgr::GetEmail(accountId, email)", body)
        self.assertNotIn("LoginDatabase", REG)
        # The decision itself is the pure policy, tested on its own.
        for text in ("CheckPoolAccount(", "PoolAccountCheck::NameChanged", "PoolAccountCheck::NameReused",
                     "PoolAccountCheck::StampMissing", "PoolAccountCheck::StampChanged"):
            self.assertIn(text, REGISTRY_POLICY)
        for text in ("NameChanged", "StampChanged", "NameReused", "StampMissing", "NameOutsidePool"):
            self.assertIn(text, REGISTRY_POLICY_TEST)
        # A ready row that is not trusted is marked stale once and never used.
        load = func(REG, "void LoadRows(")
        self.assertIn("if (check != world::PoolAccountCheck::Trusted)", load)
        self.assertIn("account.state = world::PoolRowState::Stale;", load)
        self.assertIn("WriteAccount(account);", load)

    def test_the_migration_carries_the_state_stamp_and_both_keys(self):
        for table, columns in (
                ("tb_pool_account", ("username", "account_id", "stamp", "state", "created")),
                ("tb_pool_character", ("name", "guid", "account_id", "race", "class", "gender", "state", "created", "last_login", "last_logout"))):
            for column in columns:
                self.assertIn("`" + column + "`", SQL_ROWS)
            self.assertIn("`" + table + "`", SQL_ROWS)
        self.assertIn("ADD COLUMN `stamp`", SQL_ROWS)
        self.assertIn("ADD COLUMN `state`", SQL_ROWS)
        self.assertIn("DROP PRIMARY KEY", SQL_ROWS)
        self.assertIn("ADD PRIMARY KEY (`username`)", SQL_ROWS)
        self.assertIn("ADD PRIMARY KEY (`name`)", SQL_ROWS)
        self.assertIn("MODIFY `account_id` int unsigned DEFAULT NULL", SQL_ROWS)
        self.assertIn("MODIFY `guid` bigint unsigned DEFAULT NULL", SQL_ROWS)
        # The old rows carry no stamp: they are stale, never used and never removed (the statements are PREPAREd text).
        self.assertIn("UPDATE `tb_pool_account` SET `state` = ''stale''", SQL_ROWS)
        self.assertIn("UPDATE `tb_pool_character` SET `state` = ''stale''", SQL_ROWS)
        # The same DDL is in the registry, so an enabled server needs no migration.
        self.assertIn("kCreateAccountTable", REG)
        self.assertIn("kCreateCharacterTable", REG)
        self.assertIn("PRIMARY KEY (`username`),\"", REG)
        self.assertIn("PRIMARY KEY (`name`),\"", REG)
        self.assertIn("`stamp` char(16) NOT NULL DEFAULT '',\"", REG)
        self.assertIn("`state` varchar(8) NOT NULL DEFAULT 'creating',\"", REG)

    def test_the_cap_counts_the_database_not_a_list(self):
        body = func(REG, "std::uint32_t PoolCharacters(")
        # The characters the database holds on the accounts the registry names (any row state), and the non-ready rows whose
        # character is not there; no answer is unknown, never zero.
        self.assertIn("PoolCharactersOnAccountsCompat(ids, total)", body)
        self.assertIn("return kPoolCountUnknown;", body)
        self.assertIn("character.state == world::PoolRowState::Ready", body)
        self.assertNotIn("s.characters.size()", body)
        self.assertIn("PoolAccountRows()", func(CREATE, "ChosenAccount AccountFor("))
        self.assertIn("world::MaxPoolAccounts(s.cap, kCharactersPerAccount)", func(CREATE, "ChosenAccount AccountFor("))

    def test_a_failed_auth_lookup_marks_nothing_stale(self):
        load = func(REG, "void LoadRows(")
        self.assertIn("if (check == world::PoolAccountCheck::NoAccount)", load)
        self.assertIn("s.unknownRows = true;", load)
        self.assertLess(load.index("if (check == world::PoolAccountCheck::NoAccount)"), load.index("account.state = world::PoolRowState::Stale;"))

    def test_the_reconciliation_does_not_stale_a_claim_on_an_unanswered_lookup(self):
        body = func(CREATE, "void Reconcile(")
        guard = body.index("account && check == world::PoolAccountCheck::NoAccount")
        self.assertLess(guard, body.index("world::DecidePoolCharacter(check, presence)"))
        self.assertIn("++retained;", body[guard:body.index("world::DecidePoolCharacter(check, presence)")])

    def test_the_migration_has_no_delimiter_and_no_procedure(self):
        code = "\n".join(line for line in SQL_ROWS.splitlines() if not line.lstrip().startswith("--"))
        self.assertNotIn("DELIMITER", code)
        self.assertNotIn("PROCEDURE", code)
        self.assertIn("PREPARE stmt FROM @s;", SQL_ROWS)
        self.assertIn("information_schema.COLUMNS", SQL_ROWS)
        self.assertIn("information_schema.STATISTICS", SQL_ROWS)
        self.assertNotIn("DELIMITER", "\n".join(line for line in SQL.splitlines() if not line.lstrip().startswith("--")))


class Compat(unittest.TestCase):
    def test_both_sides_of_the_name_check_and_the_wrapper(self):
        self.assertIn("inline bool CheckPlayerNameCompat(std::string const& name)", COMPAT_F)
        self.assertIn("ObjectMgr::CheckPlayerName(name, LOCALE_enUS, true) == CHAR_NAME_SUCCESS", COMPAT_F)
        # Turtle keeps the call it always made.
        self.assertIn("ObjectMgr::CheckPlayerName(name, true) == CHAR_NAME_SUCCESS", COMPAT_T)
        self.assertIn('__has_include("TbCompat_WL0.h")', WRAPPER)
        self.assertIn('#include "TbCompat_WL0.h"', WRAPPER)
        self.assertIn('#include "cmangos-compat-wl0.h"', WRAPPER)
        # The name service calls the pair, not the core's signature.
        self.assertIn("CheckPlayerNameCompat(name)", NAMES)
        self.assertNotIn("ObjectMgr::CheckPlayerName(name, true)", NAMES)
        self.assertIn('#include "playerbot/TbCompatWL0.h"', NAMES)

    def test_unused_turtle_pool_helpers_do_less(self):
        body = func(COMPAT_T, "inline std::unique_ptr<QueryResult> PoolCharacterQueryCompat(")
        self.assertIn("*schemaMissing = true", body)
        self.assertIn("return std::unique_ptr<QueryResult>();", body)
        self.assertIn("inline void PoolCharacterExecuteCompat(std::string const& /*sql*/) {}", COMPAT_T)


class Wiring(unittest.TestCase):
    def test_the_console_command_and_the_world_tick(self):
        self.assertIn('{ "pool",   poolTable },', SCRIPTS)
        create = func(SCRIPTS, "static bool HandlePoolCreate(")
        self.assertIn("TortoiseBots::pool::StartCreate(count)", create)
        self.assertIn("if (!start.started)", create)
        # `.tb pool prune` is a word, not a number: the argument is a string, and `confirm` is the only one.
        prune = func(SCRIPTS, "static bool HandlePoolPrune(")
        self.assertIn("Optional<std::string> argument", prune)
        self.assertIn('Lower(*argument) != "confirm"', prune)
        self.assertIn(".tb pool prune confirm", prune)

    def test_creating_and_confirmed_pruning_need_an_administrator_permission(self):
        # Not the lowest GM permission every other `.tb` row carries: the permissions of `.account create` and `.account delete`.
        table = SCRIPTS[SCRIPTS.index("static ChatCommandTable poolTable"):]
        table = table[:table.index("};")]
        self.assertIn('{ "create", HandlePoolCreate, rbac::RBAC_PERM_COMMAND_ACCOUNT_CREATE, Console::Yes }', table)
        self.assertIn('{ "prune",  HandlePoolPrune,  rbac::RBAC_PERM_COMMAND_GM, Console::Yes }', table)
        prune = func(SCRIPTS, "static bool HandlePoolPrune(")
        self.assertIn("handler->HasPermission(rbac::RBAC_PERM_COMMAND_ACCOUNT_DELETE)", prune)
        # The check comes before the prune runs, and only `confirm` asks for it (the dry run stays at the GM level).
        self.assertLess(prune.index("RBAC_PERM_COMMAND_ACCOUNT_DELETE"), prune.index("TortoiseBots::pool::Prune("))
        self.assertLess(prune.index('Lower(*argument) != "confirm"'), prune.index("RBAC_PERM_COMMAND_ACCOUNT_DELETE"))

    def test_a_job_stops_when_the_key_is_turned_off(self):
        update = func(CREATE, "void CreateUpdate(")
        self.assertIn("if (!KeyOn())", update)
        self.assertLess(update.index("if (!KeyOn())"), update.index("ConfirmPending(s, diff)"))
        self.assertIn("the pool was turned off", update)

        # The tick is right after the bot update, before the dashboard's work.
        tick = func(SCRIPTS, "void OnBeforeMapsUpdate(")
        self.assertLess(tick.index("BotMgr::Instance().Update(diff);"), tick.index("TortoiseBots::pool::CreateUpdate(diff);"))
        self.assertLess(tick.index("TortoiseBots::pool::CreateUpdate(diff);"), tick.index("TortoiseBots::Dashboard::Update(diff);"))
        # And the config, both at boot and on a reload; the reconciliation once, at boot.
        self.assertEqual(SCRIPTS.count("TortoiseBots::pool::LoadConfig();"), 2)
        self.assertEqual(SCRIPTS.count("TortoiseBots::pool::Reconcile();"), 1)
        init = func(SCRIPTS, "void OnWorldInitialized(")
        self.assertLess(init.index("TortoiseBots::pool::LoadConfig();"), init.index("TortoiseBots::pool::Reconcile();"))

    def test_the_off_build_creates_nothing_and_says_so(self):
        tail = CREATE[CREATE.index("#else"):]
        self.assertIn("the bot engine is not built in (TORTOISEBOTS_SLICE1 is off)", tail)
        self.assertIn("void CreateUpdate(std::uint32_t /*diff*/) {}", tail)
        self.assertIn("void Reconcile() {}", tail)
        self.assertIn("return false;", tail)

    def test_the_slice_list_carries_the_name_service_and_the_build_reads_it(self):
        listed = [l.strip() for l in LIST.splitlines() if l.strip() and not l.startswith("#")]
        self.assertIn("runtime/BotNameService.cpp", listed)
        self.assertIn("forever/SLICE-W4.txt", CMAKE)
        self.assertIn("WL0_KEYS", GEN)
        self.assertRegex(GEN, r"WL0_KEYS[,)]")


    def test_the_last_round_pins(self):
        # `.tb create` asks what `.tb pool create` asks and refuses an account that is not the pool's or a named test account.
        table = SCRIPTS[SCRIPTS.index('{ "create", HandleCreate'):][:200]
        self.assertIn("RBAC_PERM_COMMAND_ACCOUNT_CREATE", table)
        body = func(SCRIPTS, "static bool HandleCreate(")
        self.assertIn('CreateAccountAllowed(account, sConfigMgr->GetStringDefault("TortoiseBots.Create.TestAccounts"', body)
        self.assertLess(body.index("CreateAccountAllowed"), body.index("CreateCharacter("))
        self.assertIn("TortoiseBots.Create.TestAccounts", (ROOT / "forever/conf/tortoisebots.conf.dist").read_text())
        self.assertIn("TortoiseBots.Create.TestAccounts", (ROOT / "forever/docs/TB-COMMANDS.md").read_text())
        # The migration: the character half is keyed on the old key (a rerun repairs an interrupted run), and a row with no name
        # is removed before the primary key on `name`.
        self.assertIn("IF(@tb_char_pk, 'DELETE FROM `tb_pool_character` WHERE `name` = ''''', 'DO 0')", SQL_ROWS)
        self.assertIn("SET c.`name` = ch.`name` WHERE c.`name` = '''''", SQL_ROWS)
        self.assertIn("IF(@tb_acct_old OR @tb_acct_pk, 'UPDATE `tb_pool_account` SET `state` = ''stale''", SQL_ROWS)
        self.assertLess(SQL_ROWS.index("WHERE `name` = ''''"), SQL_ROWS.index("ADD PRIMARY KEY (`name`)"))
        # The login commit: a row is ready only after BotMgr says the creation is complete.
        self.assertIn("bool CreationInFlight(std::string const& name) const;", (ROOT / "forever/src/TbBotMgr.h").read_text())


class Behaviour(unittest.TestCase):
    """The registry and creation's own translation units against a fake core: g++ and a run, no build tree."""

    def test_the_pool_check_passes(self):
        gxx = shutil.which("g++")
        if not gxx:
            self.skipTest("no g++")
        with tempfile.TemporaryDirectory(prefix="pool-registry-") as tmp:
            tmp_path = pathlib.Path(tmp)
            out = self.build_and_run(tmp_path, (ROOT / "forever/src/host/pool/TbPoolRegistry.cpp").read_text(),
                                     (ROOT / "forever/src/host/pool/TbPoolCreate.cpp").read_text(), gxx)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        self.assertIn("RESULT: PASS", out.stdout)

    def build_and_run(self, tmp, registry, create, gxx, compat=None):
        for rel in ("forever/tools/pool_registry_check.cpp", "forever/src/host/pool/TbPoolRegistry.h",
                    "forever/src/host/pool/TbPoolCreate.h", "forever/src/host/pool/TbPoolReset.h",
                    "forever/src/host/pool/TbPoolReset.cpp",
                    "forever/src/compat/TbCompat_WL0.h",
                    "forever/src/compat/TbCompat_CharStore.h", "forever/src/compat/TbSqlNames.h",
                    "ai/playerbot/policy/world/PoolRegistryPolicy.h", "ai/playerbot/policy/world/PoolCompositionPolicy.h",
                    "ai/playerbot/policy/world/PoolResetSafetyPolicy.h", "runtime/policy/infra/PoolResetPolicy.h"):
            (tmp / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / rel, tmp / rel)
        shutil.copytree(TOOLS / "pool_registry_fakes", tmp / "forever/tools/pool_registry_fakes")
        if compat is not None:
            (tmp / "forever/src/compat/TbCompat_WL0.h").write_text(compat)
        (tmp / "forever/src/host/pool/TbPoolRegistry.cpp").write_text(registry)
        (tmp / "forever/src/host/pool/TbPoolCreate.cpp").write_text(create)
        exe = tmp / "pool_registry_check"
        subprocess.run([gxx, "-std=c++17", "-O0", "-DTORTOISEBOTS_SLICE1",
                        "-I", str(tmp / "forever/tools/pool_registry_fakes"), "-I", str(tmp / "forever/src"), "-I", str(tmp),
                        str(tmp / "forever/tools/pool_registry_check.cpp"),
                        str(tmp / "forever/src/host/pool/TbPoolRegistry.cpp"),
                        str(tmp / "forever/src/host/pool/TbPoolCreate.cpp"),
                        str(tmp / "forever/src/host/pool/TbPoolReset.cpp"), "-o", str(exe)],
                       check=True, capture_output=True, text=True)
        return subprocess.run([str(exe)], capture_output=True, text=True)


# Each mutation restores one of the defects this round fixes; the check has to catch it.
class Scheduler(unittest.TestCase):
    """Round W0-B: the scheduler, its policy and the three console words (pins; the behaviour is the policy test run below)."""

    def test_the_world_tick_runs_the_scheduler_after_the_bots_and_never_with_a_creation(self):
        tick = func(SCRIPTS, "void OnBeforeMapsUpdate(")
        self.assertLess(tick.index("BotMgr::Instance().Update(diff);"), tick.index("TortoiseBots::pool::PoolScheduler::Update(diff);"))
        self.assertLess(tick.index("TortoiseBots::pool::PoolScheduler::Update(diff);"), tick.index("TortoiseBots::pool::CreateUpdate(diff);"))
        self.assertLess(tick.index("TortoiseBots::pool::CreateUpdate(diff);"), tick.index("TortoiseBots::Dashboard::Update(diff);"))
        # The spec's budget: never a character creation in the same tick as a login.
        self.assertIn("bool const poolLogin = TortoiseBots::pool::PoolScheduler::Update(diff);", tick)
        self.assertIn("if (!poolLogin)", tick)
        create = tick[tick.index("if (!poolLogin)"):]
        self.assertLess(create.index("if (!poolLogin)"), create.index("CreateUpdate(diff);"))
        # Once a second, on the world thread, with the config at boot and on a reload.
        update = func(SCHED, "bool Update(")
        self.assertIn("if (s.accMs < 1000)", update)
        self.assertIn("return Pass(s);", update)
        self.assertEqual(SCRIPTS.count("TortoiseBots::pool::PoolScheduler::LoadConfig();"), 2)
        self.assertLess(func(SCRIPTS, "void OnWorldInitialized(").index("TortoiseBots::pool::PoolScheduler::LoadConfig();"),
                        func(SCRIPTS, "void OnWorldInitialized(").index("TortoiseBots::pool::Reconcile();"))

    def test_the_bot_manager_marks_the_pools_bots(self):
        # `bool pool` on Bot, its one reader, and the login that sets it.
        bot = MGR_H[MGR_H.index("    struct Bot\n    {"):]
        bot = bot[:bot.index("\n    };")]
        self.assertIn("bool pool = false;", bot)
        self.assertIn("// W0:", bot)
        self.assertIn("bool IsPoolBot(ObjectGuid characterGuid) const", MGR_H)
        self.assertIn("bool AccountHasBot(uint32 accountId) const;", MGR_H)
        self.assertIn("std::vector<uint32> BotAccounts() const;", MGR_H)
        self.assertIn("itr->second.pool", MGR_H)
        # PR #129 (player commands R2) appended `bool owned` for `.bot add`; the pool arguments stay as they were.
        self.assertIn("std::string Login(ObjectGuid characterGuid, ObjectGuid master, bool free = false, bool pool = false, "
                      "bool owned = false);", MGR_H)
        login = func(MGR_CPP, "std::string BotMgr::Login(")
        self.assertIn("bot.pool = pool;", login)
        self.assertIn("if (pool && !free)", login)
        self.assertIn("a pool bot is a free bot", login)
        # D2 in the one place the session is made: a second bot of the account is refused, and the scheduler reads the same rule.
        self.assertIn('return "another bot of that account is already logged in or loading";', login)
        account = func(MGR_CPP, "bool BotMgr::AccountHasBot(")
        self.assertIn("entry.second.session->GetAccountId() == accountId", account)
        accounts = func(MGR_CPP, "std::vector<uint32> BotMgr::BotAccounts(")
        self.assertIn("std::unique(accounts.begin(), accounts.end())", accounts)
        # The scheduler logs in through that call, and decides "bound" from pool + free (BindMaster clears free).
        pass_body = func(SCHED, "bool Pass(")
        self.assertIn("BotMgr::Instance().Login(PlayerGuidOf(guid), ObjectGuid::Empty, true, true)", pass_body)
        self.assertIn("sWorld->IsShuttingDown()", pass_body)
        states = func(SCHED, "std::vector<world::PoolBotState> StatesOf(")
        self.assertIn("BotMgr::Instance().BotAccounts()", states)
        self.assertIn("sWorld->FindSession(state.accountId)", states)
        self.assertIn("std::binary_search(botAccounts.begin(), botAccounts.end(), state.accountId)", states)
        sync = func(SCHED, "void Sync(")
        self.assertIn("BotMgr::Instance().IsPoolBot(guid)", sync)
        self.assertIn("BotMgr::Instance().IsFreeBot(guid)", sync)
        self.assertIn("world::PoolSyncBot(bot, facts, s.settings, now", sync)
        self.assertIn("BotMgr::Instance().MasterOf(guid)", sync)
        self.assertIn("player->IsInCombat()", sync)
        self.assertNotIn("IsBot(guid)", sync)
        # A bind that leaves the pool no account to refill from says so; the cap counts the accounts left to it.
        self.assertIn("UsableAccounts(s)", sync)
        self.assertIn("{} account(s) left for a target of {}", sync)

    def test_start_stop_status_are_console_commands(self):
        table = SCRIPTS[SCRIPTS.index("static ChatCommandTable poolTable"):]
        table = table[:table.index("};")]
        for word, handler in (("start", "HandlePoolStart"), ("stop", "HandlePoolStop"), ("status", "HandlePoolStatus")):
            self.assertRegex(table, rf'\{{ +"{word}", +{handler}, +rbac::RBAC_PERM_COMMAND_GM, +Console::Yes +\}}')
        self.assertIn("TortoiseBots::pool::PoolScheduler::Start()", func(SCRIPTS, "static bool HandlePoolStart("))
        self.assertIn("TortoiseBots::pool::PoolScheduler::Stop()", func(SCRIPTS, "static bool HandlePoolStop("))
        self.assertIn("TortoiseBots::pool::PoolScheduler::Status()", func(SCRIPTS, "static bool HandlePoolStatus("))
        # Stop says the spec's rate; status carries the line the live checks read.
        stop = func(SCHED, "SwitchResult Stop(")
        self.assertIn("5 per second", stop)
        self.assertIn("s.stopping = true;", stop)
        status = func(SCHED, "std::vector<std::string> Status(")
        for key in ("state={}", "target={}", "online={}", "alliance={}", "horde={}", "size={}", "due={}", "logins_60s={}", "drain={}", "last_pass_us={}"):
            self.assertIn(key, status)
        self.assertIn("capped by accounts: {}", status)
        self.assertIn("world::PoolAccountCount(states)", status)
        self.assertIn("s.settings.targetOnline > accounts", status)

    def test_the_scheduler_is_the_policy_and_writes_the_two_registry_stamps(self):
        # Every decision is the pure policy; the host translates and writes.
        for text in ("world::PlanPoolPass(in)", "world::PoolSyncBot(bot, facts, s.settings, now",
                     "world::PoolStartSession(bot, s.settings, now, rand32())", "world::PoolTrackKept(",
                     "MarkLogin(track.name, UnixNow())", "MarkLogout(track.name, UnixNow())"):
            self.assertIn(text, SCHED)
        for name in ("MarkLogin", "MarkLogout"):
            self.assertIn(f"bool {name}(std::string const& name, std::int64_t when);", REG_H)
        self.assertIn('"UPDATE `tb_pool_character` SET `{}` = {} WHERE `name` = {}"', REG)
        # The session-end logout line carries what the live check reads: reason=session and the session's length.
        self.assertIn('reason={}, session {}', SCHED)
        self.assertIn('"pool session"', SCHED)
        self.assertIn('"pool stop"', SCHED)
        self.assertIn('"pool target"', SCHED)
        # The spec's constants and the fix round's rules live in the policy.
        for text in ("kPoolLoginWindowMs = kPoolMinuteMs", "kPoolLogoutWindowMs = 1000", "kPoolLogoutsPerSecond = 5",
                     "kPoolLoginsPerPass = 1", "kPoolDeadWaitMs = 10 * kPoolMinuteMs",
                     "PoolOccupiedAccounts(", "bool inCombat", "bool shuttingDown", "PoolTrackKept("):
            self.assertIn(text, SCHED_POLICY)

    def test_the_config_keys_are_the_shipped_ones(self):
        for key, value in (("TortoiseBots.Pool.TargetOnline", 0), ("TortoiseBots.Pool.LoginsPerMinute", 20),
                           ("TortoiseBots.Pool.SessionMinutes.Min", 30), ("TortoiseBots.Pool.SessionMinutes.Max", 90),
                           ("TortoiseBots.Pool.OfflineMinutes.Min", 5), ("TortoiseBots.Pool.OfflineMinutes.Max", 20)):
            self.assertIn(f'GetIntDefault("{key}", {value})', SCHED)
            self.assertIn(f"{key} = {value}", CONF)
        self.assertIn("session per account", CONF)
        self.assertIn("capped by the number of accounts", CONF)
        load = func(SCHED, "void LoadConfig()")
        self.assertIn("if (s.settings.sessionMaxMs < s.settings.sessionMinMs)", load)
        self.assertIn("if (s.settings.offlineMaxMs < s.settings.offlineMinMs)", load)

    def test_the_level_ladder_keys_and_its_policy(self):
        # The ladder reuses the engine's four keys (read into PlayerbotAIConfig, defaults on/5/10/5), so the pool and the
        # Turtle build share one set of knobs; the pool maps them on every pass and reads the server's level cap.
        ladder_cfg = func(SCHED, "void ApplyLadderConfig(")
        self.assertIn("sPlayerbotAIConfig.levelLadder;", ladder_cfg)
        self.assertIn("sPlayerbotAIConfig.levelLadderBandSize;", ladder_cfg)
        self.assertIn("sPlayerbotAIConfig.levelLadderMaxLevelSharePct", ladder_cfg)
        self.assertIn("sPlayerbotAIConfig.levelLadderLogMinutes", ladder_cfg)
        self.assertIn("ApplyLadderConfig(s);", func(SCHED, "void LoadConfig()"))
        self.assertIn("ApplyLadderConfig(s);", func(SCHED, "bool Update("))
        # The engine reads aiplayerbot.conf lazily, on its first brain; the pool may run with none. It loads it once per
        # config load, after the world is up, before the pass reads the four keys (a failed read is not retried every pass).
        update = func(SCHED, "bool Update(")
        self.assertIn("s.ladderConfigTried = false;", func(SCHED, "void LoadConfig()"))
        self.assertIn("if (!s.ladderConfigTried)", update)
        self.assertIn("engine::ConfigReady()", update)
        self.assertIn("engine::EnsureConfig()", update)
        self.assertLess(update.index("engine::EnsureConfig()"), update.index("ApplyLadderConfig(s);"))
        self.assertIn("getIntConfig(CONFIG_MAX_PLAYER_LEVEL)", func(SCHED, "void LoadConfig()"))
        for line in ("levelLadder = config.GetBoolDefault(\"AiPlayerbot.LevelLadder\", true)",
                     "levelLadderBandSize = config.GetIntDefault(\"AiPlayerbot.LevelLadderBandSize\", 5)",
                     "levelLadderMaxLevelSharePct = config.GetIntDefault(\"AiPlayerbot.LevelLadderMaxLevelShare\", 10)",
                     "levelLadderLogMinutes = config.GetIntDefault(\"AiPlayerbot.LevelLadderLogMinutes\", 5)"):
            self.assertIn(line, CONFIG_CPP)
        # The host feeds the level from the world or the character cache, and the policy owns the band math and the pick.
        self.assertIn("sCharacterCache->GetCharacterLevelByGuid(guid)", SCHED)
        self.assertIn("player->GetLevel()", SCHED)
        for text in ("PoolLadderBandOf(", "PoolLadderBandTarget(", "PoolLadderBandName(", "PoolLadderViewOf(",
                     "PoolLadderEligible(", "PoolLadderBefore(", "levelLadderMaxLevelSharePct"):
            self.assertIn(text, SCHED_POLICY)
        self.assertIn("world::PoolLadderBandName(world::PoolLadderBandOf(bot.level", SCHED)
        self.assertIn("LadderLog(s, plan, now)", SCHED)
        for text in ("the level ladder's band math", "the ladder's pick order", "the cap's band is a hard cap",
                     "the ladder off is the round's old order", "MaxLevelShare is a hard cap"):
            self.assertIn(text, SCHED_TEST)
        # The pass cost is measured with the ladder running too: mixed levels, under the target (allowed 1).
        self.assertIn("pass cost over 2500 bots with the ladder", SCHED_TEST)
        self.assertIn("pass.logins.size() == 1", SCHED_TEST)

    def test_the_rounds_five_cases_are_in_the_policy_test(self):
        # The spec's five sentences, the fix round's cases, and the count of the pass over 2000 bots (under 0.5 ms).
        for text in ("200 due logins with LoginsPerMinute 20", "at most 20 in any 60 s",
                     "a session past its end logs out once", "a bound bot never logs out",
                     "offline less than OfflineMinutes.Min is not picked", "target lowered from 200 to 50",
                     "one session per account", "never a second character of an account",
                     "a living bot in combat is not logged out", "a pending shutdown plans no logins",
                     "a row that went away keeps the track", "2000", "average < 500.0"):
            self.assertIn(text, SCHED_TEST)
        self.assertIn("PlanPoolPass", SCHED_TEST)

    def test_population_changes_the_target_at_runtime(self):
        table = SCRIPTS[SCRIPTS.index("static ChatCommandTable poolTable"):]
        table = table[:table.index("};")]
        self.assertRegex(table, r'\{ +"population", +HandlePoolPopulation, +rbac::RBAC_PERM_COMMAND_GM, +Console::Yes +\}')
        handler = func(SCRIPTS, "static bool HandlePoolPopulation(")
        self.assertIn("TortoiseBots::pool::PoolScheduler::SetPopulation(Lower(word))", handler)
        population = func(SCHED, "SwitchResult SetPopulation(")
        self.assertIn("world::PoolResolvePopulationTarget(word, s.settings.populationLow, s.settings.populationHigh)", population)
        self.assertIn("s.settings.targetOnline = resolved.target;", population)
        self.assertIn("the pool adds or logs out bots gradually", population)
        self.assertIn("run `.tb pool start`", population)
        load = func(SCHED, "void LoadConfig()")
        self.assertIn('GetIntDefault("TortoiseBots.Pool.PopulationLow", 10)', load)
        self.assertIn('GetIntDefault("TortoiseBots.Pool.PopulationHigh", 50)', load)
        for key, value in (("TortoiseBots.Pool.PopulationLow", 10), ("TortoiseBots.Pool.PopulationHigh", 50)):
            self.assertIn(f"{key} = {value}", CONF)
        # The runtime word and its presets live in the policy and its test.
        self.assertIn("PoolResolvePopulationTarget", SCHED_POLICY)
        self.assertIn("`.tb pool population <n|low|high>`", SCHED_TEST)

    def test_the_map_cap_refuses_a_full_dungeon(self):
        # The table: dungeons 5, raids by size, nothing outside an instance, 0 switches a kind off.
        for text in ("FreeMapCapFor(bool isDungeon, bool isRaid", "dungeon = 5", "raid10 = 10",
                     "raid20 = 20", "raid40 = 40", "if (!isDungeon)", "if (!isRaid)"):
            self.assertIn(text, MAPCAP_POLICY)
        # The host: the cap is asked in the entry check, only for free bots, and counted on the target map.
        check = func(MGR_CPP, "BotMgr::EntryCheck BotMgr::CheckEntry(")
        self.assertIn("Instance().IsFreeBot(who->GetGUID())", check)
        self.assertIn("world::FreeMapCapFor(target->IsDungeon(), target->IsRaid(), target->MaxPlayers", check)
        self.assertIn("Instance().FreeBotsOnMap(mapId)", check)
        self.assertIn('"map-cap"', check)
        self.assertIn("uint32 BotMgr::FreeBotsOnMap(uint32 mapId) const", MGR_CPP)
        self.assertIn("uint32 FreeBotsOnMap(uint32 mapId) const;", MGR_H)
        self.assertIn("GetFreeMapCaps()", MGR_H)
        for key, value in (("TortoiseBots.Free.MaxPerMap.Dungeon", 5), ("TortoiseBots.Free.MaxPerMap.Raid10", 10),
                           ("TortoiseBots.Free.MaxPerMap.Raid20", 20), ("TortoiseBots.Free.MaxPerMap.Raid40", 40)):
            self.assertIn(f'GetIntDefault("{key}", {value})', MGR_CPP)
            self.assertIn(f"{key} = {value}", CONF)


class ScheduleBehaviour(unittest.TestCase):
    """The policy test as the gate runs it: g++ on the round's own test, no build tree."""

    def test_the_policy_test_passes(self):
        gxx = shutil.which("g++")
        if not gxx:
            self.skipTest("no g++")
        with tempfile.TemporaryDirectory(prefix="pool-schedule-") as tmp:
            tmp_path = pathlib.Path(tmp)
            exe = tmp_path / "test_pool_schedule_policy"
            subprocess.run([gxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", str(ROOT),
                            str(ROOT / "forever/tools/test_pool_schedule_policy.cpp"), "-o", str(exe)],
                           check=True, capture_output=True, text=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        self.assertIn("RESULT: PASS", out.stdout)

    def test_the_map_cap_policy_test_passes(self):
        gxx = shutil.which("g++")
        if not gxx:
            self.skipTest("no g++")
        with tempfile.TemporaryDirectory(prefix="map-cap-") as tmp:
            tmp_path = pathlib.Path(tmp)
            exe = tmp_path / "test_free_map_cap_policy"
            subprocess.run([gxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", str(ROOT),
                            str(ROOT / "forever/tools/test_free_map_cap_policy.cpp"), "-o", str(exe)],
                           check=True, capture_output=True, text=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        self.assertIn("RESULT: PASS", out.stdout)

    def test_the_census_test_passes(self):
        """1000 picks follow the aotc census class, race and gender shares (test_pool_census_policy.cpp)."""
        gxx = shutil.which("g++")
        if not gxx:
            self.skipTest("no g++")
        with tempfile.TemporaryDirectory(prefix="pool-census-") as tmp:
            tmp_path = pathlib.Path(tmp)
            exe = tmp_path / "test_pool_census_policy"
            subprocess.run([gxx, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", str(ROOT),
                            str(ROOT / "forever/tools/test_pool_census_policy.cpp"), "-o", str(exe)],
                           check=True, capture_output=True, text=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        self.assertIn("RESULT: PASS", out.stdout)

    def test_the_census_tables_match_the_csv(self):
        """The per-mille tables copied into PoolCompositionPolicy.h are the values in forever/data/aotc."""
        def table(name):
            m = re.search(name + r"\[\] = \{(.*?)\n\};", POLICY, re.S)
            self.assertIsNotNone(m, name)
            return {key: int(value) for key, value in re.findall(r"\{\s*(?:Class|Race)(\w+),\s*(\d+)\s*\}", m.group(1))}

        aotc = ROOT / "forever/data/aotc"
        with open(aotc / "census-classes.csv", newline="") as f:
            classes = list(csv.DictReader(f))
        totals = {"alliance": sum(int(r["alliance"]) for r in classes),
                  "horde": sum(int(r["horde"]) for r in classes)}
        for array, faction in (("kCensusAllianceClasses", "alliance"), ("kCensusHordeClasses", "horde")):
            pins = table(array)
            for row in classes:
                key = row["class"].title()
                self.assertEqual(pins[key], round(1000 * int(row[faction]) / totals[faction]), f"{array} {key}")

        race_keys = {"Human": "Human", "Night Elf": "NightElf", "Dwarf": "Dwarf", "Gnome": "Gnome",
                     "High Order Skyborne": "SkyborneAlliance", "Undead": "Undead", "Orc": "Orc",
                     "Tauren": "Tauren", "Troll": "Troll", "Windshaper Skyborne": "SkyborneHorde"}
        with open(aotc / "census-races.csv", newline="") as f:
            races = {race_keys[r["race"]]: r for r in csv.DictReader(f)}
        for array in ("kCensusAllianceRaces", "kCensusHordeRaces"):
            for key, value in table(array).items():
                self.assertEqual(value, round(10 * float(races[key]["pct_of_faction"])), f"{array} {key}")

        with open(aotc / "census-class-gender.csv", newline="") as f:
            genders = {r["class"]: r for r in csv.DictReader(f)}
        for key, value in table("kCensusFemaleShares").items():
            self.assertEqual(value, round(10 * float(genders[key.lower()]["female_pct"])), f"female {key}")


MUTATIONS = {
    # (1) the identity: the row is used whenever the account resolves, as before the stamp/name/id check.
    "identity-not-checked": ((
        "        if (check != world::PoolAccountCheck::Trusted)\n        {\n            TC_LOG_ERROR(\"tortoisebots\", \"TortoiseBots: pool account row {} (id #{}) is stale: {}.",
        "        if (false)\n        {\n            TC_LOG_ERROR(\"tortoisebots\", \"TortoiseBots: pool account row {} (id #{}) is stale: {}.",
    ), "registry"),
    # (2) the write-ahead claims: the rows are written after the irreversible step again.
    "account-claim-not-written-first": ((
        "        if (!StoreAccount(claim))\n",
        "        if (false && !StoreAccount(claim))\n",
    ), "create"),
    "character-claim-not-written-first": ((
        "    if (!StoreCharacter(pending))\n",
        "    if (false && !StoreCharacter(pending))\n",
    ), "create"),
    # The reconciliation never runs (a crash leaves the account/character unmanaged).
    "no-reconciliation": ((
        "    bool const anyClaim = std::any_of(rows.accounts.begin(), rows.accounts.end(), [](Account const& row) { return row.state == world::PoolRowState::Creating; })\n"
        "        || std::any_of(rows.characters.begin(), rows.characters.end(), [](Character const& row) { return row.state == world::PoolRowState::Creating; });\n"
        "    if (!anyClaim)",
        "    bool const anyClaim = std::any_of(rows.accounts.begin(), rows.accounts.end(), [](Account const& row) { return row.state == world::PoolRowState::Creating; })\n"
        "        || std::any_of(rows.characters.begin(), rows.characters.end(), [](Character const& row) { return row.state == world::PoolRowState::Creating; });\n"
        "    if (true)",
    ), "create"),
    # (3) the total cap: the request is no longer bounded by the pool's size.
    "cap-not-enforced": ((
        "    std::uint32_t const allowed = world::AllowedPoolCharacters(count, present, cap);",
        "    std::uint32_t const allowed = count;",
    ), "create"),
    # (4) the critical finding: a transient character-cache miss is read as "the character does not exist".
    "reconciliation-decides-from-the-cache": ((
        "        bool found = false;\n"
        "        std::uint64_t guid = 0;\n"
        "        std::uint32_t owner = 0;\n"
        "        world::PoolCharacterPresence presence = world::PoolCharacterPresence::Unanswered;\n"
        "        if (CharacterOwner(claim.name, found, guid, owner))\n"
        "        {\n"
        "            if (!found)\n"
        "                presence = world::PoolCharacterPresence::Absent;\n"
        "            else\n"
        "                presence = owner == claim.accountId ? world::PoolCharacterPresence::OnAccount : world::PoolCharacterPresence::Elsewhere;\n"
        "        }\n",
        "        bool found = !sCharacterCache->GetCharacterGuidByName(claim.name).IsEmpty();\n"
        "        std::uint64_t guid = found ? sCharacterCache->GetCharacterGuidByName(claim.name).GetCounter() : 0;\n"
        "        std::uint32_t owner = found ? sCharacterCache->GetCharacterAccountIdByName(claim.name) : 0;\n"
        "        world::PoolCharacterPresence presence = found\n"
        "            ? (owner == claim.accountId ? world::PoolCharacterPresence::OnAccount : world::PoolCharacterPresence::Elsewhere)\n"
        "            : world::PoolCharacterPresence::Absent;\n",
    ), "create"),
    # (5) the other critical half: a creation is ready as soon as its saves are queued.
    "creation-ready-when-queued": ((
        "    s.pending = Pending{ true, name, chosen.username, chosen.id, composition.race, composition.klass, gender, pending.created, 0, 0 };\n"
        "    return Step::Queued;",
        "    pending.state = world::PoolRowState::Ready;\n"
        "    UpdateCharacter(pending);\n"
        "    return Step::Made;",
    ), "create"),
    # (6) the reconciliation deletes an account again.
    "reconciliation-deletes-again": ((
        "            case world::PoolAccountAction::MarkFailed:\n"
        "                claim.state = world::PoolRowState::Failed;\n",
        "            case world::PoolAccountAction::MarkFailed:\n"
        "                AccountMgr::DeleteAccount(claim.id);\n"
        "                claim.state = world::PoolRowState::Failed;\n",
    ), "create"),
    # (7) prune no longer proves the account is the pool's.
    "prune-does-not-verify-the-account": ((
        "        if (check != world::PoolAccountCheck::Trusted)\n"
        "        {\n"
        "            report.lines.push_back(head + \": kept, \" + world::PoolAccountCheckText(check));",
        "        if (false)\n"
        "        {\n"
        "            report.lines.push_back(head + \": kept, \" + world::PoolAccountCheckText(check));",
    ), "create"),
    # (8) prune no longer proves that every character on the account is the pool's.
    "prune-ignores-foreign-characters": ((
        "        if (world::DecideAccountPrune(facts) == world::PoolPruneAction::KeepAccount)",
        "        if (false)",
    ), "create"),
    # Fix round r3 (the review's findings).
    # (r3-1) the account is trusted from the cached list again, with no database re-verification before a character.
    "account-not-reverified-before-a-character": ((
        "if (VerifyAccount(chosen.id, chosen.username, why) != world::PoolAccountCheck::Trusted)",
        "if (false)",
    ), "create"),
    # (r3-2) the cap no longer counts the characters the database holds, or the rows that have no character yet.
    "cap-ignores-the-database-count": ((
        "    if (!PoolCharactersOnAccountsCompat(ids, total))\n        return kPoolCountUnknown;",
        "    total = 0;",
    ), "registry"),
    "cap-ignores-unfinished-rows": ((
        "    for (Character const& character : characters)\n    {\n        if (character.state == world::PoolRowState::Ready)\n            continue;",
        "    for (Character const& character : characters)\n    {\n        if (true)\n            continue;",
    ), "registry"),
    "failed-auth-lookup-marks-rows-stale": ((
        "        if (check == world::PoolAccountCheck::NoAccount)\n        {\n            // The account manager gave no answer",
        "        if (false)\n        {\n            // The account manager gave no answer",
    ), "registry"),
    "reconciliation-stales-a-claim-on-an-unanswered-lookup": ((
        "        if (account && check == world::PoolAccountCheck::NoAccount && (",
        "        if (false && account && check == world::PoolAccountCheck::NoAccount && (",
    ), "create"),
    # (r3-3) an account is made before the name is settled, the core's name rules are skipped, empty accounts are not reused,
    # and the number of pool accounts is not bounded.
    "account-made-before-the-name-is-settled": ((
        "    ChosenAccount chosen;\n    Character claimed;                                  // the pool's own claim of an existing name (adoption only)\n    if (exists)\n    {",
        "    ChosenAccount chosen;\n    Character claimed;                                  // the pool's own claim of an existing name (adoption only)\n    if (false)\n    {",
    ), "create"),
    "core-name-rules-skipped": ((
        "    if (name.empty() || !CheckPlayerNameCompat(name))",
        "    if (name.empty())",
    ), "create"),
    "empty-account-not-reused": ((
        "    if (FindReusableAccount(reusable))",
        "    if (false && FindReusableAccount(reusable))",
    ), "create"),
    "account-count-not-bounded": ((
        "    if (accountRows >= accountLimit)",
        "    if (false)",
    ), "create"),
    # (r3-6) a running job reads the live cap and ignores the key being turned off.
    "job-reads-the-live-cap": ((
        "    if (present >= s.cap)",
        "    if (present >= MaxCharacters())",
    ), "create"),
    "job-ignores-the-key-turned-off": ((
        "    if (!KeyOn())",
        "    if (false)",
    ), "create"),
    # (r3-7) SQL text is not escaped any more (the fake's EscapeString really escapes).
    "core-read-name-not-escaped": ((
        "    CharacterDatabase.EscapeString(value);",
        "    (void)value;",
    ), "compat"),
    # Last fix round (codex-w0a-r4, claude-w0a-r2).
    "adopts-an-existing-character-without-its-claim": ((
        "        if (!FindCharacterRow(name, claimed) || claimed.state != world::PoolRowState::Creating || claimed.accountId != knownAccount)",
        "        if (false)",
    ), "create"),
    "prune-ignores-creating-claims": ((
        "(character.state == world::PoolRowState::Ready || character.state == world::PoolRowState::Creating))\n                inUse = true;",
        "(character.state == world::PoolRowState::Ready))\n                inUse = true;",
    ), "create"),
    "stale-rows-count-toward-the-account-cap": ((
        "[](Account const& row) { return row.state != world::PoolRowState::Stale; }));",
        "[](Account const& row) { return true; }));",
    ), "registry"),
    "a-failed-row-read-is-an-empty-table": ((
        "    if (!accountsAnswered || !charactersAnswered)",
        "    if (false)",
    ), "registry"),
    "a-half-failed-lookup-marks-the-row-stale": ((
        "    if (unanswered)\n        return world::PoolAccountCheck::NoAccount;",
        "    if (false)\n        return world::PoolAccountCheck::NoAccount;",
    ), "registry"),
    "ready-before-the-login-commit": ((
        "        if (BotMgr::Instance().CreationInFlight(p.name))",
        "        if (false)",
    ), "create"),
    "registry-sql-text-not-escaped": ((
        "        if (c == '\\'' || c == '\\\\')\n            out += '\\\\';",
        "        if (false)\n            out += '\\\\';",
    ), "registry"),
}


@unittest.skipUnless(__import__("os").environ.get("POOL_REGISTRY_MUTATE") == "1", "set POOL_REGISTRY_MUTATE=1 to compile the mutations")
class Mutations(Behaviour):
    def test_each_mutation_fails_the_check(self):
        gxx = shutil.which("g++")
        if not gxx:
            self.skipTest("no g++")
        for name, ((old, new), which) in MUTATIONS.items():
            with self.subTest(mutation=name):
                registry = (ROOT / "forever/src/host/pool/TbPoolRegistry.cpp").read_text()
                create = (ROOT / "forever/src/host/pool/TbPoolCreate.cpp").read_text()
                compat = (ROOT / "forever/src/compat/TbCompat_WL0.h").read_text()
                source = {"registry": registry, "create": create, "compat": compat}[which]
                self.assertIn(old, source, f"mutation {name} no longer applies")
                if which == "registry":
                    registry = registry.replace(old, new, 1)
                elif which == "compat":
                    compat = compat.replace(old, new, 1)
                else:
                    create = create.replace(old, new, 1)
                with tempfile.TemporaryDirectory(prefix="pool-mutate-") as tmp:
                    out = self.build_and_run(pathlib.Path(tmp), registry, create, gxx, compat)
                self.assertNotEqual(out.returncode, 0, f"mutation {name} was NOT caught:\n{out.stdout}")
                self.assertIn("FAIL", out.stdout)


# Each mutation restores a defect of the scheduler's policy; the policy test has to catch it. The header is copied alone into a
# scratch tree and compiled with the round's own test, so a defect that only the host would notice is not in scope here.
SCHEDULE_MUTATIONS = {
    # (1) the 60 s login window: every pass may log in again.
    "login-window-not-enforced": (
        "std::uint32_t const inWindow = PoolRecentCount(recentLogins, in.nowMs, kPoolLoginWindowMs);",
        "std::uint32_t const inWindow = 0u;",
    ),
    # (2) the difference between the two "once" rules: alternation, and no second logout.
    "login-not-alternating": (
        "out.nextFaction = OtherFaction(best->faction);",
        "out.nextFaction = best->faction;",
    ),
    "oldest-offline-last": (
        "return a.lastLogoutMs < b.lastLogoutMs;",
        "return a.lastLogoutMs > b.lastLogoutMs;",
    ),
    "session-end-ignored": (
        "if (bot.bound || bot.phase != PoolPhase::Online || !bot.sessionEndsMs || in.nowMs < bot.sessionEndsMs)",
        "if (false)",
    ),
    "no-second-logout-guard": (
        "if (bot.bound || bot.phase != PoolPhase::Online || !bot.sessionEndsMs || in.nowMs < bot.sessionEndsMs)",
        "if (bot.bound || bot.phase == PoolPhase::Offline || !bot.sessionEndsMs || in.nowMs < bot.sessionEndsMs)",
    ),
    # (3) a bot that got a master.
    "bound-bot-drained": (
        "            if (!bot.bound && (bot.phase == PoolPhase::Online || bot.phase == PoolPhase::Loading))\n"
        "                candidates.push_back(&bot);",
        "            if (bot.phase == PoolPhase::Online || bot.phase == PoolPhase::Loading)\n"
        "                candidates.push_back(&bot);",
    ),
    "bound-bot-counted": (
        "if (bot.bound || bot.phase == PoolPhase::Offline)\n            continue;",
        "if (bot.phase == PoolPhase::Offline)\n            continue;",
    ),
    # (4) the offline time and the account.
    "offline-window-ignored": (
        "return !bot.bound && bot.phase == PoolPhase::Offline && !PoolAccountBusy(bot, occupiedAccounts) && nowMs >= bot.offlineUntilMs;",
        "return !bot.bound && bot.phase == PoolPhase::Offline && !PoolAccountBusy(bot, occupiedAccounts);",
    ),
    "account-online-ignored": (
        "return !bot.bound && bot.phase == PoolPhase::Offline && !PoolAccountBusy(bot, occupiedAccounts) && nowMs >= bot.offlineUntilMs;",
        "return !bot.bound && bot.phase == PoolPhase::Offline && nowMs >= bot.offlineUntilMs;",
    ),
    # The account's own occupation, not only the host's word.
    "accounts-not-shared": (
        "if (bot.accountId && (bot.accountOnline || bot.phase != PoolPhase::Offline))",
        "if (bot.accountOnline)",
    ),
    # A fight and a pending shutdown.
    "combat-ignored": (
        "        else if (bot.inCombat)\n            continue;",
        "        else if (false)\n            continue;",
    ),
    "target-drain-takes-fighters": (
        "            if (!bot.alive || bot.inCombat)\n                continue;",
        "            if (false)\n                continue;",
    ),
    "shutdown-logins": (
        "    if (in.shuttingDown)\n        allowed = 0;",
        "    if (false)\n        allowed = 0;",
    ),
    # The bound flag on the switch to Online, its clearing when the bot is free again, and its clearing when the bot leaves.
    "bound-not-seen-when-online": (
        "                PoolSyncMaster(bot, facts, settings, now, roll, out);\n            }\n            break;",
        "            }\n            break;",
    ),
    "bound-not-cleared-on-free": (
        "        bot.bound = false;\n        PoolStartSession(bot, settings, now, roll());",
        "        PoolStartSession(bot, settings, now, roll());",
    ),
    "bound-not-cleared-when-binding-ends": (
        "bool const shouldBind = !facts.free && facts.hasMaster;",
        "bool const shouldBind = !facts.free;",
    ),
    "bound-kept-after-logout": (
        "    bot.phase = PoolPhase::Offline;\n    bot.bound = false;",
        "    bot.phase = PoolPhase::Offline;",
    ),
    # A row that went away must not drop an online bot's track.
    "track-dropped-while-online": (
        "return rowPresent || bot.phase != PoolPhase::Offline;",
        "return rowPresent;",
    ),
    # (5) the order of a lowered target.
    "target-lowering-oldest-first": (
        "return a.sessionStartMs > b.sessionStartMs;",
        "return a.sessionStartMs < b.sessionStartMs;",
    ),
    # The corpse's ten minutes, the rolled range, the stop's 5 per second.
    "dead-logged-out-at-once": (
        "            if (in.nowMs - bot.sessionEndsMs < kPoolDeadWaitMs)",
        "            if (false)",
    ),
    "session-length-ignores-max": (
        "std::uint64_t const offset = (std::uint64_t(roll) * span) >> 32;",
        "std::uint64_t const offset = 0;",
    ),
    "stop-not-paced": (
        "        for (PoolBotState const* bot : candidates)\n"
        "        {\n"
        "            if (out.logouts.size() >= allowancePerSecond)\n"
        "                break;",
        "        for (PoolBotState const* bot : candidates)\n"
        "        {\n"
        "            if (false)\n"
        "                break;",
    ),
}


@unittest.skipUnless(__import__("os").environ.get("POOL_SCHEDULE_MUTATE") == "1", "set POOL_SCHEDULE_MUTATE=1 to compile the mutations")
class ScheduleMutations(unittest.TestCase):
    def build_and_run(self, policy, gxx):
        with tempfile.TemporaryDirectory(prefix="pool-schedule-mutate-") as tmp:
            tmp_path = pathlib.Path(tmp)
            (tmp_path / "ai/playerbot/policy/world").mkdir(parents=True)
            (tmp_path / "ai/playerbot/policy/world/PoolSchedulePolicy.h").write_text(policy)
            exe = tmp_path / "test_pool_schedule_policy"
            subprocess.run([gxx, "-std=c++17", "-O1", "-I", str(tmp_path),
                            str(ROOT / "forever/tools/test_pool_schedule_policy.cpp"), "-o", str(exe)],
                           check=True, capture_output=True, text=True)
            return subprocess.run([str(exe)], capture_output=True, text=True)

    def test_each_mutation_fails_the_policy_test(self):
        gxx = shutil.which("g++")
        if not gxx:
            self.skipTest("no g++")
        baseline = self.build_and_run(SCHED_POLICY, gxx)
        self.assertEqual(baseline.returncode, 0, baseline.stdout + baseline.stderr)
        self.assertIn("RESULT: PASS", baseline.stdout)
        for name, (old, new) in SCHEDULE_MUTATIONS.items():
            with self.subTest(mutation=name):
                self.assertIn(old, SCHED_POLICY, f"mutation {name} no longer applies")
                out = self.build_and_run(SCHED_POLICY.replace(old, new, 1), gxx)
                self.assertNotEqual(out.returncode, 0, f"mutation {name} was NOT caught:\n{out.stdout}")
                self.assertIn("FAIL", out.stdout)


if __name__ == "__main__":
    unittest.main()
