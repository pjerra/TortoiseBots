#!/bin/sh
# Checks for the ForeverLua test toolkit, no server needed: syntax (luac -p, Lua 5.4), luacheck when installed, and the
# mock test (fake Cmd/World/Timer/Json and handles, the real tt/*.lua). Run from anywhere: bash forever/lua/check.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
cd "${ROOT}"

rc=0
# The toolkit is written for Lua 5.4 (math.type, bitwise operators). A 5.1/5.2 `lua` or `luac` is not a substitute: it
# would reject valid files and pass code 5.4 refuses, so both steps need the real 5.4 tool.
LUA54=""
if command -v lua5.4 >/dev/null 2>&1; then
    LUA54=lua5.4
elif command -v lua >/dev/null 2>&1 && lua -v 2>&1 | grep -q '^Lua 5\.4'; then
    LUA54=lua
fi
LUAC54=""
if command -v luac5.4 >/dev/null 2>&1; then
    LUAC54=luac5.4
elif command -v luac >/dev/null 2>&1 && luac -v 2>&1 | grep -q '^Lua 5\.4'; then
    LUAC54=luac
fi

if [ -n "${LUAC54}" ]; then
    for f in "${HERE}"/tt/*.lua "${HERE}"/tests/*.lua; do
        "${LUAC54}" -p "${f}" || { echo "FAIL ${LUAC54} ${f}"; rc=1; }
    done
else
    echo "SKIPPED: the syntax check needs luac 5.4"
fi

if command -v luacheck >/dev/null 2>&1; then
    luacheck --config "${HERE}/.luacheckrc" "${HERE}"/tt/*.lua "${HERE}"/tests/*.lua || rc=1
else
    echo "SKIPPED: luacheck is not installed"
fi

if [ -n "${LUA54}" ]; then
    "${LUA54}" forever/lua/tests/mock_tt_test.lua || rc=1
else
    echo "SKIPPED: the mock test needs a Lua 5.4 interpreter"
fi

exit ${rc}
