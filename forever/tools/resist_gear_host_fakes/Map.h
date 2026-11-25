// The one map question the resist-gear host asks: is this a raid (where the swap arms).
#pragma once

class Map
{
public:
    explicit Map(bool raid) : _raid(raid) {}
    bool IsRaid() const { return _raid; }

private:
    bool _raid = false;
};
