#include "protocol.hpp"

#include "message_types.hpp"

#include <cstdint>
#include <type_traits>

namespace SKB
{
namespace
{

constexpr std::size_t HeaderSize = 1;
constexpr std::size_t MaxPacketSize = 65536;
constexpr std::size_t MaxStringSize = MaxPacketSize - HeaderSize - 4;

// -----------------------------------------------------------------------------
// Writer
// -----------------------------------------------------------------------------

class Writer
{
  public:
    void WriteU8(std::uint8_t value)
    {
        m_data.push_back(static_cast<std::byte>(value));
    }

    void WriteU32(std::uint32_t value)
    {
        m_data.push_back(static_cast<std::byte>((value >> 24) & 0xFF));
        m_data.push_back(static_cast<std::byte>((value >> 16) & 0xFF));
        m_data.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
        m_data.push_back(static_cast<std::byte>(value & 0xFF));
    }

    void WriteI32(std::int32_t value)
    {
        WriteU32(static_cast<std::uint32_t>(value));
    }

    void WriteU64(std::uint64_t value)
    {
        for (int i = 7; i >= 0; --i)
            m_data.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFF));
    }

    void WriteString(const std::string &value)
    {
        WriteU32(static_cast<std::uint32_t>(value.size()));

        for (unsigned char c : value)
            m_data.push_back(static_cast<std::byte>(c));
    }

    Packet Finish()
    {
        return std::move(m_data);
    }

  private:
    Packet m_data;
};

// -----------------------------------------------------------------------------
// Reader
// -----------------------------------------------------------------------------

class Reader
{
  public:
    explicit Reader(std::span<const std::byte> data) : m_data(data)
    {
    }

    bool ReadU8(std::uint8_t &value)
    {
        if (Remaining() < 1)
            return false;

        value = std::to_integer<std::uint8_t>(m_data[m_offset++]);
        return true;
    }

    bool ReadU32(std::uint32_t &value)
    {
        if (Remaining() < 4)
            return false;

        value = (std::uint32_t(std::to_integer<std::uint8_t>(m_data[m_offset])) << 24) |
                (std::uint32_t(std::to_integer<std::uint8_t>(m_data[m_offset + 1])) << 16) |
                (std::uint32_t(std::to_integer<std::uint8_t>(m_data[m_offset + 2])) << 8) |
                (std::uint32_t(std::to_integer<std::uint8_t>(m_data[m_offset + 3])));

        m_offset += 4;
        return true;
    }

    bool ReadI32(std::int32_t &value)
    {
        std::uint32_t raw;

        if (!ReadU32(raw))
            return false;

        value = static_cast<std::int32_t>(raw);
        return true;
    }

    bool ReadU64(std::uint64_t &value)
    {
        if (Remaining() < 8)
            return false;

        value = 0;

        for (int i = 0; i < 8; ++i)
        {
            value = (value << 8) | std::to_integer<std::uint8_t>(m_data[m_offset++]);
        }

        return true;
    }

    bool ReadString(std::string &value)
    {
        std::uint32_t size;

        if (!ReadU32(size))
            return false;

        if (size > MaxStringSize || Remaining() < size)
            return false;

        value.clear();
        value.reserve(size);

        for (std::uint32_t i = 0; i < size; ++i)
        {
            value.push_back(static_cast<char>(std::to_integer<unsigned char>(m_data[m_offset++])));
        }

        return true;
    }

    std::size_t Remaining() const
    {
        return m_data.size() - m_offset;
    }

    bool IsFinished() const
    {
        return Remaining() == 0;
    }

  private:
    std::span<const std::byte> m_data;
    std::size_t m_offset{0};
};

// -----------------------------------------------------------------------------
// Serialization helpers
// -----------------------------------------------------------------------------

template <typename T> void WriteMessage(Writer &writer, const T &message);

template <> void WriteMessage(Writer &writer, const ActiveWindowChanged &message)
{
    writer.WriteString(message.name);
}

template <> void WriteMessage(Writer &writer, const GetCursorPosition &message)
{
    writer.WriteU64(message.id);
}

template <> void WriteMessage(Writer &writer, const CursorPosition &message)
{
    writer.WriteU64(message.id);
    writer.WriteI32(message.x);
    writer.WriteI32(message.y);
}

// -----------------------------------------------------------------------------
// Deserialization helpers
// -----------------------------------------------------------------------------

template <typename T> std::expected<Message, ProtocolError> ReadMessage(Reader &reader);

template <> std::expected<Message, ProtocolError> ReadMessage<ActiveWindowChanged>(Reader &reader)
{
    ActiveWindowChanged message;

    if (!reader.ReadString(message.name))
        return std::unexpected(ProtocolError::InvalidString);

    return Message{std::move(message)};
}

template <> std::expected<Message, ProtocolError> ReadMessage<GetCursorPosition>(Reader &reader)
{
    GetCursorPosition message{};

    if (!reader.ReadU64(message.id))
        return std::unexpected(ProtocolError::InvalidPayload);

    return Message{message};
}

template <> std::expected<Message, ProtocolError> ReadMessage<CursorPosition>(Reader &reader)
{
    CursorPosition message{};

    if (!reader.ReadU64(message.id) || !reader.ReadI32(message.x) || !reader.ReadI32(message.y))
    {
        return std::unexpected(ProtocolError::InvalidPayload);
    }

    return Message{message};
}

} // namespace

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

Packet Serialize(const Message &message)
{
    Writer writer;

    std::visit(
        [&writer](const auto &value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, ActiveWindowChanged>)
            {
                writer.WriteU8(static_cast<std::uint8_t>(MessageType::ActiveWindowChanged));
            }
            else if constexpr (std::is_same_v<T, GetCursorPosition>)
            {
                writer.WriteU8(static_cast<std::uint8_t>(MessageType::GetCursorPosition));
            }
            else if constexpr (std::is_same_v<T, CursorPosition>)
            {
                writer.WriteU8(static_cast<std::uint8_t>(MessageType::CursorPosition));
            }

            WriteMessage(writer, value);
        },
        message);

    return writer.Finish();
}

std::expected<Message, ProtocolError> Deserialize(std::span<const std::byte> packet)
{
    if (packet.size() < HeaderSize)
        return std::unexpected(ProtocolError::PacketTooSmall);

    if (packet.size() > MaxPacketSize)
        return std::unexpected(ProtocolError::PacketTooLarge);

    Reader reader(packet);

    std::uint8_t rawType;

    if (!reader.ReadU8(rawType))
        return std::unexpected(ProtocolError::PacketTooSmall);

    const auto type = static_cast<MessageType>(rawType);

    std::expected<Message, ProtocolError> result = std::unexpected(ProtocolError::UnknownMessageType);

    switch (type)
    {
    case MessageType::ActiveWindowChanged:
        result = ReadMessage<ActiveWindowChanged>(reader);
        break;

    case MessageType::GetCursorPosition:
        result = ReadMessage<GetCursorPosition>(reader);
        break;

    case MessageType::CursorPosition:
        result = ReadMessage<CursorPosition>(reader);
        break;

    default:
        return std::unexpected(ProtocolError::UnknownMessageType);
    }

    if (!result)
        return result;

    if (!reader.IsFinished())
        return std::unexpected(ProtocolError::InvalidPayload);

    return result;
}

} // namespace SKB
