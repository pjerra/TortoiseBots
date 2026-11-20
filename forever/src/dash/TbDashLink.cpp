// The local sender shim: see TbDashLink.h. Built with TORTOISEBOTS_SLICE1 only.
#include "dash/TbDashLink.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "Config.h"
#include "Log.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include <ctime>
#include <memory>

namespace TortoiseBots
{
namespace DashRuns
{
namespace link
{
namespace
{
struct Shim
{
    bool enabled = false;
    uint64 session = 0;
    uint64 seq = 0;
    boost::asio::io_context io;
    std::unique_ptr<boost::asio::ip::udp::socket> socket;
    boost::asio::ip::udp::endpoint target;
};

Shim& Get()
{
    static Shim shim;
    return shim;
}
}

void Load()
{
    Shim& s = Get();
    s.enabled = sConfigMgr->GetBoolDefault("TortoiseBots.Dashboard.Enable", true);
    s.socket.reset();
    if (!s.enabled)
        return;
    std::string const host = sConfigMgr->GetStringDefault("TortoiseBots.Dashboard.Host", "127.0.0.1");
    int32 const port = sConfigMgr->GetIntDefault("TortoiseBots.Dashboard.Port", 9195);
    boost::system::error_code ec;
    boost::asio::ip::address const address = boost::asio::ip::make_address(host, ec);
    if (ec)
    {
        s.enabled = false;
        return;
    }
    s.target = boost::asio::ip::udp::endpoint(address, uint16(port));
    s.socket = std::make_unique<boost::asio::ip::udp::socket>(s.io);
    s.socket->open(s.target.protocol(), ec);
    if (!ec)
        s.socket->non_blocking(true, ec);
    if (ec)
    {
        s.socket.reset();
        s.enabled = false;
        return;
    }
    if (!s.session)
        s.session = uint64(time(nullptr));
}

bool Active()
{
    Shim const& s = Get();
    return s.enabled && s.socket != nullptr;
}

uint64 Session()
{
    return Get().session;
}

uint64 NextSeq()
{
    return ++Get().seq;
}

void Send(std::string const& datagram)
{
    Shim& s = Get();
    if (!s.socket)
        return;
    boost::system::error_code ec;
    s.socket->send_to(boost::asio::buffer(datagram), s.target, 0, ec);
}
}
}
}

#endif
