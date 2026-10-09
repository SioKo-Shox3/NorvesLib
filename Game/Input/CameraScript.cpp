#include "Input/CameraScript.h"

#include "Core/Public/Input/GamepadTypes.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

using NorvesLib::Core::Container::String;

namespace Game::Input
{

    namespace
    {
        /// 1行の中の区切られた一語（先頭と長さ。終端文字を持たない）
        struct Token
        {
            const char *Begin = nullptr;
            std::size_t Length = 0;
        };

        bool IsBlank(char c)
        {
            return c == ' ' || c == '\t';
        }

        /// 一語を文字列にする（エラーの表示用。長い語は先頭の 63 文字まで）
        String TokenToString(const Token &token)
        {
            char buffer[64];
            const std::size_t length = token.Length < sizeof(buffer) - 1 ? token.Length : sizeof(buffer) - 1;
            std::memcpy(buffer, token.Begin, length);
            buffer[length] = '\0';
            return String(buffer);
        }

        /// 一語が keyword と等しいか（大文字小文字を区別しない）
        bool TokenEquals(const Token &token, const char *keyword)
        {
            const std::size_t keywordLength = std::strlen(keyword);
            if (token.Length != keywordLength)
            {
                return false;
            }
            for (std::size_t index = 0; index < keywordLength; ++index)
            {
                if (std::tolower(static_cast<unsigned char>(token.Begin[index])) !=
                    std::tolower(static_cast<unsigned char>(keyword[index])))
                {
                    return false;
                }
            }
            return true;
        }

        /// "name=" で始まる一語なら、'=' の後ろを outValue に入れる
        bool TokenHasPrefix(const Token &token, const char *name, Token &outValue)
        {
            const std::size_t nameLength = std::strlen(name);
            if (token.Length <= nameLength || token.Begin[nameLength] != '=')
            {
                return false;
            }
            Token head{token.Begin, nameLength};
            if (!TokenEquals(head, name))
            {
                return false;
            }
            outValue.Begin = token.Begin + nameLength + 1;
            outValue.Length = token.Length - nameLength - 1;
            return true;
        }

        /// 一語の先頭から有限の実数を1つ読む。読めたら end を読み終えた位置にする
        bool ParseFloat(const char *begin, const char *limit, float &outValue, const char *&end)
        {
            char buffer[64];
            const std::size_t length = static_cast<std::size_t>(limit - begin);
            if (length == 0 || length >= sizeof(buffer))
            {
                return false;
            }
            std::memcpy(buffer, begin, length);
            buffer[length] = '\0';
            char *parsedEnd = nullptr;
            errno = 0;
            const float value = std::strtof(buffer, &parsedEnd);
            if (parsedEnd == buffer || errno == ERANGE || !std::isfinite(value))
            {
                return false;
            }
            outValue = value;
            end = begin + (parsedEnd - buffer);
            return true;
        }

        /// "x" または "x,y" の実数1〜2個を読む。個数が expected と違えば false
        bool ParseFloats(const Token &value, float *outValues, int expected)
        {
            const char *cursor = value.Begin;
            const char *limit = value.Begin + value.Length;
            for (int index = 0; index < expected; ++index)
            {
                const char *valueEnd = cursor;
                const char *numberLimit = cursor;
                while (numberLimit < limit && *numberLimit != ',')
                {
                    ++numberLimit;
                }
                if (!ParseFloat(cursor, numberLimit, outValues[index], valueEnd) || valueEnd != numberLimit)
                {
                    return false;
                }
                cursor = numberLimit;
                if (index + 1 < expected)
                {
                    if (cursor >= limit)
                    {
                        return false;
                    }
                    ++cursor; // ','
                }
            }
            return cursor == limit;
        }

        /// "W+A+Shift" をキーのビットにする
        bool ParseKeys(const Token &value, uint32_t &outMask)
        {
            struct KeyName
            {
                const char *Name;
                uint32_t Bit;
            };
            static const KeyName kKeyNames[] = {
                {"W", CameraScript::kKeyW}, {"A", CameraScript::kKeyA},
                {"S", CameraScript::kKeyS}, {"D", CameraScript::kKeyD},
                {"Q", CameraScript::kKeyQ}, {"E", CameraScript::kKeyE},
                {"Shift", CameraScript::kKeyShift},
            };
            uint32_t mask = 0;
            const char *cursor = value.Begin;
            const char *limit = value.Begin + value.Length;
            while (cursor <= limit)
            {
                const char *partEnd = cursor;
                while (partEnd < limit && *partEnd != '+')
                {
                    ++partEnd;
                }
                const Token part{cursor, static_cast<std::size_t>(partEnd - cursor)};
                bool bFound = false;
                for (const KeyName &keyName : kKeyNames)
                {
                    if (TokenEquals(part, keyName.Name))
                    {
                        mask |= keyName.Bit;
                        bFound = true;
                        break;
                    }
                }
                if (!bFound)
                {
                    return false;
                }
                if (partEnd >= limit)
                {
                    break;
                }
                cursor = partEnd + 1;
            }
            outMask = mask;
            return true;
        }

        /// 1行を読む。コメントだけ・空の行は bHasStep を false にして成功とする
        bool ParseLine(const char *begin, const char *limit, CameraScriptStep &outStep, bool &bHasStep, String &outReason)
        {
            bHasStep = false;
            // コメントを落とす
            for (const char *scan = begin; scan < limit; ++scan)
            {
                if (*scan == '#')
                {
                    limit = scan;
                    break;
                }
            }
            // 語に分ける
            Token tokens[16];
            int tokenCount = 0;
            const char *cursor = begin;
            while (cursor < limit)
            {
                while (cursor < limit && (IsBlank(*cursor) || *cursor == '\r'))
                {
                    ++cursor;
                }
                if (cursor >= limit)
                {
                    break;
                }
                const char *tokenEnd = cursor;
                while (tokenEnd < limit && !IsBlank(*tokenEnd) && *tokenEnd != '\r')
                {
                    ++tokenEnd;
                }
                if (tokenCount >= 16)
                {
                    outReason = "項目が多すぎます";
                    return false;
                }
                tokens[tokenCount++] = Token{cursor, static_cast<std::size_t>(tokenEnd - cursor)};
                cursor = tokenEnd;
            }
            if (tokenCount == 0)
            {
                return true;
            }

            // 先頭の語はフレーム数（1〜kMaxFramesPerStep の整数）
            uint32_t frames = 0;
            for (std::size_t index = 0; index < tokens[0].Length; ++index)
            {
                const char c = tokens[0].Begin[index];
                if (c < '0' || c > '9' || frames > CameraScript::kMaxFramesPerStep)
                {
                    outReason = "先頭はフレーム数（正の整数）で書いてください";
                    return false;
                }
                frames = frames * 10 + static_cast<uint32_t>(c - '0');
            }
            if (frames == 0 || frames > CameraScript::kMaxFramesPerStep)
            {
                outReason = "フレーム数は 1〜100000 で書いてください";
                return false;
            }
            outStep = CameraScriptStep{};
            outStep.Frames = frames;

            for (int tokenIndex = 1; tokenIndex < tokenCount; ++tokenIndex)
            {
                const Token &token = tokens[tokenIndex];
                Token value;
                float numbers[2] = {0.0f, 0.0f};
                bool bOk = true;
                if (TokenEquals(token, "rmb"))
                {
                    outStep.bRightButton = true;
                }
                else if (TokenHasPrefix(token, "keys", value))
                {
                    bOk = ParseKeys(value, outStep.KeyMask);
                }
                else if (TokenHasPrefix(token, "mouse", value))
                {
                    bOk = ParseFloats(value, numbers, 2);
                    outStep.MouseDeltaX = numbers[0];
                    outStep.MouseDeltaY = numbers[1];
                }
                else if (TokenHasPrefix(token, "scroll", value))
                {
                    bOk = ParseFloats(value, numbers, 1);
                    outStep.Scroll = numbers[0];
                }
                else if (TokenHasPrefix(token, "lstick", value))
                {
                    bOk = ParseFloats(value, numbers, 2) && std::fabs(numbers[0]) <= 1.0f && std::fabs(numbers[1]) <= 1.0f;
                    outStep.LeftStick[0] = numbers[0];
                    outStep.LeftStick[1] = numbers[1];
                }
                else if (TokenHasPrefix(token, "rstick", value))
                {
                    bOk = ParseFloats(value, numbers, 2) && std::fabs(numbers[0]) <= 1.0f && std::fabs(numbers[1]) <= 1.0f;
                    outStep.RightStick[0] = numbers[0];
                    outStep.RightStick[1] = numbers[1];
                }
                else if (TokenHasPrefix(token, "lt", value))
                {
                    bOk = ParseFloats(value, numbers, 1) && numbers[0] >= 0.0f && numbers[0] <= 1.0f;
                    outStep.LeftTrigger = numbers[0];
                }
                else if (TokenHasPrefix(token, "rt", value))
                {
                    bOk = ParseFloats(value, numbers, 1) && numbers[0] >= 0.0f && numbers[0] <= 1.0f;
                    outStep.RightTrigger = numbers[0];
                }
                else
                {
                    outReason = String("知らない項目です: ") + TokenToString(token);
                    return false;
                }
                if (!bOk)
                {
                    outReason = String("値を読めません: ") + TokenToString(token);
                    return false;
                }
            }
            bHasStep = true;
            return true;
        }
    } // namespace

    bool CameraScript::Parse(const char *text, std::size_t length, String &outError)
    {
        m_Steps.clear();
        m_StepIndex = 0;
        m_FrameInStep = 0;
        outError = String();

        std::size_t lineNumber = 0;
        std::size_t position = 0;
        while (position <= length)
        {
            std::size_t lineEnd = position;
            while (lineEnd < length && text[lineEnd] != '\n')
            {
                ++lineEnd;
            }
            ++lineNumber;

            CameraScriptStep step;
            bool bHasStep = false;
            String reason;
            // UTF-8 の BOM は先頭行の頭で読み飛ばす
            const char *lineBegin = text + position;
            if (lineNumber == 1 && lineEnd - position >= 3 && static_cast<unsigned char>(lineBegin[0]) == 0xEF &&
                static_cast<unsigned char>(lineBegin[1]) == 0xBB && static_cast<unsigned char>(lineBegin[2]) == 0xBF)
            {
                lineBegin += 3;
            }
            if (!ParseLine(lineBegin, text + lineEnd, step, bHasStep, reason))
            {
                char prefix[48];
                std::snprintf(prefix, sizeof(prefix), "%zu 行目: ", lineNumber);
                outError = String(prefix) + reason;
                m_Steps.clear();
                return false;
            }
            if (bHasStep)
            {
                m_Steps.push_back(step);
            }
            position = lineEnd + 1;
        }

        if (m_Steps.empty())
        {
            outError = "流す入力の行がありません";
            return false;
        }
        return true;
    }

    bool CameraScript::Load(const char *path, String &outError)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            outError = String("台本を開けません: ") + path;
            return false;
        }
        NorvesLib::Core::Container::VariableArray<char> bytes;
        for (std::istreambuf_iterator<char> it(stream), end; it != end; ++it)
        {
            bytes.push_back(*it);
        }
        return Parse(bytes.data(), bytes.size(), outError);
    }

    void CameraScript::Rewind()
    {
        m_StepIndex = 0;
        m_FrameInStep = 0;
    }

    uint32_t CameraScript::GetTotalFrames() const
    {
        uint32_t total = 0;
        for (std::size_t index = 0; index < m_Steps.size(); ++index)
        {
            total += m_Steps[index].Frames;
        }
        return total;
    }

    bool CameraScript::NextFrame(NorvesLib::Core::Input::InputState &out)
    {
        using namespace NorvesLib::Core::Input;
        if (IsFinished())
        {
            return false;
        }
        const CameraScriptStep &step = m_Steps[m_StepIndex];

        out.SetKeyState(KeyCode::W, (step.KeyMask & kKeyW) != 0);
        out.SetKeyState(KeyCode::A, (step.KeyMask & kKeyA) != 0);
        out.SetKeyState(KeyCode::S, (step.KeyMask & kKeyS) != 0);
        out.SetKeyState(KeyCode::D, (step.KeyMask & kKeyD) != 0);
        out.SetKeyState(KeyCode::Q, (step.KeyMask & kKeyQ) != 0);
        out.SetKeyState(KeyCode::E, (step.KeyMask & kKeyE) != 0);
        out.SetKeyState(KeyCode::LeftShift, (step.KeyMask & kKeyShift) != 0);
        out.SetMouseButtonState(MouseButton::Right, step.bRightButton);
        // 実際の入力の集約（CameraInputCollector）と同じく、右ボタンを押している間だけ移動量が入る
        out.SetMousePosition(0.0f, 0.0f);
        if (step.bRightButton)
        {
            out.SetMousePosition(step.MouseDeltaX, step.MouseDeltaY);
        }
        out.AddMouseScroll(step.Scroll);

        GamepadState pad;
        pad.Connected = true;
        pad.Axes[static_cast<uint8_t>(GamepadAxis::LeftX)] = step.LeftStick[0];
        pad.Axes[static_cast<uint8_t>(GamepadAxis::LeftY)] = step.LeftStick[1];
        pad.Axes[static_cast<uint8_t>(GamepadAxis::RightX)] = step.RightStick[0];
        pad.Axes[static_cast<uint8_t>(GamepadAxis::RightY)] = step.RightStick[1];
        pad.Triggers[static_cast<uint8_t>(GamepadTrigger::Left)] = step.LeftTrigger;
        pad.Triggers[static_cast<uint8_t>(GamepadTrigger::Right)] = step.RightTrigger;
        out.SetGamepadState(0, pad);

        if (++m_FrameInStep >= step.Frames)
        {
            ++m_StepIndex;
            m_FrameInStep = 0;
        }
        return true;
    }

} // namespace Game::Input
