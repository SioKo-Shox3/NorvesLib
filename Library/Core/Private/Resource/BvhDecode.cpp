#include "Resource/BvhDecode.h"
#include "Text/UnicodeText.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace NorvesLib::Core::Bvh
{
    namespace
    {
        using Bytes = Container::Span<const uint8_t>;
        using Status = BvhDecodeStatus;
        bool Horizontal(uint8_t c) noexcept
        {
            return c == ' ' || c == '\t';
        }
        bool Newline(uint8_t c) noexcept
        {
            return c == '\r' || c == '\n';
        }
        bool Space(uint8_t c) noexcept
        {
            return Horizontal(c) || Newline(c);
        }
        struct Token
        {
            Bytes Text;
            size_t Offset = 0;
        };
        bool Word(Bytes value, const char* expected) noexcept
        {
            const size_t size = std::strlen(expected);
            if (value.size() != size)
            {
                return false;
            }
            for (size_t i = 0; i < size; ++i)
            {
                auto actual = value[i];
                auto wanted = static_cast<uint8_t>(expected[i]);
                if (actual >= 'A' && actual <= 'Z')
                {
                    actual += 'a' - 'A';
                }
                if (wanted >= 'A' && wanted <= 'Z')
                {
                    wanted += 'a' - 'A';
                }
                if (actual != wanted)
                {
                    return false;
                }
            }
            return true;
        }
        BvhDecodeResult Failure(Bytes bytes, Status status, size_t offset)
        {
            BvhDecodeResult result{status, std::min(offset, bytes.size()), 1, 1};
            for (size_t i = 0; i < result.ByteOffset; ++i)
            {
                if (bytes[i] == '\r')
                {
                    ++result.Line;
                    result.Column = 1;
                    if (i + 1 < result.ByteOffset && bytes[i + 1] == '\n')
                    {
                        ++i;
                    }
                }
                else if (bytes[i] == '\n')
                {
                    ++result.Line;
                    result.Column = 1;
                }
                else
                {
                    ++result.Column;
                }
            }
            return result;
        }
        class Parser
        {
          public:
            Parser(Bytes bytes, const BvhDecodeLimits& limits) : m_Bytes(bytes), m_Limits(limits)
            {
            }
            BvhDocument m_Document;
            BvhDecodeResult m_Result{Status::Success, 0, 1, 1};
            bool Parse()
            {
                if (m_Bytes.size() >= 3 && m_Bytes[0] == 0xef && m_Bytes[1] == 0xbb && m_Bytes[2] == 0xbf)
                {
                    m_Position = 3;
                }
                if (!Expect("HIERARCHY", Status::InvalidHeader) || !Expect("ROOT", Status::InvalidHierarchy) ||
                    !BeginJoint(UINT32_MAX))
                {
                    return false;
                }
                Container::VariableArray<uint32_t> stack;
                stack.push_back(0);
                while (!stack.empty())
                {
                    const auto token = Next();
                    if (Word(token.Text, "}"))
                    {
                        stack.pop_back();
                    }
                    else if (Word(token.Text, "JOINT"))
                    {
                        if (stack.size() >= m_Limits.MaxDepth)
                        {
                            return Fail(Status::LimitExceeded, token.Offset);
                        }
                        if (!BeginJoint(stack.back()))
                        {
                            return false;
                        }
                        stack.push_back(static_cast<uint32_t>(m_Document.Joints.size() - 1));
                    }
                    else if (Word(token.Text, "End"))
                    {
                        auto& joint = m_Document.Joints[stack.back()];
                        if (joint.bHasEndSite)
                        {
                            return Fail(Status::InvalidHierarchy, token.Offset);
                        }
                        if (!Expect("Site", Status::InvalidHierarchy) || !Expect("{", Status::InvalidHierarchy) ||
                            !Expect("OFFSET", Status::InvalidHierarchy) || !Vector(joint.EndSiteOffset) ||
                            !Expect("}", Status::InvalidHierarchy))
                        {
                            return false;
                        }
                        joint.bHasEndSite = true;
                    }
                    else
                    {
                        return Fail(Status::InvalidHierarchy, token.Offset);
                    }
                }
                if (!Expect("MOTION", Status::InvalidHeader) || !Label("Frames", Status::InvalidHeader))
                {
                    return false;
                }
                const auto count = Next();
                uint64_t frames = 0;
                if (!Unsigned(count, frames))
                {
                    return false;
                }
                if (frames == 0)
                {
                    return Fail(Status::FrameCountMismatch, count.Offset);
                }
                if (frames > m_Limits.MaxFrames || frames > UINT32_MAX)
                {
                    return Fail(Status::LimitExceeded, count.Offset);
                }
                m_Document.FrameCount = static_cast<uint32_t>(frames);
                if (!Expect("Frame", Status::InvalidHeader) || !Label("Time", Status::InvalidHeader))
                {
                    return false;
                }
                const auto time = Next();
                if (!Number(time, m_Document.FrameTimeSeconds, Status::InvalidFrameTime))
                {
                    return false;
                }
                if (!(m_Document.FrameTimeSeconds > 0) || !std::isfinite((frames - 1) * m_Document.FrameTimeSeconds))
                {
                    return Fail(Status::InvalidFrameTime, time.Offset);
                }
                if (m_Document.Channels.empty())
                {
                    return Fail(Status::InvalidChannels, count.Offset);
                }
                const size_t width = m_Document.Channels.size();
                if (frames > SIZE_MAX / width || frames * width > m_Limits.MaxValues ||
                    frames * width > SIZE_MAX / sizeof(double) || frames * width > m_Document.Values.max_size())
                {
                    return Fail(Status::LimitExceeded, count.Offset);
                }
                if (!FinishLine(Status::InvalidHeader))
                {
                    return false;
                }
                m_Document.Values.reserve(static_cast<size_t>(frames) * width);
                for (uint64_t frame = 0; frame < frames; ++frame)
                {
                    SkipSpace();
                    if (m_Position == m_Bytes.size())
                    {
                        return Fail(Status::FrameCountMismatch, m_Position);
                    }
                    for (size_t column = 0; column < width; ++column)
                    {
                        while (m_Position < m_Bytes.size() && Horizontal(m_Bytes[m_Position]))
                        {
                            ++m_Position;
                        }
                        if (m_Position == m_Bytes.size() || Newline(m_Bytes[m_Position]))
                        {
                            return Fail(Status::FrameCountMismatch, m_Position);
                        }
                        const auto value = Next(false);
                        double number = 0;
                        if (!Number(value, number, Status::InvalidNumber))
                        {
                            return false;
                        }
                        m_Document.Values.push_back(number);
                    }
                    if (!FinishLine(Status::FrameCountMismatch))
                    {
                        return false;
                    }
                }
                SkipSpace();
                if (m_Position != m_Bytes.size())
                {
                    return Fail(Status::TrailingInput, m_Position);
                }
                return true;
            }

          private:
            Bytes m_Bytes;
            const BvhDecodeLimits& m_Limits;
            size_t m_Position = 0;
            bool Fail(Status status, size_t offset)
            {
                m_Result = Failure(m_Bytes, status, offset);
                return false;
            }
            void SkipSpace()
            {
                while (m_Position < m_Bytes.size() && Space(m_Bytes[m_Position]))
                {
                    ++m_Position;
                }
            }
            Token Next(bool bAllWhitespace = true)
            {
                if (bAllWhitespace)
                {
                    SkipSpace();
                }
                else
                {
                    while (m_Position < m_Bytes.size() && Horizontal(m_Bytes[m_Position]))
                    {
                        ++m_Position;
                    }
                }
                const size_t start = m_Position;
                if (m_Position < m_Bytes.size() && (m_Bytes[m_Position] == '{' || m_Bytes[m_Position] == '}'))
                {
                    ++m_Position;
                }
                else
                {
                    while (m_Position < m_Bytes.size() && !Space(m_Bytes[m_Position]) && m_Bytes[m_Position] != '{' &&
                           m_Bytes[m_Position] != '}')
                    {
                        ++m_Position;
                    }
                }
                return {{m_Bytes.data() + start, m_Position - start}, start};
            }
            bool Expect(const char* expected, Status status)
            {
                const auto token = Next();
                return Word(token.Text, expected) || Fail(status, token.Offset);
            }
            bool Label(const char* expected, Status status)
            {
                const auto token = Next();
                if (!token.Text.empty() && token.Text.back() == ':')
                {
                    return Word({token.Text.data(), token.Text.size() - 1}, expected) || Fail(status, token.Offset);
                }
                return (Word(token.Text, expected) || Fail(status, token.Offset)) && Expect(":", status);
            }
            bool Unsigned(Token token, uint64_t& out)
            {
                if (token.Text.size() > m_Limits.MaxNumberBytes)
                {
                    return Fail(Status::LimitExceeded, token.Offset);
                }
                if (token.Text.empty())
                {
                    return Fail(Status::InvalidNumber, token.Offset);
                }
                for (uint8_t c : token.Text)
                {
                    if (c < '0' || c > '9')
                    {
                        return Fail(Status::InvalidNumber, token.Offset);
                    }
                }
                const auto* begin = reinterpret_cast<const char*>(token.Text.data());
                const auto converted = std::from_chars(begin, begin + token.Text.size(), out);
                if (converted.ec == std::errc::result_out_of_range)
                {
                    return Fail(Status::LimitExceeded, token.Offset);
                }
                return (converted.ec == std::errc{} && converted.ptr == begin + token.Text.size()) ||
                       Fail(Status::InvalidNumber, token.Offset);
            }
            bool Number(Token token, double& out, Status invalid)
            {
                if (token.Text.size() > m_Limits.MaxNumberBytes)
                {
                    return Fail(Status::LimitExceeded, token.Offset);
                }
                if (token.Text.empty())
                {
                    return Fail(invalid, token.Offset);
                }
                size_t i = 0;
                if (token.Text[i] == '+' || token.Text[i] == '-')
                {
                    ++i;
                }
                size_t digits = 0;
                while (i < token.Text.size() && token.Text[i] >= '0' && token.Text[i] <= '9')
                {
                    ++i;
                    ++digits;
                }
                if (i < token.Text.size() && token.Text[i] == '.')
                {
                    ++i;
                    while (i < token.Text.size() && token.Text[i] >= '0' && token.Text[i] <= '9')
                    {
                        ++i;
                        ++digits;
                    }
                }
                if (digits == 0)
                {
                    return Fail(invalid, token.Offset);
                }
                if (i < token.Text.size() && (token.Text[i] == 'e' || token.Text[i] == 'E'))
                {
                    ++i;
                    if (i < token.Text.size() && (token.Text[i] == '+' || token.Text[i] == '-'))
                    {
                        ++i;
                    }
                    const auto exponent = i;
                    while (i < token.Text.size() && token.Text[i] >= '0' && token.Text[i] <= '9')
                    {
                        ++i;
                    }
                    if (i == exponent)
                    {
                        return Fail(invalid, token.Offset);
                    }
                }
                if (i != token.Text.size())
                {
                    return Fail(invalid, token.Offset);
                }
                const auto* begin = reinterpret_cast<const char*>(token.Text.data());
                const auto* end = begin + token.Text.size();
                if (*begin == '+')
                {
                    ++begin;
                }
                const auto converted = std::from_chars(begin, end, out, std::chars_format::general);
                return (converted.ec == std::errc{} && converted.ptr == end && std::isfinite(out)) ||
                       Fail(invalid, token.Offset);
            }
            bool Vector(Vector3d& out)
            {
                return Number(Next(), out.X, Status::InvalidNumber) && Number(Next(), out.Y, Status::InvalidNumber) &&
                       Number(Next(), out.Z, Status::InvalidNumber);
            }
            bool BeginJoint(uint32_t parent)
            {
                const auto name = Next();
                if (m_Limits.MaxDepth == 0 || m_Document.Joints.size() >= m_Limits.MaxJoints ||
                    m_Document.Joints.size() >= UINT32_MAX || name.Text.size() > m_Limits.MaxNameBytes)
                {
                    return Fail(Status::LimitExceeded, name.Offset);
                }
                if (name.Text.empty() || Word(name.Text, "{") || Word(name.Text, "}") ||
                    std::find(name.Text.begin(), name.Text.end(), uint8_t{'"'}) != name.Text.end())
                {
                    return Fail(Status::InvalidHierarchy, name.Offset);
                }
                for (const auto& joint : m_Document.Joints)
                {
                    if (joint.Name.size() == name.Text.size() &&
                        std::equal(joint.Name.begin(), joint.Name.end(), name.Text.begin()))
                    {
                        return Fail(Status::DuplicateJointName, name.Offset);
                    }
                }
                Joint joint;
                joint.Name.assign(name.Text.begin(), name.Text.end());
                joint.Parent = parent;
                if (!Expect("{", Status::InvalidHierarchy) || !Expect("OFFSET", Status::InvalidHierarchy) ||
                    !Vector(joint.Offset) || !Expect("CHANNELS", Status::InvalidHierarchy))
                {
                    return false;
                }
                const auto countToken = Next();
                uint64_t count = 0;
                if (!Unsigned(countToken, count))
                {
                    return false;
                }
                if (count > 6)
                {
                    return Fail(Status::InvalidChannels, countToken.Offset);
                }
                if (m_Document.Channels.size() > UINT32_MAX - count)
                {
                    return Fail(Status::LimitExceeded, countToken.Offset);
                }
                joint.ChannelOffset = static_cast<uint32_t>(m_Document.Channels.size());
                joint.ChannelCount = static_cast<uint8_t>(count);
                unsigned seen = 0;
                for (uint64_t i = 0; i < count; ++i)
                {
                    const auto token = Next();
                    Channel channel;
                    if (Word(token.Text, "Xposition"))
                    {
                        channel = Channel::Xposition;
                    }
                    else if (Word(token.Text, "Yposition"))
                    {
                        channel = Channel::Yposition;
                    }
                    else if (Word(token.Text, "Zposition"))
                    {
                        channel = Channel::Zposition;
                    }
                    else if (Word(token.Text, "Xrotation"))
                    {
                        channel = Channel::Xrotation;
                    }
                    else if (Word(token.Text, "Yrotation"))
                    {
                        channel = Channel::Yrotation;
                    }
                    else if (Word(token.Text, "Zrotation"))
                    {
                        channel = Channel::Zrotation;
                    }
                    else
                    {
                        return Fail(Status::InvalidChannels, token.Offset);
                    }
                    const unsigned bit = 1u << static_cast<unsigned>(channel);
                    if (seen & bit)
                    {
                        return Fail(Status::InvalidChannels, token.Offset);
                    }
                    seen |= bit;
                    m_Document.Channels.push_back(channel);
                }
                m_Document.Joints.push_back(std::move(joint));
                return true;
            }
            bool FinishLine(Status status)
            {
                while (m_Position < m_Bytes.size() && Horizontal(m_Bytes[m_Position]))
                {
                    ++m_Position;
                }
                if (m_Position == m_Bytes.size())
                {
                    return true;
                }
                if (!Newline(m_Bytes[m_Position]))
                {
                    return Fail(status, m_Position);
                }
                const uint8_t first = m_Bytes[m_Position++];
                if (first == '\r' && m_Position < m_Bytes.size() && m_Bytes[m_Position] == '\n')
                {
                    ++m_Position;
                }
                return true;
            }
        };
    } // namespace
    BvhDecodeResult DecodeBvh(Bytes bytes, const BvhDecodeLimits& limits, BvhDocument& out)
    {
        if ((!bytes.empty() && !bytes.data()) || bytes.size() > UINTPTR_MAX - reinterpret_cast<uintptr_t>(bytes.data()))
        {
            return {Status::InvalidArgument, 0, 1, 1};
        }
        if (bytes.size() > limits.MaxInputBytes)
        {
            return {Status::LimitExceeded, 0, 1, 1};
        }
        size_t consumed = 0, invalid = SIZE_MAX;
        const bool bUtf8 = TextDetail::ForEachUnicodeScalar<uint8_t>(
            bytes,
            [&](uint32_t scalar)
            {
                if (invalid == SIZE_MAX && ((scalar < 32 && scalar != '\t' && scalar != '\r' && scalar != '\n') ||
                                            (scalar >= 0x7f && scalar <= 0x9f) || (scalar == 0xfeff && consumed != 0)))
                {
                    invalid = consumed;
                }
                consumed += scalar < 0x80 ? 1 : scalar < 0x800 ? 2 : scalar < 0x10000 ? 3 : 4;
            });
        if (!bUtf8)
        {
            return Failure(bytes, Status::InvalidUtf8, consumed);
        }
        if (invalid != SIZE_MAX)
        {
            return Failure(bytes, Status::InvalidCharacter, invalid);
        }
        if (bytes.empty())
        {
            return {Status::InvalidHeader, 0, 1, 1};
        }
        Parser parser(bytes, limits);
        if (!parser.Parse())
        {
            return parser.m_Result;
        }
        // 成功時の置換で再確保しないことを、独自allocatorの将来変更にも要求する。
        static_assert(std::is_nothrow_move_assignable_v<BvhDocument>);
        out = std::move(parser.m_Document);
        return {Status::Success, bytes.size(), 0, 0};
    }
} // namespace NorvesLib::Core::Bvh
