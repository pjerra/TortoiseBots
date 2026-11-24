// The module's ForeverLua bot provider (TBF-B1, SPEC-forever-lua-bot-api.md section 7). Register() is called once from
// AddSC_tortoisebots when both modules are built; Shutdown() from TBF's WorldScript::OnShutdown. Without the engine the
// implementations are not compiled and both calls are compiled out at the call sites.
#pragma once

class ObjectGuid;

namespace TortoiseBots
{
namespace luabots
{
// Hands the engine the module's provider (static lifetime, never unregistered). Available only when the module is built
// with TORTOISEBOTS_SLICE1 and TBF_WITH_FOREVER_LUA.
void Register();
// TortoiseBots.Enable changed (the world thread calls it after every config load): available() answers true only while the
// module is enabled. A shutdown is final: after Shutdown() the provider never becomes available again.
void SetEnabled(bool enabled);
// After the world shutdown the provider answers available() == false; the engine empties its snapshot on the next tick.
void Shutdown();
// A level or talent change: the bot's cached role/spec words are recomputed on the next snapshot (host/TbLuaBots.cpp).
void Invalidate(ObjectGuid const& bot);
}  // namespace luabots
}  // namespace TortoiseBots
