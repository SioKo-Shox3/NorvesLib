#include "Resource/GltfBufferFile.h"
#include <fstream>
#include <limits>
#include <system_error>

namespace NorvesLib::Core::Gltf
{
    ExternalBufferReadResult ReadBufferFile(Container::Span<const uint8_t> uri,
        Container::VariableArray<uint8_t>& bytes, void* context)
    {
        return ReadBufferFileWithPath(uri, bytes, context, nullptr);
    }
    ExternalBufferReadResult ReadBufferFileWithPath(Container::Span<const uint8_t> uri,
        Container::VariableArray<uint8_t>& bytes, void* context, std::filesystem::path* outResolvedPath)
    {
        bytes.clear();
        if (!context || !IsValidRelativeBufferUri(uri))
        {
            return ExternalBufferReadResult::InvalidPath;
        }
        const auto& source = static_cast<const BufferFileContext*>(context)->SourceFile;
        if (source.empty() || !source.has_filename() || source.filename() == "." || source.filename() == "..")
        {
            return ExternalBufferReadResult::SourcePathError;
        }
        std::error_code error;
        const auto absolute = std::filesystem::absolute(source,error);
        if (error)
        {
            return ExternalBufferReadResult::SourcePathError;
        }
        const auto directory = std::filesystem::weakly_canonical(absolute.parent_path(),error);
        if (error)
        {
            return ExternalBufferReadResult::SourcePathError;
        }
        const std::filesystem::path relative(reinterpret_cast<const char*>(uri.data()),
            reinterpret_cast<const char*>(uri.data()+uri.size()));
        const auto candidate = std::filesystem::weakly_canonical(directory/relative,error);
        if (error)
        {
            return ExternalBufferReadResult::OutsideDirectory;
        }
        auto candidatePart = candidate.begin();
        for (auto part = directory.begin(); part != directory.end(); ++part, ++candidatePart)
        {
            if (candidatePart == candidate.end() || *part != *candidatePart)
            {
                return ExternalBufferReadResult::OutsideDirectory;
            }
        }
        const auto status = std::filesystem::status(candidate,error);
        if (error || !std::filesystem::exists(status))
        {
            return ExternalBufferReadResult::OpenFailed;
        }
        if (!std::filesystem::is_regular_file(status))
        {
            return ExternalBufferReadResult::InvalidFileType;
        }
        std::ifstream input(candidate,std::ios::binary);
        if (!input.is_open())
        {
            return ExternalBufferReadResult::OpenFailed;
        }
        input.seekg(0,std::ios::end);
        const std::streamoff size = input.tellg();
        if (size < 0 || static_cast<uint64_t>(size) > std::numeric_limits<size_t>::max() ||
            static_cast<uint64_t>(size) > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        {
            return ExternalBufferReadResult::InvalidSize;
        }
        bytes.resize(static_cast<size_t>(size));
        input.seekg(0,std::ios::beg);
        if (!input)
        {
            bytes.clear();
            return ExternalBufferReadResult::ReadFailed;
        }
        if (!bytes.empty())
        {
            input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
            if (input.gcount() != static_cast<std::streamsize>(bytes.size()))
            {
                bytes.clear();
                return ExternalBufferReadResult::ReadFailed;
            }
        }
        if (outResolvedPath) *outResolvedPath = candidate;
        return ExternalBufferReadResult::Success;
    }
}
