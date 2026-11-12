"""Compile production formatting, packet-prefix parsing and per-boss throttle."""
import csv
import io
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

CPP = r'''
#include "runtime/policy/infra/RaidBossLogPolicy.h"
#include <cassert>
#include <limits>
#include <iostream>
using namespace TortoiseBots::RaidBossLog;
int main(){
 Throttle bossA,bossB;
 assert(bossA.Due(1));assert(bossB.Due(1));
 assert(!bossA.Due(1999));assert(bossA.Due(1));assert(!bossA.Due(0));
 assert(bossB.Due(2000));assert(bossA.Due(9000));assert(!bossA.Due(1));
 assert(bossA.Due(std::numeric_limits<uint32_t>::max()));assert(!bossA.Due(1));
 bossA=Throttle{};assert(bossA.Due(0));
 std::vector<uint8_t> packet;
 auto packed=[&](uint64_t guid){
   uint8_t mask=0;for(unsigned i=0;i<8;++i)if((guid>>(8*i))&255)mask|=1<<i;
   packet.push_back(mask);for(unsigned i=0;i<8;++i)if(mask&(1<<i))packet.push_back(guid>>(8*i));
 };
 auto u32=[&](uint32_t n){for(unsigned i=0;i<4;++i)packet.push_back(n>>(8*i));};
 packed(0x100002);packed(0xf13000003ea00001ULL);u32(28308);u32(6500);
 packet.resize(packet.size()+20);Damage damage;
 assert(ParseDamage(packet.data(),packet.size(),damage));
 assert(damage.victim==0x100002 && damage.caster==0xf13000003ea00001ULL);
 assert(damage.spell==28308 && damage.amount==6500);
 for(size_t n=0;n<packet.size();++n){Damage truncated;assert(!ParseDamage(packet.data(),n,truncated));}
 assert(VictimCopy(0x100002,damage));assert(!VictimCopy(7,damage));
 // Identical consecutive hits are both valid; no timestamp/content deduplication.
 assert(VictimCopy(0x100002,damage));
 auto hit=Hit(16028,533,42,damage,"Patchwerk","Tank,\"One\"",6000);
 assert(hit.find("|reported_damage=6500|hp_sample=6000|hp_est=0|hp_sample_at=packet_send")!=std::string::npos);
 assert(hit.find("victim=Tank%2C%22One%22")!=std::string::npos);
 assert(Hit(16028,533,42,damage,"Patchwerk","Tank",8000).find("hp_est=1500")!=std::string::npos);
 damage.amount=0;assert(Hit(16028,533,42,damage,"Patchwerk","Tank",8000).find("hp_est=8000")!=std::string::npos);
 // ParseHeal: SMSG_SPELLHEALLOG carries two packed guids and nine fixed bytes, not the damage tail.
 packet.clear();packed(0x100002);packed(0x100007);u32(25314);u32(2400);packet.push_back(1);
 Heal heal;
 assert(ParseHeal(packet.data(),packet.size(),heal));
 assert(heal.target==0x100002 && heal.caster==0x100007);
 assert(heal.spell==25314 && heal.amount==2400 && heal.crit);
 for(size_t n=0;n<packet.size();++n){Heal truncated;assert(!ParseHeal(packet.data(),n,truncated));}
 assert(TargetCopy(0x100002,heal));assert(!TargetCopy(7,heal));
 auto healRow=HealRow(249,9034,heal,"Healer","Tank",3000,9000);
 assert(healRow.find("map=249|instance=9034|healer=Healer|healer_guid=1048583|spell=25314|target=Tank")!=std::string::npos);
 assert(healRow.find("|amount=2400|crit=1|hp_sample=3000|hp_max=9000|hp_sample_at=packet_send")!=std::string::npos);
 packet.back()=0;Heal plain;assert(ParseHeal(packet.data(),packet.size(),plain) && !plain.crit);
 std::vector<ThreatEntry> entries={{7,"Low",1},{2,"TieB",100},{1,"TieA",100},
   {3,"Three",90},{4,"Four",80},{5,"Five",70},{6,"Six",60},
   {8,"NaN",std::numeric_limits<float>::quiet_NaN()}};
 auto threat=Threat(16028,533,42,9,"Tank One",entries);
 assert(threat=="entry=16028|map=533|instance=42|boss_guid=9|victim=Tank%20One|top=5|t1=TieA:100.00:1|t2=TieB:100.00:2|t3=Three:90.00:3|t4=Four:80.00:4|t5=Five:70.00:5");
 assert(Threat(16028,533,43,9,"none",{}).find("|top=0")!=std::string::npos);
 std::cout<<Row("2026-10-13 00:00:00","Patchwerk","BossThreat","\"POINT(1.00 2.00)\",",0,0,63,"Patchwerk",threat)<<'\n';
 std::cout<<Row("2026-10-13 00:00:00","Tank,\"One\"","BossSpellHit","\"POINT(1.00 2.00)\",",1,1,60,"Patchwerk",hit)<<'\n';
 std::cout<<Row("2026-10-13 00:00:00","Tank","BossHeal","\"POINT(1.00 2.00)\",",1,1,60,"Healer",healRow)<<'\n';
}
'''

_RUN = None


def run_fixture():
    """Compile the policy fixture once; every case asserts on the same run."""
    global _RUN
    if _RUN is None:
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as tmp:
            source, exe = Path(tmp)/'test.cpp', Path(tmp)/'test'
            source.write_text(CPP)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(root),str(source),'-o',str(exe)],check=True)
            result = subprocess.run([str(exe)],check=True,text=True,capture_output=True)
        _RUN = (result.stdout, root)
    return _RUN


class RaidBossLoggingTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_rows_packets_and_throttle(self):
        stdout, _ = run_fixture()
        rows = list(csv.reader(io.StringIO(stdout)))
        self.assertEqual([len(row) for row in rows], [9,9,9])
        self.assertEqual(rows[0][2], 'BossThreat')
        self.assertEqual(rows[1][1], 'Tank,"One"')
        self.assertEqual(rows[1][2], 'BossSpellHit')
        self.assertIn('hp_sample_at=packet_send', rows[1][8])
        self.assertNotIn('hp_exact', rows[1][8])

    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_parse_heal_packet(self):
        # Fails on the old header: no ParseHeal exists, and an SMSG_SPELLHEALLOG body has nine bytes after the guids.
        stdout, root = run_fixture()
        rows = list(csv.reader(io.StringIO(stdout)))
        self.assertEqual(rows[2][2], 'BossHeal')
        self.assertEqual(rows[2][7], 'Healer')
        self.assertIn('|amount=2400|crit=1|hp_sample=3000|hp_max=9000|', rows[2][8])
        self.assertIn('|healer_guid=1048583|spell=25314|target=Tank|target_guid=1048578|', rows[2][8])
        # Ensure the helpers are on the live module paths.
        adapter = (root/'host/RaidBossDiagnostics.cpp').read_text()
        self.assertIn('SMSG_SPELLHEALLOG', adapter)
        self.assertIn('RaidBossLog::ParseHeal', adapter)
        self.assertIn('RaidBossLog::TargetCopy', adapter)
        self.assertIn('RaidBossLog::HealRow', adapter)
        self.assertIn('EngagedBossRecent(', adapter)
        self.assertIn('if (!EngagedBossRecent(victim->GetMapId(), victim->GetInstanceId(), WorldTimer::getMSTime())) return;', adapter)
        self.assertIn('NoteEngagedBoss(', adapter)
        self.assertEqual(adapter.count('!sPlayerbotAIConfig.raidBossDiagnostics'), 2)
        config = (root/'ai/playerbot/PlayerbotAIConfig.cpp').read_text()
        self.assertIn('GetBoolDefault("AiPlayerbot.RaidBossDiagnostics", false)', config)
        template = (root/'ai/playerbot/aiplayerbot.conf.dist.in').read_text()
        self.assertIn('AiPlayerbot.RaidBossDiagnostics = 0', template)
        packet = (root/'host/BotPacketAdapter.cpp').read_text()
        self.assertLess(packet.index('RaidBossDiagnostics::ObservePacket'), packet.index('if (session->IsHeadless())'))


if __name__ == '__main__':
    unittest.main()
