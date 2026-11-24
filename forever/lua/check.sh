#!/bin/sh
# Checks for the ForeverLua test toolkit, no server needed: syntax (luac -p, Lua 5.4), luacheck when installed, and the
# mock test (fake Cmd/World/Timer/Json and handles, the real tt/*.lua). Run from anywhere: bash forever/lua/check.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
cd "${ROOT}"

rc=0
for f in "${HERE}"/tt/*.lua "${HERE}"/tests/*.lua; do
    if command -v luac5.4 >/dev/null 2>&1; then
        luac5.4 -p "${f}" || { echo "FAIL luac5.4 ${f}"; rc=1; }
    elif command -v luac >/dev/null 2>&1; then
        luac -p "${f}" || { echo "FAIL luac ${f}"; rc=1; }
    fi
done

if command -v luacheck >/dev/null 2>&1; then
    luacheck --config "${HERE}/.luacheckrc" "${HERE}"/tt/*.lua "${HERE}"/tests/*.lua || rc=1
else
    echo "SKIPPED: luacheck is not installed"
fi

if command -v lua5.4 >/dev/null 2>&1; then
    lua5.4 forever/lua/tests/mock_tt_test.lua || rc=1
elif command -v lua >/dev/null 2>&1; then
    lua forever/lua/tests/mock_tt_test.lua || rc=1
else
    echo "SKIPPED: no lua interpreter on PATH"
fi

exit ${rc}
