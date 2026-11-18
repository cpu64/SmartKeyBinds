#pragma once

#include "messages.hpp"

#include <cstddef>
#include <expected>
#include <span>
#include <variant>
#include <vector>

namespace SKB
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

Packet Serialize(const Message &message);

std::expected<Message, ProtocolError> Deserialize(std::span<const std::byte> packet);

} // namespace SKB
