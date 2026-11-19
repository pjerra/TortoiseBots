"""Compile production formatting, packet-prefix parsing and per-boss throttle."""
import csv
import io
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class RaidBossLoggingTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_rows_packets_and_throttle(self):
        root = Path(__file__).resolve().parents[1]
        cpp = r'''
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
 std::vector<ThreatEntry> entries={{7,"Low",1},{2,"TieB",100},{1,"TieA",100},
   {3,"Three",90},{4,"Four",80},{5,"Five",70},{6,"Six",60},
   {8,"NaN",std::numeric_limits<float>::quiet_NaN()}};
 auto threat=Threat(16028,533,42,9,"Tank One",entries);
 assert(threat=="entry=16028|map=533|instance=42|boss_guid=9|victim=Tank%20One|top=5|t1=TieA:100.00:1|t2=TieB:100.00:2|t3=Three:90.00:3|t4=Four:80.00:4|t5=Five:70.00:5");
 assert(Threat(16028,533,43,9,"none",{}).find("|top=0")!=std::string::npos);
 std::cout<<Row("2026-10-13 00:00:00","Patchwerk","BossThreat","\"POINT(1.00 2.00)\",",0,0,63,"Patchwerk",threat)<<'\n';
 std::cout<<Row("2026-10-13 00:00:00","Tank,\"One\"","BossSpellHit","\"POINT(1.00 2.00)\",",1,1,60,"Patchwerk",hit)<<'\n';
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source, exe = Path(tmp)/'test.cpp', Path(tmp)/'test'
            source.write_text(cpp)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(root),str(source),'-o',str(exe)],check=True)
            result = subprocess.run([str(exe)],check=True,text=True,capture_output=True)
        rows = list(csv.reader(io.StringIO(result.stdout)))
        self.assertEqual([len(row) for row in rows], [9,9])
        self.assertEqual(rows[0][2], 'BossThreat')
        self.assertEqual(rows[1][1], 'Tank,"One"')
        self.assertEqual(rows[1][2], 'BossSpellHit')
        self.assertIn('hp_sample_at=packet_send', rows[1][8])
        self.assertNotIn('hp_exact', rows[1][8])
        # Ensure fixture-covered helpers are on the live module paths.
        adapter = (root/'host/RaidBossDiagnostics.cpp').read_text()
        self.assertIn('clock.throttle.Due(diff)', adapter)
        self.assertIn('RaidBossLog::VictimCopy', adapter)
        self.assertIn('RaidBossLog::Row', adapter)
        self.assertEqual(adapter.count('!sPlayerbotAIConfig.raidBossDiagnostics'), 2)
        config = (root/'ai/playerbot/PlayerbotAIConfig.cpp').read_text()
        self.assertIn('GetBoolDefault("AiPlayerbot.RaidBossDiagnostics", false)', config)
        template = (root/'ai/playerbot/aiplayerbot.conf.dist.in').read_text()
        self.assertIn('AiPlayerbot.RaidBossDiagnostics = 0', template)
        packet = (root/'host/BotPacketAdapter.cpp').read_text()
        self.assertLess(packet.index('RaidBossDiagnostics::ObservePacket'), packet.index('if (IsHeadlessCompat(session))'))


if __name__ == '__main__':
    unittest.main()
