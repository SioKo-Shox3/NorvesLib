#include "Debug/CapturedFramePng.h"

#include "Core/Public/Container/VariableArray.h"
#include "Core/Public/FileStream/FileStream.h"
#include "Core/Public/Rendering/FrameCaptureTypes.h"
#include "stb_image_write.h"

namespace Game::Debug
{
    namespace
    {
        using NorvesLib::Core::Container::VariableArray;

        void AppendPngBytes(void* pContext, void* pData, int byteCount)
        {
            auto* pBytes = static_cast<VariableArray<uint8_t>*>(pContext);
            if (!pBytes || !pData || byteCount <= 0)
            {
                return;
            }
            const uint8_t* pSource = static_cast<const uint8_t*>(pData);
            pBytes->insert(pBytes->end(), pSource, pSource + byteCount);
        }
    } // namespace

    bool SaveCapturedFramePng(const NorvesLib::Core::Rendering::CapturedFrame& frame,
                              const NorvesLib::Core::Container::String& path)
    {
        using NorvesLib::RHI::Format;
        if (!frame.IsSuccess() || frame.Width == 0 || frame.Height == 0 || frame.BytesPerPixel != 4)
        {
            return false;
        }
        bool bBgra = false;
        switch (frame.Format)
        {
        case Format::B8G8R8A8_UNORM:
        case Format::B8G8R8A8_SRGB:
            bBgra = true;
            break;
        case Format::R8G8B8A8_UNORM:
        case Format::R8G8B8A8_SRGB:
            break;
        default:
            return false;
        }
        const uint64_t tightRowPitch = static_cast<uint64_t>(frame.Width) * 4u;
        if (frame.RowPitchBytes < tightRowPitch ||
            frame.Pixels.size() < static_cast<size_t>(frame.RowPitchBytes) * frame.Height)
        {
            return false;
        }

        VariableArray<uint8_t> rgba(static_cast<size_t>(tightRowPitch) * frame.Height);
        for (uint32_t y = 0; y < frame.Height; ++y)
        {
            const uint8_t* pSourceRow = frame.Pixels.data() + static_cast<size_t>(frame.RowPitchBytes) * y;
            uint8_t* pDestRow = rgba.data() + static_cast<size_t>(tightRowPitch) * y;
            for (uint32_t x = 0; x < frame.Width; ++x)
            {
                const uint8_t* pSource = pSourceRow + x * 4u;
                uint8_t* pDest = pDestRow + x * 4u;
                pDest[0] = bBgra ? pSource[2] : pSource[0];
                pDest[1] = pSource[1];
                pDest[2] = bBgra ? pSource[0] : pSource[2];
                // スワップチェーンのアルファは表示に使われないため不透明にそろえる。
                pDest[3] = 255u;
            }
        }

        VariableArray<uint8_t> png;
        const int writeResult = stbi_write_png_to_func(AppendPngBytes,
                                                       &png,
                                                       static_cast<int>(frame.Width),
                                                       static_cast<int>(frame.Height),
                                                       4,
                                                       rgba.data(),
                                                       static_cast<int>(tightRowPitch));
        if (writeResult == 0 || png.empty())
        {
            return false;
        }

        NorvesLib::FileStream::FileStreamUniquePtr stream = NorvesLib::FileStream::FileStream::CreateUnique(
            path, NorvesLib::FileStream::FileMode::Write, NorvesLib::FileStream::FileAccess::Write,
            NorvesLib::FileStream::FileShare::None);
        if (!stream || stream->Write(png.data(), png.size()) != png.size())
        {
            return false;
        }
        stream->Flush();
        return true;
    }
} // namespace Game::Debug
