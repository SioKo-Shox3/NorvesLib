#include "Asset/AssetFileReader.h"
#include "FileStream/FileStream.h"
#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace NorvesLib::Core::Asset
{
    namespace
    {
        std::string ToStdString(Container::AnsiStringView value)
        {
            if (value.data() == nullptr || value.size() == 0)
            {
                return {};
            }

            return std::string(value.data(), value.size());
        }

        std::string ToStdString(const Container::AnsiString &value)
        {
            return ToStdString(Container::AnsiStringView(value));
        }

        Container::AnsiString ToAnsiString(const std::string &value)
        {
            return Container::AnsiString(value);
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

        bool IsAbsolutePath(const std::string &path)
        {
            return IsDriveAbsolute(path) || IsRootAbsolute(path);
        }

        bool HasParentSegment(const std::string &path)
        {
            size_t start = 0;
            while (start <= path.size())
            {
                const size_t end = path.find('/', start);
                const size_t count = (end == std::string::npos) ? std::string::npos : end - start;
                if (path.substr(start, count) == "..")
                {
                    return true;
                }

                if (end == std::string::npos)
                {
                    break;
                }

                start = end + 1;
            }

            return false;
        }

        bool NormalizeAssetRoot(const Container::AnsiString &assetRoot, std::string &outRoot)
        {
            std::string root = ReplaceSeparators(ToStdString(assetRoot));
            while (!root.empty() && root.back() == '/')
            {
                root.pop_back();
            }

            if (root.empty() || IsDriveRelative(root) || IsUncPath(root) || !IsAbsolutePath(root) || HasParentSegment(root))
            {
                return false;
            }

            const AssetPath normalized = AssetPath::Normalize(ToAnsiString(root));
            if (!normalized.IsValid() || !normalized.IsAbsolute() || !normalized.HasResolvedPath())
            {
                return false;
            }

            outRoot = ToStdString(normalized.GetResolvedPath());
            return !outRoot.empty();
        }

        AssetReadResult MakeFailure(AssetReadStatus status,
                                    const AssetPath &path,
                                    const Container::AnsiString &reason,
                                    int64_t fileSize = -1)
        {
            AssetReadResult result;
            result.Status = status;
            result.Path = path;
            result.Blob = AssetBlob::Invalid();
            result.Reason = reason;
            result.BytesRead = 0;
            result.FileSize = fileSize;
            return result;
        }

        AssetReadStatus DiagnoseOpenFailure(const std::string &resolvedPath)
        {
            std::error_code errorCode;
            const bool bExists = std::filesystem::exists(std::filesystem::path(resolvedPath), errorCode);
            if (!errorCode && !bExists)
            {
                return AssetReadStatus::FileNotFound;
            }

            return AssetReadStatus::OpenFailed;
        }

#if defined(UNICODE)
        bool ConvertAnsiToWide(std::string_view input, UINT codePage, DWORD flags, std::wstring &outValue)
        {
            outValue.clear();
            if (input.empty())
            {
                return true;
            }

            if (input.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                return false;
            }

            const int inputSize = static_cast<int>(input.size());
            const int wideSize = MultiByteToWideChar(codePage, flags, input.data(), inputSize, nullptr, 0);
            if (wideSize <= 0)
            {
                return false;
            }

            outValue.resize(static_cast<size_t>(wideSize));
            return MultiByteToWideChar(codePage, flags, input.data(), inputSize, outValue.data(), wideSize) == wideSize;
        }
#endif

        bool ConvertToFileStreamString(const std::string &path, NorvesLib::FileStream::String &outPath)
        {
            if (path.empty())
            {
                outPath = NorvesLib::FileStream::String{};
                return false;
            }

#if defined(UNICODE)
            std::wstring widePath;
            if (!ConvertAnsiToWide(std::string_view(path), CP_UTF8, MB_ERR_INVALID_CHARS, widePath) &&
                !ConvertAnsiToWide(std::string_view(path), CP_ACP, 0, widePath))
            {
                outPath = NorvesLib::FileStream::String{};
                return false;
            }

            outPath = NorvesLib::FileStream::String(widePath);
#else
            outPath = NorvesLib::FileStream::String(path);
#endif
            return true;
        }

        AssetReadResult ReadResolvedPath(const AssetPath &assetPath)
        {
            const std::string resolvedPath = ToStdString(assetPath.GetResolvedPath());
            if (resolvedPath.empty())
            {
                return MakeFailure(AssetReadStatus::InvalidPath, assetPath, "resolved path is empty");
            }

            NorvesLib::FileStream::String fileStreamPath;
            if (!ConvertToFileStreamString(resolvedPath, fileStreamPath))
            {
                return MakeFailure(AssetReadStatus::InvalidPath, assetPath, "failed to convert path");
            }

            auto stream = NorvesLib::FileStream::FileStream::Create(
                fileStreamPath,
                NorvesLib::FileStream::FileMode::Read,
                NorvesLib::FileStream::FileAccess::Read,
                NorvesLib::FileStream::FileShare::Read);

            if (!stream || !stream->IsOpen())
            {
                return MakeFailure(DiagnoseOpenFailure(resolvedPath), assetPath, "failed to open file");
            }

            const int64_t fileSize = stream->GetSize();
            if (fileSize < 0)
            {
                return MakeFailure(AssetReadStatus::SizeQueryFailed, assetPath, "failed to query file size");
            }

            if (static_cast<uint64_t>(fileSize) > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
            {
                return MakeFailure(AssetReadStatus::SizeTooLarge, assetPath, "file size is too large", fileSize);
            }

            const size_t targetSize = static_cast<size_t>(fileSize);
            auto bytes = Container::MakeShared<AssetBlob::ByteArray>();
            bytes->resize(targetSize);

            size_t bytesRead = 0;
            while (bytesRead < targetSize)
            {
                const size_t remainingBytes = targetSize - bytesRead;
                const size_t chunkSize = std::min<size_t>(
                    remainingBytes,
                    static_cast<size_t>(std::numeric_limits<std::streamsize>::max()));
                const size_t readNow = stream->Read(bytes->data() + bytesRead, chunkSize);
                if (readNow == 0)
                {
                    return MakeFailure(AssetReadStatus::ReadFailed, assetPath, "failed to read full file", fileSize);
                }

                bytesRead += readNow;
            }

            AssetReadResult result;
            result.Status = AssetReadStatus::Success;
            result.Path = assetPath;
            result.Blob = AssetBlob::FromOwnedBytes(bytes, assetPath.GetResolvedPath());
            result.Reason = {};
            result.BytesRead = bytesRead;
            result.FileSize = fileSize;

            if (!result.Blob.IsValid())
            {
                return MakeFailure(AssetReadStatus::ReadFailed, assetPath, "failed to create asset blob", fileSize);
            }

            return result;
        }
    }

    AssetFileReader::AssetFileReader()
        : m_DefaultAssetRoot(GetCompiledDefaultAssetRoot())
    {
    }

    AssetFileReader::AssetFileReader(const Container::AnsiString &defaultAssetRoot)
        : m_DefaultAssetRoot(defaultAssetRoot)
    {
    }

    AssetReadResult AssetFileReader::Read(const AssetReadRequest &request) const
    {
        if (request.InputPath.empty())
        {
            return MakeFailure(AssetReadStatus::InvalidRequest, AssetPath::Invalid(request.InputPath), "input path is empty");
        }

        const std::string inputPath = ReplaceSeparators(ToStdString(request.InputPath));
        if (IsDriveRelative(inputPath) || IsUncPath(inputPath))
        {
            return MakeFailure(AssetReadStatus::InvalidPath, AssetPath::Invalid(request.InputPath), "input path is invalid");
        }

        if (IsAbsolutePath(inputPath))
        {
            if (!request.bAllowAbsolutePath)
            {
                return MakeFailure(AssetReadStatus::InvalidPath, AssetPath::Invalid(request.InputPath), "absolute path is not allowed");
            }

            const AssetPath path = AssetPath::Normalize(request.InputPath);
            if (!path.IsValid() || !path.HasResolvedPath())
            {
                return MakeFailure(AssetReadStatus::InvalidPath, path, "absolute path is invalid");
            }

            return ReadResolvedPath(path);
        }

        const Container::AnsiString &requestedRoot = request.AssetRoot.empty() ? m_DefaultAssetRoot : request.AssetRoot;
        std::string normalizedRoot;
        if (!NormalizeAssetRoot(requestedRoot, normalizedRoot))
        {
            return MakeFailure(AssetReadStatus::InvalidAssetRoot, AssetPath::Invalid(request.InputPath), "asset root is invalid");
        }

        const AssetPath path = AssetPath::Normalize(request.InputPath, ToAnsiString(normalizedRoot));
        if (!path.IsValid() || !path.HasLogicalPath() || !path.HasResolvedPath())
        {
            return MakeFailure(AssetReadStatus::InvalidPath, path, "relative path is invalid");
        }

        return ReadResolvedPath(path);
    }

    AssetReadResult AssetFileReader::Read(Container::AnsiStringView inputPath) const
    {
        AssetReadRequest request;
        request.InputPath = Container::AnsiString(inputPath);
        request.AssetRoot = {};
        request.bAllowAbsolutePath = true;
        return Read(request);
    }

    Container::AnsiString AssetFileReader::GetCompiledDefaultAssetRoot()
    {
#if defined(NORVES_ASSET_DIR)
        return Container::AnsiString(NORVES_ASSET_DIR);
#else
        return {};
#endif
    }
}
