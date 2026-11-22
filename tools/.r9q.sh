python3 forever/tools/wc_plan_rows.py --tsv /tmp/wc_rows.tsv >/dev/null
g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_run_wc.cpp -o /tmp/a 2>&1 | grep " error"; /tmp/a /tmp/wc_rows.tsv | tail -1
g++ -std=c++17 -Wall -Wextra tools/test_run_wc_r9_policy.cpp -o /tmp/b 2>&1 | grep " error"; /tmp/b
g++ -std=c++17 -O1 -Wall -Wextra tools/test_run_leader_sentinel_policy.cpp -o /tmp/c 2>&1 | grep " error"; /tmp/c
g++ -std=c++17 -O1 tools/replay_decisions.cpp -o /tmp/d 2>&1|head -3; /tmp/d run tools/fixtures/decisions/synthetic-pull.json | tail -1
python3 tools/gen_decision_capture.py --check && echo gen-check-ok
