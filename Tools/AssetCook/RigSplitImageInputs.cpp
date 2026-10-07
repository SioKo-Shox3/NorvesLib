#include "RigSplitImageInputs.h"
#include "Asset/RigSplitAllocationTestAccess.h"
#include "Asset/AssetPackageFormat.h"
#include "Resource/GltfImageSource.h"
#include "Resource/GltfBufferFile.h"
#include <algorithm>
namespace NorvesLib::Tools::AssetCook
{
    namespace C = Core::Container;
    namespace G = Core::Gltf;
    namespace
    {
        bool Canonical(const std::filesystem::path& source, C::Span<const uint8_t> uri, std::filesystem::path& out)
        {
            if (!G::IsValidRelativeBufferUri(uri))
            {
                return false;
            }
            std::error_code error;
            const auto absolute = std::filesystem::absolute(source, error);
            if (error)
            {
                return false;
            }
            const auto directory = std::filesystem::weakly_canonical(absolute.parent_path(), error);
            if (error)
            {
                return false;
            }
            const std::filesystem::path relative(reinterpret_cast<const char*>(uri.data()),
                                                 reinterpret_cast<const char*>(uri.data() + uri.size()));
            auto candidate = std::filesystem::weakly_canonical(directory / relative, error);
            if (error)
            {
                return false;
            }
            auto part = candidate.begin();
            for (auto base = directory.begin(); base != directory.end(); ++base, ++part)
            {
                if (part == candidate.end() || *base != *part)
                {
                    return false;
                }
            }
            out = std::move(candidate);
            return true;
        }
        C::Span<const uint8_t> CapturedExternal(const RigSplitImageInputs& inputs, const std::filesystem::path& path)
        {
            if (inputs.Capture->SourceCanonicalFiles.size() != inputs.Capture->Buffers.GetCount())
            {
                return {};
            }
            for (size_t i = 0; i < inputs.Capture->Buffers.GetCount(); ++i)
            {
                if (inputs.Capture->Buffers.GetSourceKind(i) == G::BufferStorageKind::ExternalFile &&
                    inputs.Capture->SourceCanonicalFiles[i] == path)
                {
                    return inputs.Capture->Buffers.GetSourceBytes(i);
                }
            }
            return {};
        }
    } // namespace
    bool RigSplitImageInputs::ReserveOutput(uint64_t bytes, C::AnsiString& error)
    {
        if (!IsValidRigSplitImageLimits(Limits))
        {
            error = "split_image_limits";
            return false;
        }
        if (bytes > Limits.MaxOutputBytes || OutputBytes > Limits.MaxTotalOutputBytes ||
            bytes > Limits.MaxTotalOutputBytes - OutputBytes)
        {
            error = "split_image_output_limit";
            return false;
        }
        OutputBytes += bytes;
        return true;
    }
    bool RigSplitImageInputs::ReserveCopies(uint64_t bytes, C::AnsiString& error)
    {
        if (!IsValidRigSplitImageLimits(Limits))
        {
            error = "split_image_limits";
            return false;
        }
        const uint64_t limit = Limits.MaxTotalDecodedBytes + Limits.MaxTotalEncodedBytes;
        if (ReturnedCopyBytes > limit || bytes > limit - ReturnedCopyBytes)
        {
            error = "split_image_copy_limit";
            return false;
        }
        ReturnedCopyBytes += bytes;
        return true;
    }
    bool RigSplitImageInputs::ReserveDerived(uint64_t bytes, uint32_t width, uint32_t height, C::AnsiString& error)
    {
        uint64_t output = 0;
        const uint64_t copyLimit = Limits.MaxTotalDecodedBytes + Limits.MaxTotalEncodedBytes;
        if (!IsValidRigSplitImageLimits(Limits) || bytes != uint64_t(width) * height * 4 ||
            bytes > Limits.MaxDecodedBytes || DecodedBytes > Limits.MaxTotalDecodedBytes ||
            bytes > Limits.MaxTotalDecodedBytes - DecodedBytes || ReturnedCopyBytes > copyLimit ||
            bytes > (copyLimit - ReturnedCopyBytes) / 2 ||
            !MeasureRigSplitTextureBytes(width, height, Limits, output) || !ReserveOutput(output, error))
        {
            error = "split_arm_output_limit";
            return false;
        }
        // scratch pixelsとSetRawRgba8の所有copyを、最初の確保より前に両方予約する。
        ReturnedCopyBytes += 2 * bytes;
        DecodedBytes += bytes;
        Core::Skeletal::Detail::ObserveSplitAllocation("arm_scratch_copy");
        return true;
    }
    bool RigSplitImageInputs::Read(uint32_t index, MeshEmbeddedImage& encodedOut, DecodedTextureRgba8& decodedOut,
                                   G::DataUriMime& mimeOut, C::AnsiString& error)
    {
        if (!Capture || !IsValidRigSplitImageLimits(Limits))
        {
            error = "split_image_context";
            return false;
        }
        const auto root = Capture->Document.GetRoot();
        const auto images = root.FindMember("images");
        if (!images.IsArray() || images.GetArraySize() > Limits.MaxSourceImages || index >= images.GetArraySize())
        {
            error = "split_image_count";
            return false;
        }
        size_t found = Entries.size();
        for (size_t i = 0; i < Entries.size(); ++i)
        {
            if (Entries[i].Index == index)
            {
                found = i;
                break;
            }
        }
        if (found == Entries.size())
        {
            if (EncodedBytes >= Limits.MaxTotalEncodedBytes || DecodedBytes >= Limits.MaxTotalDecodedBytes)
            {
                error = "split_image_total_limit";
                return false;
            }
            G::ImageSourceReadLimits sourceLimits;
            sourceLimits.MaxEncodedBytes = std::min(Limits.MaxEncodedBytes, Limits.MaxTotalEncodedBytes - EncodedBytes);
            sourceLimits.MaxUriBytes = std::min<uint64_t>(64ull * 1024 * 1024, sourceLimits.MaxEncodedBytes * 12 + 256);
            G::ImageSource source;
            if (G::ImageSource::Resolve(root, index, Capture->Buffers, source, &sourceLimits) !=
                G::ImageSourceResult::Success)
            {
                error = "split_image_source_limit_or_descriptor";
                return false;
            }
            C::VariableArray<uint8_t> external;
            C::Span<const uint8_t> bytes;
            std::filesystem::path canonical;
            if (source.GetKind() == G::ImageSourceKind::ExternalFile)
            {
                if (!Canonical(SourcePath, source.GetExternalUri(), canonical))
                {
                    error = "split_image_external_path";
                    return false;
                }
                // 同canonical fileやgeometry bufferは取得済みbytesを再利用する。
                for (const auto& entry : Entries)
                {
                    if (!entry.CanonicalFile.empty() && entry.CanonicalFile == canonical)
                    {
                        bytes = entry.Encoded.GetBytes();
                        break;
                    }
                }
                if (bytes.empty())
                {
                    bytes = CapturedExternal(*this, canonical);
                }
                if (bytes.empty())
                {
                    G::BufferFileContext context{SourcePath};
                    context.MaxReadBytes = sourceLimits.MaxEncodedBytes;
                    std::filesystem::path opened;
                    if (G::ReadBufferFileWithPath(source.GetExternalUri(), external, &context, &opened) !=
                            G::ExternalBufferReadResult::Success ||
                        opened != canonical)
                    {
                        error = "split_image_external_read_limit";
                        return false;
                    }
                    bytes = {external.data(), external.size()};
                }
            }
            else
            {
                bytes = source.GetBytes(Capture->Buffers);
            }
            if (bytes.empty() || bytes.size() > sourceLimits.MaxEncodedBytes)
            {
                error = "split_image_encoded_limit";
                return false;
            }
            const auto mime = G::ProbeEmbeddedImageMime(bytes);
            if ((mime != G::DataUriMime::Png && mime != G::DataUriMime::Jpeg) ||
                (source.GetMime() != G::DataUriMime::Unknown && source.GetMime() != mime))
            {
                error = "split_image_mime";
                return false;
            }
            if (OutputBytes >= Limits.MaxTotalOutputBytes)
            {
                error = "split_image_output_limit";
                return false;
            }
            const uint64_t copyLimit = Limits.MaxTotalDecodedBytes + Limits.MaxTotalEncodedBytes;
            if (ReturnedCopyBytes > copyLimit || bytes.size() > (copyLimit - ReturnedCopyBytes) / 2)
            {
                error = "split_image_copy_limit";
                return false;
            }
            const uint64_t copyDecodedRemaining = copyLimit - ReturnedCopyBytes - 2 * bytes.size();
            if (!copyDecodedRemaining)
            {
                error = "split_image_copy_limit";
                return false;
            }
            auto decodeLimits = Limits;
            decodeLimits.MaxOutputBytes = std::min(Limits.MaxOutputBytes, Limits.MaxTotalOutputBytes - OutputBytes);
            decodeLimits.MaxDecodedBytes = std::min(
                std::min(Limits.MaxDecodedBytes, Limits.MaxTotalDecodedBytes - DecodedBytes), copyDecodedRemaining);
            RigSplitImageEntry entry;
            entry.Index = index;
            entry.CanonicalFile = std::move(canonical);
            entry.Mime = mime;
            if (!DecodeRigSplitImage(bytes, decodeLimits, entry.Decoded, error))
            {
                return false;
            }
            uint64_t outputBytes = 0;
            if (!MeasureRigSplitTextureBytes(entry.Decoded.Width, entry.Decoded.Height, Limits, outputBytes) ||
                !ReserveOutput(outputBytes, error))
            {
                return false;
            }
            entry.Encoded.ImageIndex = index;
            if (!ReserveCopies(bytes.size(), error))
            {
                return false;
            }
            Core::Skeletal::Detail::ObserveSplitAllocation("image_cache_copy");
            if (!entry.Encoded.SetBytes(bytes, false))
            {
                error = "split_image_ownership";
                return false;
            }
            entry.Encoded.SourceHash = Core::Asset::ComputeAssetPackagePayloadHash(bytes.data(), bytes.size());
            EncodedBytes += bytes.size();
            DecodedBytes += entry.Decoded.Pixels.size();
            Entries.push_back(std::move(entry));
        }
        const auto& entry = Entries[found];
        const uint64_t copyBytes = entry.Encoded.GetBytes().size() + entry.Decoded.Pixels.size();
        if (!ReserveCopies(copyBytes, error))
        {
            return false;
        }
        Core::Skeletal::Detail::ObserveSplitAllocation("image_return_copy");
        // 下流planへGLBやlocal decoderの借用を残さない。
        encodedOut = entry.Encoded;
        decodedOut = entry.Decoded;
        mimeOut = entry.Mime;
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
