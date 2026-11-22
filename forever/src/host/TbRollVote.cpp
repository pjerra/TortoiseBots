#include "host/TbRollVote.h"

#include "host/TbRollVotePure.h"

int TbRollVoteMaskFallback(int vote, std::uint32_t voteMask)
{
    return tbrollvote::FallbackVote(vote, voteMask);
}
