#pragma once

// The dashboard's UDP destination (TortoiseBots.Dashboard.Host / Port): an address literal or a host name such as the
// dashboard container's. A name's IPv4 answer wins (the daemon listens on 127.0.0.1 by default); without one the first IPv6
// answer is used. Resolving a name is a DNS lookup, so it runs at config load, never per datagram.
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include <string>

namespace TortoiseBots::dash
{
inline bool ResolveEndpoint(std::string const& host, unsigned short port, boost::asio::ip::udp::endpoint& endpoint, std::string& error)
{
    boost::system::error_code ec;
    boost::asio::ip::address const literal = boost::asio::ip::make_address(host, ec);
    if (!ec)
    {
        endpoint = boost::asio::ip::udp::endpoint(literal, port);
        return true;
    }

    boost::asio::io_context io;
    boost::asio::ip::udp::resolver resolver(io);
    auto const results = resolver.resolve(host, std::to_string(port), ec);
    if (ec || results.empty())
    {
        error = ec ? ec.message() : "no address";
        return false;
    }

    boost::asio::ip::udp::endpoint fallback;
    bool haveFallback = false;
    for (auto const& result : results)
    {
        if (result.endpoint().address().is_v4())
        {
            endpoint = result.endpoint();
            return true;
        }
        if (!haveFallback)
        {
            fallback = result.endpoint();
            haveFallback = true;
        }
    }
    endpoint = fallback;
    return true;
}
}
