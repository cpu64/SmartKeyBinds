#pragma once

#include <cstdint>

namespace SKB
{

enum class MessageType : std::uint8_t
{
    ActiveWindowChanged = 1,
    GetCursorPosition = 2,
    CursorPosition = 3
};

} // namespace SKB
