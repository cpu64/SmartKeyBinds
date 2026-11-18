#pragma once

#include <cstdint>
#include <string>

namespace SKB
{

struct ActiveWindowChanged
{
    std::string name;
};

struct GetCursorPosition
{
    std::uint64_t id;
};

struct CursorPosition
{
    std::uint64_t id;
    int x;
    int y;
};

} // namespace SKB
