#include "Asset/AssetPath.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace NorvesLib::Core::Asset
{
    namespace
    {
        std::string ToStdString(Container::AnsiStringView view)
        {
            if (view.data() == nullptr || view.size() == 0)
            {
                return {};
            }

            return std::string(view.data(), view.size());
        }

        std::string ReplaceSeparators(std::string value)
        {
            std::replace(value.begin(), value.end(), '\\', '/');
            return value;
        }

        bool IsDriveLetter(char value)
        {
            return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
        }

        bool IsDriveAbsolute(const std::string &path)
        {
            return path.size() >= 3 && IsDriveLetter(path[0]) && path[1] == ':' && path[2] == '/';
        }

        bool IsDriveRelative(const std::string &path)
        {
            return path.size() >= 2 && IsDriveLetter(path[0]) && path[1] == ':' && (path.size() == 2 || path[2] != '/');
        }

        bool IsUncPath(const std::string &path)
        {
            return path.size() >= 2 && path[0] == '/' && path[1] == '/';
        }

        bool IsRootAbsolute(const std::string &path)
        {
            return !path.empty() && path[0] == '/' && !IsUncPath(path);
        }

        void StripLeadingCurrentDirectory(std::string &path)
        {
            while (path == "." || path.rfind("./", 0) == 0)
            {
                if (path == ".")
                {
                    path.clear();
                    return;
                }

                path.erase(0, 2);
            }
        }

        bool StripAssetsPrefix(std::string &path)
        {
            if (path == "Assets")
            {
                path.clear();
                return true;
            }

            if (path.rfind("Assets/", 0) == 0)
            {
                path.erase(0, 7);
                return true;
            }

            return false;
        }

        bool NormalizeSegments(const std::string &path, std::string &outPath)
        {
            std::vector<std::string> segments;
            size_t start = 0;

            while (start <= path.size())
            {
                const size_t end = path.find('/', start);
                const size_t count = (end == std::string::npos) ? std::string::npos : end - start;
                const std::string segment = path.substr(start, count);

                if (segment.empty() || segment == ".")
                {
                }
                else if (segment == "..")
                {
                    if (segments.empty())
                    {
                        return false;
                    }

                    segments.pop_back();
                }
                else
                {
                    segments.push_back(segment);
                }

                if (end == std::string::npos)
                {
                    break;
                }

                start = end + 1;
            }

            outPath.clear();
            for (size_t index = 0; index < segments.size(); ++index)
            {
                if (index > 0)
                {
                    outPath += '/';
                }

                outPath += segments[index];
            }

            return !outPath.empty();
        }

        bool NormalizeRelativePath(std::string path, std::string &outPath)
        {
            StripLeadingCurrentDirectory(path);
            StripAssetsPrefix(path);
            return NormalizeSegments(path, outPath);
        }

        bool NormalizeAbsolutePath(const std::string &path, std::string &outPath)
        {
            std::string prefix;
            std::string segmentText;

            if (IsDriveAbsolute(path))
            {
                prefix = path.substr(0, 3);
                segmentText = path.substr(3);
            }
            else if (IsRootAbsolute(path))
            {
                prefix = "/";
                segmentText = path.substr(1);
            }
            else
            {
                return false;
            }

            std::string normalizedSegments;
            if (!NormalizeSegments(segmentText, normalizedSegments))
            {
                return false;
            }

            outPath = prefix + normalizedSegments;
            return true;
        }

        std::string JoinResolvedPath(const std::string &assetRoot, const std::string &logicalPath)
        {
            if (assetRoot.empty())
            {
                return {};
            }

            std::string normalizedRoot = ReplaceSeparators(assetRoot);
            while (!normalizedRoot.empty() && normalizedRoot.back() == '/')
            {
                normalizedRoot.pop_back();
            }

            if (normalizedRoot.empty())
            {
                return logicalPath;
            }

            return normalizedRoot + "/" + logicalPath;
        }
    }

    AssetPath AssetPath::Normalize(Container::AnsiStringView input, Container::AnsiStringView assetRoot)
    {
        const std::string original = ToStdString(input);
        std::string path = ReplaceSeparators(original);

        if (path.empty())
        {
            return Invalid(Container::AnsiString(original));
        }

        if (IsDriveRelative(path) || IsUncPath(path))
        {
            return Invalid(Container::AnsiString(original));
        }

        AssetPath result;
        result.m_OriginalPath = Container::AnsiString(original);

        if (IsDriveAbsolute(path) || IsRootAbsolute(path))
        {
            std::string resolvedPath;
            if (!NormalizeAbsolutePath(path, resolvedPath))
            {
                return Invalid(Container::AnsiString(original));
            }

            result.m_bValid = true;
            result.m_Kind = PathKind::Absolute;
            result.m_ResolvedPath = Container::AnsiString(resolvedPath);
            return result;
        }

        std::string logicalPath;
        if (!NormalizeRelativePath(path, logicalPath))
        {
            return Invalid(Container::AnsiString(original));
        }

        result.m_bValid = true;
        result.m_Kind = PathKind::Logical;
        result.m_LogicalPath = Container::AnsiString(logicalPath);
        result.m_ResolvedPath = Container::AnsiString(JoinResolvedPath(ToStdString(assetRoot), logicalPath));
        return result;
    }

    AssetPath AssetPath::Normalize(const char *input, const char *assetRoot)
    {
        return Normalize(Container::AnsiStringView(input), Container::AnsiStringView(assetRoot));
    }

    AssetPath AssetPath::Invalid(const Container::AnsiString &originalPath)
    {
        AssetPath result;
        result.m_OriginalPath = originalPath;
        return result;
    }
}
