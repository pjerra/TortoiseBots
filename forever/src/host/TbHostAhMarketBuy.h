#pragma once

// One automatic auction-house buyer pass: at most one chance roll, one house page and a bounded number of
// item probes, with a free bot that already stands at a faction auctioneer. Purchases go through the existing
// `ah bid` action (AhBidAction applies the price ceiling and budget and skips own listings). Called by the seller
// sweep's pass (TbHostAhMarket.cpp), so both are paced by the same market cycle and the same World.Auction gate.

namespace tbhost
{
namespace ah_market
{

void RunBuyStep();

} // namespace ah_market
} // namespace tbhost
