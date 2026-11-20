"""Run production snapshot loading and raid enter/leave predicates with core doubles."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class RaidStrategyRestore(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_saved_snapshot_and_arrival(self):
        root = pathlib.Path(os.environ.get('TBOTS_TEST_SOURCE', pathlib.Path(__file__).resolve().parents[1]))
        code = r'''
#include <array>
#include <cassert>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "ai/playerbot/MovementModePolicy.h"
using namespace ai;
using uint64 = unsigned long long;
namespace BotState { enum State {BOT_STATE_COMBAT, BOT_STATE_NON_COMBAT, BOT_STATE_DEAD, BOT_STATE_REACTION}; }
struct Guid { uint64 GetRawValue() { return 1; } };
struct Map { unsigned id=0; bool instance=false; bool IsDungeon(){return instance;} bool IsRaid(){return instance;} unsigned GetId(){return id;} };
struct Player {
 Map map; bool world=true, teleport=false;
 Guid getObjectGuid(){return {};}
 bool InBattleGround(){return false;}
 bool IsInWorld(){return world;}
 bool IsBeingTeleported(){return teleport;}
 Map* GetMap(){return &map;}
 unsigned GetMapId(){return map.id;}
};
struct Context {void Load(std::list<std::string> const&){} };
struct PlayerbotAI {
 Player bot; Context context;
 std::array<std::set<std::string>,4> strategies;
 PlayerbotAI(){for(int state:{0,1})strategies[state]={"dungeon","follow"};}
 Player* GetBot(){return &bot;}
 Context* GetAiObjectContext(){return &context;}
 void EndPullWindow(const char* = nullptr){} void ReleasePullHold(){}
 bool HasRealPlayerMaster(){return true;}
 static bool IsTank(Player*){return false;}
 void ClearStrategies(int state){strategies[state].clear();}
 bool HasStrategy(std::string const& s,int state){return strategies[state].count(s);}
 void ChangeStrategy(std::string const& text,int state){
  std::istringstream input(text); std::string token;
  while(std::getline(input,token,',')){
   if(token.empty())continue;
   if(token[0]=='+')strategies[state].insert(token.substr(1));
   else if(token[0]=='-')strategies[state].erase(token.substr(1));
   else std::abort();
  }
 }
};
namespace TortoiseBots {
namespace BotCommands {
 bool IsInCommandedGroup(Player*){return false;}
 void NoteCommanderThreat(Player*){}
 void HoldCommanderAutonomy(Player*,PlayerbotAI*,bool){}
}
namespace RemoteCommander {bool KeepsCommandedThreatBackoff(bool,bool){return false;}}
}
struct Field {std::string value;std::string GetString(){return value;}};
using Row=std::array<Field,2>;
struct Result {
 std::vector<Row> rows; size_t pos=0;
 Field* Fetch(){return rows[pos].data();}
 bool NextRow(){return ++pos<rows.size();}
};
struct Database {
 std::vector<Row> rows;
 void escape_string(std::string&){}
 std::shared_ptr<Result> PQuery(char const*,uint64,char const*){
  return rows.empty()?nullptr:std::make_shared<Result>(Result{rows});
 }
} CharacterDatabase;
// the Turtle bodies of the pass-3 helpers (ai/cmangos-compat-w3.h, ai/cmangos-compat-unported.h)
template <class Db> inline void DbEscapeW3(Db& db, std::string& text) { db.escape_string(text); }
template <class... Args>
inline std::shared_ptr<Result> CharacterPQueryCompat(char const* format, Args... args) { return CharacterDatabase.PQuery(format, args...); }
struct PlayerbotDbStore {void Load(PlayerbotAI*,std::string);};
struct EnterDungeonTrigger {
 PlayerbotAI* ai; Player* bot; std::string dungeonStrategy; unsigned mapID;
 bool IsActive();
};
struct LeaveDungeonTrigger {
 PlayerbotAI* ai; Player* bot; std::string dungeonStrategy; unsigned mapID;
 bool IsActive();
};
'''
        code += function((root/'ai/playerbot/PlayerbotDbStore.cpp').read_text(), 'void PlayerbotDbStore::Load(')
        triggers = (root/'ai/playerbot/strategy/triggers/DungeonTriggers.cpp').read_text()
        code += function(triggers, 'bool EnterDungeonTrigger::IsActive()')
        code += function(triggers, 'bool LeaveDungeonTrigger::IsActive()')
        code += r'''
int main(){
 int failures=0;
 auto check=[&](bool ok,char const* label){if(!ok){++failures;std::cerr<<"FAIL "<<label<<"\n";}};
 PlayerbotDbStore store;
 auto row=[](char const* key,char const* value){return Row{Field{key},Field{value}};};
 for(int active:{0,1})for(unsigned map:{249u,409u,469u,533u,531u,509u,309u}){
  PlayerbotAI ai;
  CharacterDatabase.rows={row("co","+chat,+dps assist"),row("nc","+chat,+follow")};
  store.Load(&ai,"");
  check(ai.HasStrategy("dungeon",active),"old snapshot must retain transition strategy in each active engine");
  ai.bot.map={map,true};ai.bot.teleport=true;
  EnterDungeonTrigger enter{&ai,&ai.bot,"raid",map};
  check(!enter.IsActive(),"no activation before teleport ACK");
  ai.bot.teleport=false;ai.bot.world=false;
  check(!enter.IsActive(),"no activation before world insertion");
  ai.bot.world=true;
  // Engine dispatch requires the parent strategy; actual production predicate
  // then detects the destination, regardless of how the player arrived.
  bool scheduled=ai.HasStrategy("dungeon",active)&&enter.IsActive();
  check(scheduled,"summoned arrival or immediate combat schedules raid activation");
  if(scheduled)for(int state:{0,1,2,3})ai.ChangeStrategy("+raid",state);
  check(!enter.IsActive(),"already enabled raid is idempotent");
  LeaveDungeonTrigger leave{&ai,&ai.bot,"raid",map};
  ai.bot.map={0,false};
  check(ai.HasStrategy("dungeon",1)&&leave.IsActive(),"leaving raid schedules removal");
 }
 for(auto rows:std::vector<std::vector<Row>>{{},{row("value","x")},{row("co","+chat")},{row("nc","+stay")}}){
  PlayerbotAI ai;CharacterDatabase.rows=rows;store.Load(&ai,"");
  check(ai.HasStrategy("dungeon",0)&&ai.HasStrategy("dungeon",1),"no rows, values only, and partial snapshots retain detectors");
 }
 {
  PlayerbotAI ai;CharacterDatabase.rows={row("co","+chat,-dungeon"),row("nc","+stay,-dungeon")};store.Load(&ai,"");
  check(!ai.HasStrategy("dungeon",0)&&!ai.HasStrategy("dungeon",1),"explicit saved opt-out wins");
  check(ai.HasStrategy("stay",1)&&!ai.HasStrategy("follow",1),"saved movement order unchanged");
 }
 {
  PlayerbotAI ai;CharacterDatabase.rows={row("co","+dungeon,+onyxia's lair"),row("nc","+dungeon,+stay")};store.Load(&ai,"");
  check(ai.HasStrategy("onyxia's lair",0)&&ai.HasStrategy("stay",1),"current snapshots remain intact");
  EnterDungeonTrigger enter{&ai,&ai.bot,"molten core",409};
  check(!enter.IsActive(),"outdoor map never enables a raid");
 }
 if(!failures)std::cout<<"PASS snapshot restore, arrival gates, seven raids, both engines, exit, opt-out and controls\n";
 return failures?1:0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = pathlib.Path(tmp)/'test.cpp', pathlib.Path(tmp)/'test'
            src.write_text(code)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-I',str(root),str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    unittest.main()
