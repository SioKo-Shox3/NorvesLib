// 資産プレビューの起動引数と、床に並べる位置をエンジンの状態に依存せずに求める。
#include "GameModes/AssetPreview/AssetPreviewLayout.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

using NorvesLib::Core::Container::String;
using NorvesLib::Core::Container::VariableArray;
using NorvesLib::Math::AABB;
using NorvesLib::Math::Vector3;

namespace Game::GameModes
{
    namespace
    {
        constexpr const TCHAR* kOptionPrefix = TEXT("--asset-preview-");
        constexpr const TCHAR* kModelOption = TEXT("--asset-preview-model=");
        constexpr const TCHAR* kSkeletalOption = TEXT("--asset-preview-skeletal=");
        constexpr const TCHAR* kClipOption = TEXT("--asset-preview-clip=");
        constexpr const TCHAR* kTimeOption = TEXT("--asset-preview-time=");

        bool HasPrefix(const String& text, const TCHAR* prefix, size_t& outLength)
        {
            size_t length = 0;
            while (prefix[length] != 0)
            {
                if (length >= text.size() || text[length] != prefix[length])
                {
                    return false;
                }
                ++length;
            }
            outLength = length;
            return true;
        }

        bool TryStripPrefix(const String& argument, const TCHAR* prefix, String& outValue)
        {
            size_t length = 0;
            if (!HasPrefix(argument, prefix, length))
            {
                return false;
            }
            outValue = argument.substr(length);
            return true;
        }

        // UTF-8 または UTF-16 の空白を認識し、パスやクリップ名の内容は変更しない。
        bool IsBlank(const String& value)
        {
            size_t index = 0;
            while (index < value.size())
            {
#if defined(_UNICODE)
                const unsigned int code = static_cast<unsigned int>(value[index++]);
#else
                const unsigned int first = static_cast<unsigned char>(value[index++]);
                unsigned int code = first;
                if (first >= 0xC2 && first <= 0xDF && index < value.size())
                {
                    const unsigned int next = static_cast<unsigned char>(value[index++]);
                    if ((next & 0xC0) != 0x80)
                    {
                        return false;
                    }
                    code = ((first & 0x1F) << 6) | (next & 0x3F);
                }
                else if (first >= 0xE0 && first <= 0xEF && value.size() - index >= 2)
                {
                    const unsigned int next = static_cast<unsigned char>(value[index++]);
                    const unsigned int last = static_cast<unsigned char>(value[index++]);
                    if ((next & 0xC0) != 0x80 || (last & 0xC0) != 0x80)
                    {
                        return false;
                    }
                    code = ((first & 0x0F) << 12) | ((next & 0x3F) << 6) | (last & 0x3F);
                    if (code < 0x800)
                    {
                        return false;
                    }
                }
                else if (first >= 0x80)
                {
                    return false;
                }
#endif
                if (code != 0x20 && !(code >= 0x09 && code <= 0x0D) && code != 0x85 &&
                    code != 0xA0 && code != 0x1680 && !(code >= 0x2000 && code <= 0x200A) &&
                    code != 0x2028 && code != 0x2029 && code != 0x202F && code != 0x205F && code != 0x3000)
                {
                    return false;
                }
            }
            return true;
        }

        bool ContainsPath(const VariableArray<String>& paths, const String& value)
        {
            for (const String& path : paths)
            {
                if (path == value)
                {
                    return true;
                }
            }
            return false;
        }

        bool TryParseTime(const String& text, float& outTime)
        {
            VariableArray<char> buffer;
            buffer.reserve(text.size() + 1);
            for (const TCHAR character : text)
            {
                if (character <= 0 || character > 0x7F)
                {
                    return false;
                }
                buffer.push_back(static_cast<char>(character));
            }
            buffer.push_back('\0');

            char* end = nullptr;
            const float time = std::strtof(buffer.data(), &end);
            if (end == buffer.data() || end != buffer.data() + text.size() || !std::isfinite(time) ||
                time < 0.0f || time > kAssetPreviewMaxTimeSeconds)
            {
                return false;
            }
            // 負の極小値が float の負のゼロへ丸められても、負の時刻として拒否する。
            if (time == 0.0f && std::signbit(time))
            {
                const char* digit = buffer.data();
                while (*digit != '\0' && *digit != '-')
                {
                    ++digit;
                }
                if (*digit == '\0')
                {
                    return false;
                }
                ++digit;
                const bool bHex = digit[0] == '0' && (digit[1] == 'x' || digit[1] == 'X');
                if (bHex)
                {
                    digit += 2;
                }
                while (*digit != '\0' && (bHex ? (*digit != 'p' && *digit != 'P') : (*digit != 'e' && *digit != 'E')))
                {
                    if ((*digit >= '1' && *digit <= '9') ||
                        (bHex && ((*digit >= 'a' && *digit <= 'f') || (*digit >= 'A' && *digit <= 'F'))))
                    {
                        return false;
                    }
                    ++digit;
                }
            }
            outTime = time;
            return true;
        }

        bool FailArgument(const String& argument, const TCHAR* reason, String& outError)
        {
            outError = argument;
            outError += TEXT("：");
            outError += reason;
            return false;
        }
    } // namespace

    bool AssetPreviewArguments::HasAny() const noexcept
    {
        return !ModelPaths.empty() || !SkeletalPaths.empty() || !ClipName.empty() || TimeSeconds >= 0.0f;
    }

    bool IsAssetPreviewOption(const String& argument)
    {
        size_t length = 0;
        return HasPrefix(argument, kOptionPrefix, length);
    }

    bool ParseAssetPreviewArguments(const VariableArray<String>& args, AssetPreviewArguments& out, String& outError)
    {
        out = AssetPreviewArguments{};
        outError.clear();
        AssetPreviewArguments parsed;
        String clipArgument;
        String timeArgument;
        bool bHasClip = false;
        bool bHasTime = false;

        // 先頭は実行ファイルのパスなので、起動オプションとして扱わない。
        for (size_t index = 1; index < args.size(); ++index)
        {
            const String& argument = args[index];
            if (!IsAssetPreviewOption(argument))
            {
                continue;
            }

            String value;
            if (TryStripPrefix(argument, kModelOption, value))
            {
                if (IsBlank(value))
                {
                    return FailArgument(argument, TEXT("モデルのパスが空です"), outError);
                }
                if (ContainsPath(parsed.ModelPaths, value))
                {
                    return FailArgument(argument, TEXT("モデルのパスが重複しています"), outError);
                }
                parsed.ModelPaths.push_back(value);
            }
            else if (TryStripPrefix(argument, kSkeletalOption, value))
            {
                if (IsBlank(value))
                {
                    return FailArgument(argument, TEXT("骨格つき資産のパスが空です"), outError);
                }
                if (ContainsPath(parsed.SkeletalPaths, value))
                {
                    return FailArgument(argument, TEXT("骨格つき資産のパスが重複しています"), outError);
                }
                parsed.SkeletalPaths.push_back(value);
            }
            else if (TryStripPrefix(argument, kClipOption, value))
            {
                if (bHasClip)
                {
                    return FailArgument(argument, TEXT("クリップの指定が重複しています"), outError);
                }
                if (IsBlank(value))
                {
                    return FailArgument(argument, TEXT("クリップ名が空です"), outError);
                }
                parsed.ClipName = value;
                clipArgument = argument;
                bHasClip = true;
            }
            else if (TryStripPrefix(argument, kTimeOption, value))
            {
                if (bHasTime)
                {
                    return FailArgument(argument, TEXT("時刻の指定が重複しています"), outError);
                }
                if (IsBlank(value) || !TryParseTime(value, parsed.TimeSeconds))
                {
                    return FailArgument(argument, TEXT("時刻は 0 以上 100000 以下の有限な数で指定してください"), outError);
                }
                timeArgument = argument;
                bHasTime = true;
            }
            else
            {
                return FailArgument(argument, TEXT("不明な引数です。名前と = を確認してください"), outError);
            }
        }

        if (bHasTime && !bHasClip)
        {
            return FailArgument(timeArgument, TEXT("時刻には --asset-preview-clip の指定が必要です"), outError);
        }
        if (bHasClip && parsed.SkeletalPaths.empty())
        {
            return FailArgument(clipArgument, TEXT("クリップには --asset-preview-skeletal の指定が必要です"), outError);
        }

        out = std::move(parsed);
        return true;
    }

    bool IsValidAssetPreviewBounds(const AABB& bounds)
    {
        return std::isfinite(bounds.Min.x) && std::isfinite(bounds.Min.y) && std::isfinite(bounds.Min.z) &&
               std::isfinite(bounds.Max.x) && std::isfinite(bounds.Max.y) && std::isfinite(bounds.Max.z) &&
               bounds.Min.x <= bounds.Max.x && bounds.Min.y <= bounds.Max.y && bounds.Min.z <= bounds.Max.z;
    }

    bool ComputeAssetPreviewLayout(const AABB* bounds, size_t count, float gapMeters, VariableArray<Vector3>& outPositions)
    {
        outPositions.clear();
        if (count == 0)
        {
            return true;
        }
        if (bounds == nullptr || !std::isfinite(gapMeters) || gapMeters < 0.0f)
        {
            return false;
        }

        // 有限な float の差や総幅が float の範囲を超えても、中間計算があふれないようにする。
        double totalWidth = static_cast<double>(gapMeters) * static_cast<double>(count - 1);
        for (size_t index = 0; index < count; ++index)
        {
            if (!IsValidAssetPreviewBounds(bounds[index]))
            {
                return false;
            }
            const double extent = static_cast<double>(bounds[index].Max.x) - bounds[index].Min.x;
            totalWidth += extent < kAssetPreviewMinFootprintMeters ? kAssetPreviewMinFootprintMeters : extent;
        }

        const double maxFloat = static_cast<double>((std::numeric_limits<float>::max)());
        double cursor = -totalWidth * 0.5;
        outPositions.reserve(count);
        for (size_t index = 0; index < count; ++index)
        {
            const AABB& asset = bounds[index];
            const double extent = static_cast<double>(asset.Max.x) - asset.Min.x;
            const double width = extent < kAssetPreviewMinFootprintMeters ? kAssetPreviewMinFootprintMeters : extent;
            const double x = cursor + (width - extent) * 0.5 - asset.Min.x;
            const double z = -(static_cast<double>(asset.Min.z) + asset.Max.z) * 0.5;
            if (x < -maxFloat || x > maxFloat)
            {
                outPositions.clear();
                return false;
            }
            outPositions.emplace_back(static_cast<float>(x), -asset.Min.y, static_cast<float>(z));
            cursor += width + static_cast<double>(gapMeters);
        }
        return true;
    }
} // namespace Game::GameModes
