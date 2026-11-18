#pragma once

#include <cstdint>

namespace kwin_messages
{

enum class MessageType : std::uint8_t
{
    ActiveWindowChanged = 1,
    GetCursorPosition = 2,
    CursorPosition = 3
};

} // namespace kwin_messages
