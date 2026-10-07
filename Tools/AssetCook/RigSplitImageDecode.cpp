// split専用stb実体。旧global decoderのallocator/出力経路は変更しない。
#include "RigSplitImageInputs.h"
#include "Asset/RigSplitAllocationTestAccess.h"
#include <algorithm>
#include <cstddef>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <limits>
namespace
{
    struct alignas(std::max_align_t) SplitAllocation
    {
        SplitAllocation* Previous = nullptr;
        SplitAllocation* Next = nullptr;
        size_t Size = 0;
    };
    struct SplitDecodeHeap
    {
        uint64_t Limit = 0, Live = 0;
        SplitAllocation* First = nullptr;
        bool bExceeded = false;
    };
    thread_local SplitDecodeHeap* CurrentSplitHeap = nullptr;
    void* SplitMalloc(size_t size)
    {
        auto* heap = CurrentSplitHeap;
        if (!heap || size > SIZE_MAX - sizeof(SplitAllocation) ||
            uint64_t(size) + sizeof(SplitAllocation) > heap->Limit - heap->Live)
        {
            if (heap)
            {
                heap->bExceeded = true;
            }
            return nullptr;
        }
        auto* node = static_cast<SplitAllocation*>(std::malloc(size + sizeof(SplitAllocation)));
        if (!node)
        {
            return nullptr;
        }
        node->Size = size;
        node->Previous = nullptr;
        node->Next = heap->First;
        if (heap->First)
        {
            heap->First->Previous = node;
        }
        heap->First = node;
        heap->Live += size + sizeof(SplitAllocation);
        return node + 1;
    }
    void SplitFree(void* pointer)
    {
        if (!pointer)
        {
            return;
        }
        auto* node = static_cast<SplitAllocation*>(pointer) - 1;
        auto* heap = CurrentSplitHeap;
        if (node->Previous)
        {
            node->Previous->Next = node->Next;
        }
        else
        {
            heap->First = node->Next;
        }
        if (node->Next)
        {
            node->Next->Previous = node->Previous;
        }
        heap->Live -= node->Size + sizeof(SplitAllocation);
        std::free(node);
    }
    void* SplitRealloc(void* old, size_t size)
    {
        if (!old)
        {
            return SplitMalloc(size);
        }
        if (!size)
        {
            SplitFree(old);
            return nullptr;
        }
        auto* previous = static_cast<SplitAllocation*>(old) - 1;
        // 新旧の同時生存分も予算へ含め、reallocの一時的ピークを隠さない。
        void* result = SplitMalloc(size);
        if (!result)
        {
            return nullptr;
        }
        std::memcpy(result, old, std::min(size, previous->Size));
        SplitFree(old);
        return result;
    }
    struct SplitHeapScope
    {
        SplitDecodeHeap Heap;
        explicit SplitHeapScope(uint64_t limit)
        {
            Heap.Limit = limit;
            CurrentSplitHeap = &Heap;
        }
        ~SplitHeapScope()
        {
            while (Heap.First)
            {
                SplitFree(Heap.First + 1);
            }
            CurrentSplitHeap = nullptr;
        }
    };
} // namespace
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_MAX_DIMENSIONS 4096
#define STBI_MALLOC(sz) SplitMalloc(sz)
#define STBI_REALLOC(p, sz) SplitRealloc(p, sz)
#define STBI_FREE(p) SplitFree(p)
#include "stb_image.h"
namespace NorvesLib::Tools::AssetCook
{
    bool IsValidRigSplitImageLimits(const RigSplitImageLimits& v) noexcept
    {
        const RigSplitImageLimits hard;
        return v.MaxEncodedBytes && v.MaxEncodedBytes <= hard.MaxEncodedBytes && v.MaxTotalEncodedBytes &&
               v.MaxTotalEncodedBytes <= hard.MaxTotalEncodedBytes && v.MaxDecodedBytes &&
               v.MaxDecodedBytes <= hard.MaxDecodedBytes && v.MaxTotalDecodedBytes &&
               v.MaxTotalDecodedBytes <= hard.MaxTotalDecodedBytes && v.MaxDecoderWorkspaceBytes &&
               v.MaxDecoderWorkspaceBytes <= hard.MaxDecoderWorkspaceBytes && v.MaxOutputBytes &&
               v.MaxOutputBytes <= hard.MaxOutputBytes && v.MaxTotalOutputBytes &&
               v.MaxTotalOutputBytes <= hard.MaxTotalOutputBytes && v.MaxDimension &&
               v.MaxDimension <= hard.MaxDimension && v.MaxSourceImages && v.MaxSourceImages <= hard.MaxSourceImages;
    }
    bool MeasureRigSplitTextureBytes(uint32_t width, uint32_t height, const RigSplitImageLimits& limits,
                                     uint64_t& out) noexcept
    {
        if (!IsValidRigSplitImageLimits(limits) || !width || !height || width > limits.MaxDimension ||
            height > limits.MaxDimension)
        {
            return false;
        }
        if (uint64_t(width) * height * 4 > limits.MaxDecodedBytes)
        {
            return false;
        }
        uint64_t bytes = 112;
        while (true)
        {
            // NVTEX v0のheader/tableと全RGBA8 mip。計算だけ行いpixelを確保しない。
            bytes += 32 + uint64_t(width) * height * 4;
            if (bytes > limits.MaxOutputBytes)
            {
                return false;
            }
            if (width == 1 && height == 1)
            {
                break;
            }
            width = std::max<uint32_t>(1, width / 2);
            height = std::max<uint32_t>(1, height / 2);
        }
        out = bytes;
        return true;
    }
    bool DecodeRigSplitImage(Core::Container::Span<const uint8_t> encoded, const RigSplitImageLimits& limits,
                             DecodedTextureRgba8& out, Core::Container::AnsiString& error)
    {
        if (!IsValidRigSplitImageLimits(limits) || encoded.empty() || !encoded.data() ||
            encoded.size() > limits.MaxEncodedBytes || encoded.size() > INT_MAX || CurrentSplitHeap)
        {
            error = "split_image_input_limit";
            return false;
        }
        SplitHeapScope heap(limits.MaxDecoderWorkspaceBytes);
        int width = 0, height = 0, channels = 0;
        if (!stbi_info_from_memory(encoded.data(), int(encoded.size()), &width, &height, &channels) || width <= 0 ||
            height <= 0 || uint32_t(width) > limits.MaxDimension || uint32_t(height) > limits.MaxDimension ||
            uint64_t(width) * height * 4 > limits.MaxDecodedBytes)
        {
            error = heap.Heap.bExceeded ? "split_image_workspace_limit" : "split_image_dimensions";
            return false;
        }
        uint64_t outputBytes = 0;
        if (!MeasureRigSplitTextureBytes(uint32_t(width), uint32_t(height), limits, outputBytes))
        {
            error = "split_image_output_limit";
            return false;
        }
        const int expectedWidth = width, expectedHeight = height;
        Core::Skeletal::Detail::ObserveSplitAllocation("image_pixels");
        stbi_uc* pixels = stbi_load_from_memory(encoded.data(), int(encoded.size()), &width, &height, &channels, 4);
        if (!pixels)
        {
            error = heap.Heap.bExceeded ? "split_image_workspace_limit" : "split_image_decode";
            return false;
        }
        // stream内の後続headerがinfoと違っても、保存copy前に再検査する。
        if (width <= 0 || height <= 0 || uint32_t(width) > limits.MaxDimension ||
            uint32_t(height) > limits.MaxDimension || uint64_t(width) * height * 4 > limits.MaxDecodedBytes)
        {
            error = "split_image_dimensions";
            return false;
        }
        if (width != expectedWidth || height != expectedHeight ||
            !MeasureRigSplitTextureBytes(uint32_t(width), uint32_t(height), limits, outputBytes))
        {
            error = "split_image_dimensions_changed";
            return false;
        }
        Core::Skeletal::Detail::ObserveSplitAllocation("image_decoded_copy");
        DecodedTextureRgba8 candidate;
        candidate.Width = uint32_t(width);
        candidate.Height = uint32_t(height);
        candidate.Pixels.resize(size_t(width) * height * 4);
        std::memcpy(candidate.Pixels.data(), pixels, candidate.Pixels.size());
        stbi_image_free(pixels);
        out = std::move(candidate);
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
