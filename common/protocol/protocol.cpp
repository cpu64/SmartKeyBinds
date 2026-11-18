#include "./protocol.hpp"

#include "./message_types.hpp"

#include <cstdint>
#include <type_traits>

namespace kwin_messages
{
namespace
{

constexpr std::size_t HEADER_SIZE = 1;
constexpr std::size_t MAX_PACKET_SIZE = 65536;
constexpr std::size_t MAX_STRING_SIZE = MAX_PACKET_SIZE - HEADER_SIZE - 4;

// -----------------------------------------------------------------------------
// Writer
// -----------------------------------------------------------------------------

class Writer
{
  public:
    void write_u8(std::uint8_t value)
    {
        m_data.push_back(static_cast<std::byte>(value));
    }

    void write_u32(std::uint32_t value)
    {
        m_data.push_back(static_cast<std::byte>((value >> 24) & 0xFF));
        m_data.push_back(static_cast<std::byte>((value >> 16) & 0xFF));
        m_data.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
        m_data.push_back(static_cast<std::byte>(value & 0xFF));
    }

    void write_i32(std::int32_t value)
    {
        write_u32(static_cast<std::uint32_t>(value));
    }

    void write_u64(std::uint64_t value)
    {
        for (int i = 7; i >= 0; --i)
            m_data.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFF));
    }

    void write_string(const std::string &value)
    {
        write_u32(static_cast<std::uint32_t>(value.size()));

        for (unsigned char c : value)
            m_data.push_back(static_cast<std::byte>(c));
    }

    Packet finish()
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

    bool read_u8(std::uint8_t &value)
    {
        if (remaining() < 1)
            return false;

        value = std::to_integer<std::uint8_t>(m_data[m_offset++]);
        return true;
    }

    bool read_u32(std::uint32_t &value)
    {
        if (remaining() < 4)
            return false;

        value = (std::uint32_t(std::to_integer<std::uint8_t>(m_data[m_offset])) << 24) |
                (std::uint32_t(std::to_integer<std::uint8_t>(m_data[m_offset + 1])) << 16) |
                (std::uint32_t(std::to_integer<std::uint8_t>(m_data[m_offset + 2])) << 8) |
                (std::uint32_t(std::to_integer<std::uint8_t>(m_data[m_offset + 3])));

        m_offset += 4;
        return true;
    }

    bool read_i32(std::int32_t &value)
    {
        std::uint32_t raw;

        if (!read_u32(raw))
            return false;

        value = static_cast<std::int32_t>(raw);
        return true;
    }

    bool read_u64(std::uint64_t &value)
    {
        if (remaining() < 8)
            return false;

        value = 0;

        for (int i = 0; i < 8; ++i)
        {
            value = (value << 8) | std::to_integer<std::uint8_t>(m_data[m_offset++]);
        }

        return true;
    }

    bool read_string(std::string &value)
    {
        std::uint32_t size;

        if (!read_u32(size))
            return false;

        if (size > MAX_STRING_SIZE || remaining() < size)
            return false;

        value.clear();
        value.reserve(size);

        for (std::uint32_t i = 0; i < size; ++i)
        {
            value.push_back(static_cast<char>(std::to_integer<unsigned char>(m_data[m_offset++])));
        }

        return true;
    }

    std::size_t remaining() const
    {
        return m_data.size() - m_offset;
    }

    bool is_finished() const
    {
        return remaining() == 0;
    }

  private:
    std::span<const std::byte> m_data;
    std::size_t m_offset{0};
};

// -----------------------------------------------------------------------------
// Serialization helpers
// -----------------------------------------------------------------------------

template <typename T> void write_message(Writer &writer, const T &message);

template <> void write_message(Writer &writer, const ActiveWindowChanged &message)
{
    writer.write_string(message.name);
}

template <> void write_message(Writer &writer, const GetCursorPosition &message)
{
    writer.write_u64(message.id);
}

template <> void write_message(Writer &writer, const CursorPosition &message)
{
    writer.write_u64(message.id);
    writer.write_i32(message.x);
    writer.write_i32(message.y);
}

// -----------------------------------------------------------------------------
// Deserialization helpers
// -----------------------------------------------------------------------------

template <typename T> std::expected<Message, ProtocolError> read_message(Reader &reader);

template <> std::expected<Message, ProtocolError> read_message<ActiveWindowChanged>(Reader &reader)
{
    ActiveWindowChanged message;

    if (!reader.read_string(message.name))
        return std::unexpected(ProtocolError::InvalidString);

    return Message{std::move(message)};
}

template <> std::expected<Message, ProtocolError> read_message<GetCursorPosition>(Reader &reader)
{
    GetCursorPosition message{};

    if (!reader.read_u64(message.id))
        return std::unexpected(ProtocolError::InvalidPayload);

    return Message{message};
}

template <> std::expected<Message, ProtocolError> read_message<CursorPosition>(Reader &reader)
{
    CursorPosition message{};

    if (!reader.read_u64(message.id) || !reader.read_i32(message.x) || !reader.read_i32(message.y))
    {
        return std::unexpected(ProtocolError::InvalidPayload);
    }

    return Message{message};
}

} // namespace

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

Packet serialize(const Message &message)
{
    Writer writer;

    std::visit(
        [&writer](const auto &value) {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, ActiveWindowChanged>)
            {
                writer.write_u8(static_cast<std::uint8_t>(MessageType::ActiveWindowChanged));
            }
            else if constexpr (std::is_same_v<T, GetCursorPosition>)
            {
                writer.write_u8(static_cast<std::uint8_t>(MessageType::GetCursorPosition));
            }
            else if constexpr (std::is_same_v<T, CursorPosition>)
            {
                writer.write_u8(static_cast<std::uint8_t>(MessageType::CursorPosition));
            }

            write_message(writer, value);
        },
        message);

    return writer.finish();
}

std::expected<Message, ProtocolError> deserialize(std::span<const std::byte> packet)
{
    if (packet.size() < HEADER_SIZE)
        return std::unexpected(ProtocolError::PacketTooSmall);

    if (packet.size() > MAX_PACKET_SIZE)
        return std::unexpected(ProtocolError::PacketTooLarge);

    Reader reader(packet);

    std::uint8_t raw_type;

    if (!reader.read_u8(raw_type))
        return std::unexpected(ProtocolError::PacketTooSmall);

    const auto Type = static_cast<MessageType>(raw_type);

    std::expected<Message, ProtocolError> result = std::unexpected(ProtocolError::UnknownMessageType);

    switch (Type)
    {
    case MessageType::ActiveWindowChanged:
        result = read_message<ActiveWindowChanged>(reader);
        break;

    case MessageType::GetCursorPosition:
        result = read_message<GetCursorPosition>(reader);
        break;

    case MessageType::CursorPosition:
        result = read_message<CursorPosition>(reader);
        break;

    default:
        return std::unexpected(ProtocolError::UnknownMessageType);
    }

    if (!result)
        return result;

    if (!reader.is_finished())
        return std::unexpected(ProtocolError::InvalidPayload);

    return result;
}

} // namespace kwin_messages
