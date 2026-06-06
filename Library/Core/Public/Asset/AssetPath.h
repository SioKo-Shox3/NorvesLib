#pragma once

#include "Container/String.h"
#include "Container/StringView.h"
#include <cstdint>

namespace NorvesLib::Core::Asset
{
    /**
     * @brief Lexically normalized asset path.
     *
     * Relative and Assets/-prefixed inputs produce a logical path for manifest lookup.
     * Absolute inputs produce only a resolved path; they are not logicalized in Phase 3.
     * This type performs no filesystem access or existence checks.
     */
    class AssetPath
    {
    public:
        enum class PathKind : uint8_t
        {
            Invalid,
            Logical,
            Absolute
        };

        AssetPath() = default;

        static AssetPath Normalize(Container::AnsiStringView input, Container::AnsiStringView assetRoot = {});
        static AssetPath Normalize(const char *input, const char *assetRoot = "");
        static AssetPath Invalid(const Container::AnsiString &originalPath = {});

        [[nodiscard]] bool IsValid() const noexcept { return m_bValid; }
        [[nodiscard]] bool IsAbsolute() const noexcept { return m_Kind == PathKind::Absolute; }
        [[nodiscard]] bool HasLogicalPath() const noexcept { return m_bValid && !m_LogicalPath.empty(); }
        [[nodiscard]] bool HasResolvedPath() const noexcept { return m_bValid && !m_ResolvedPath.empty(); }
        [[nodiscard]] PathKind GetKind() const noexcept { return m_Kind; }

        [[nodiscard]] const Container::AnsiString &GetOriginalPath() const noexcept { return m_OriginalPath; }
        [[nodiscard]] const Container::AnsiString &GetLogicalPath() const noexcept { return m_LogicalPath; }
        [[nodiscard]] const Container::AnsiString &GetResolvedPath() const noexcept { return m_ResolvedPath; }

    private:
        bool m_bValid = false;
        PathKind m_Kind = PathKind::Invalid;
        Container::AnsiString m_OriginalPath;
        Container::AnsiString m_LogicalPath;
        Container::AnsiString m_ResolvedPath;
    };
}
