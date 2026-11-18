#pragma once

#include "./messages.hpp"

#include <cstddef>
#include <expected>
#include <span>
#include <variant>
#include <vector>

namespace kwin_messages
{

using Packet = std::vector<std::byte>;

using Message = std::variant<ActiveWindowChanged, GetCursorPosition, CursorPosition>;

enum class ProtocolError
{
    PacketTooSmall,
    PacketTooLarge,
    UnknownMessageType,
    InvalidPayload,
    InvalidString
};

Packet serialize(const Message &message);

std::expected<Message, ProtocolError> deserialize(std::span<const std::byte> packet);

} // namespace kwin_messages
