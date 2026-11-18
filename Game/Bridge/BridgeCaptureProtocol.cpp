#include "BridgeCaptureProtocol.h"

#include "Container/Span.h"
#include "Logging/LogMacros.h"
#include "Platform/PlatformFile.h"

#include <charconv>
#include <limits>
#include <Windows.h>

#include "stb_image.h"

namespace Game::Bridge
{
    namespace
    {
        namespace Container = NorvesLib::Core::Container;
        namespace Platform = NorvesLib::Core::Platform;
        namespace Rendering = NorvesLib::Core::Rendering;

        constexpr uint64_t FnvOffsetBasis = 0xcbf29ce484222325ull;
        constexpr uint64_t FnvPrime = 0x100000001b3ull;
        constexpr size_t MaxEventFileBytes = 33554432u;
        constexpr uint32_t MaxDrainFrames = 240;

        bool IsSeparator(char value)
        {
            return value == '/' || value == '\\';
        }

        bool IsAsciiLetter(char value)
        {
            return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
        }

        bool IsAsciiDigit(char value)
        {
            return value >= '0' && value <= '9';
        }

        bool IsValidOutputName(const Container::String& name)
        {
            if (name.empty() || name.size() > 64 || (!IsAsciiLetter(name[0]) && !IsAsciiDigit(name[0])))
            {
                return false;
            }
            for (size_t index = 1; index < name.size(); ++index)
            {
                const char value = name[index];
                if (!IsAsciiLetter(value) && !IsAsciiDigit(value) && value != '_' && value != '-')
                {
                    return false;
                }
            }
            return true;
        }

        bool IsAbsoluteCaptureDir(const Container::String& path)
        {
            return (path.size() >= 3 && IsAsciiLetter(path[0]) && path[1] == ':' && IsSeparator(path[2]))
                || (path.size() >= 2 && path[0] == '\\' && path[1] == '\\');
        }

        bool ContainsParentReference(const Container::String& path)
        {
            size_t componentStart = 0;
            for (size_t index = 0; index <= path.size(); ++index)
            {
                if (index == path.size() || IsSeparator(path[index]))
                {
                    if (index - componentStart == 2 && path[componentStart] == '.' && path[componentStart + 1] == '.')
                    {
                        return true;
                    }
                    componentStart = index + 1;
                }
            }
            return false;
        }

        bool Utf8ToWide(const Container::String& text, Container::WideString& out)
        {
            out.clear();
            if (text.empty() || text.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
            {
                return false;
            }
            for (size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == '\0')
                {
                    return false;
                }
            }
            const int length = static_cast<int>(text.size());
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
            if (count <= 0)
            {
                return false;
            }
            Container::VariableArray<wchar_t> buffer(static_cast<size_t>(count));
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, buffer.data(), count) != count)
            {
                return false;
            }
            out.append(buffer.data(), buffer.size());
            return true;
        }

        BridgeDeterministicCaptureRequest MakeRequest(const BridgeCaptureParams& params)
        {
            BridgeDeterministicCaptureRequest request;
            request.SettleFrames = params.SettleFrames;
            request.MaxWidth = params.MaxWidth;
            request.MaxHeight = params.MaxHeight;
            return request;
        }

        void AppendNumber(Container::String& text, uint64_t number)
        {
            char buffer[32];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), number);
            text.append(buffer, static_cast<size_t>(result.ptr - buffer));
        }

        Container::String MakeOutputPath(const BridgeCaptureParams& params)
        {
            Container::String path = params.CaptureDir;
            if (!IsSeparator(path[path.size() - 1]))
            {
                char separator = '\\';
                for (size_t index = 0; index < path.size(); ++index)
                {
                    if (IsSeparator(path[index]))
                    {
                        separator = path[index];
                    }
                }
                path += separator;
            }
            path += params.OutputName;
            path += ".png";
            return path;
        }

        void MakeFailureEvent(BridgeCaptureCompletedEvent& event, const Container::String& captureId,
            const Container::String& reason, uint32_t nativeError = 0)
        {
            event = {};
            event.CaptureId = captureId;
            if (reason == "capture_timeout")
            {
                event.FailureCode = "CAPTURE_TIMEOUT";
                event.FailureMessage = reinterpret_cast<const char*>(u8"撮影が時間内に終わらなかった");
            }
            else if (reason == "scene_not_ready")
            {
                event.FailureCode = "SCENE_NOT_READY";
                event.FailureMessage = reinterpret_cast<const char*>(u8"撮影中にシーンを使用できなくなった");
            }
            else if (reason == "deterministic_capture_disabled")
            {
                event.FailureCode = "CAPTURE_DISABLED";
                event.FailureMessage = reinterpret_cast<const char*>(u8"決定的な撮影が無効になった");
            }
            else if (reason == "output_write_failed")
            {
                event.FailureCode = "OUTPUT_WRITE_FAILED";
                event.FailureMessage = reinterpret_cast<const char*>(u8"PNG の書き出しに失敗した");
            }
            else
            {
                event.FailureCode = "RENDER_FAILED";
                event.FailureMessage = reinterpret_cast<const char*>(u8"撮影結果を作成できなかった");
            }
            event.FailureMessage += " (";
            event.FailureMessage += reason;
            event.FailureMessage += ")";
            if (reason == "output_write_failed")
            {
                event.FailureMessage += reinterpret_cast<const char*>(u8" Win32 エラー: ");
                AppendNumber(event.FailureMessage, nativeError);
            }
        }

        Container::String ComputePixelDigest(const BridgeDeterministicCaptureResult& result)
        {
            if (result.PngBytes.empty() || result.PngBytes.size() > MaxEventFileBytes
                || result.Width == 0 || result.Height == 0
                || result.Width > BridgeDeterministicCaptureMaxWidth || result.Height > BridgeDeterministicCaptureMaxHeight)
            {
                return {};
            }
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_uc* pixels = stbi_load_from_memory(result.PngBytes.data(), static_cast<int>(result.PngBytes.size()),
                &width, &height, &channels, 4);
            if (pixels == nullptr)
            {
                return {};
            }
            if (width <= 0 || height <= 0 || static_cast<uint32_t>(width) != result.Width
                || static_cast<uint32_t>(height) != result.Height)
            {
                stbi_image_free(pixels);
                return {};
            }
            const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
            uint64_t digest = FnvOffsetBasis;
            for (size_t index = 0; index < byteCount; ++index)
            {
                digest = (digest ^ pixels[index]) * FnvPrime;
            }
            stbi_image_free(pixels);

            constexpr char HexDigits[] = "0123456789abcdef";
            char hex[17] = {};
            for (size_t index = 0; index < 16; ++index)
            {
                hex[15 - index] = HexDigits[digest & 0xfu];
                digest >>= 4;
            }
            return hex;
        }

        uint32_t WritePngFile(const Container::String& path, const Container::VariableArray<uint8_t>& bytes)
        {
            const Container::String temporary = path + ".tmp";
            Container::WideString widePath;
            Container::WideString wideTemporary;
            if (!Utf8ToWide(path, widePath) || !Utf8ToWide(temporary, wideTemporary))
            {
                (void)Platform::DeleteFileIfExists(temporary);
                return ERROR_INVALID_NAME;
            }

            const auto deletion = Platform::DeleteFileIfExists(temporary);
            if (!deletion.bSuccess)
            {
                (void)Platform::DeleteFileIfExists(temporary);
                return deletion.NativeError != 0 ? deletion.NativeError : ERROR_WRITE_FAULT;
            }

            Platform::IFileOperations& ops = Platform::GetDefaultFileOperations();
            Platform::IFileOperations::Handle handle = nullptr;
            uint32_t error = ops.CreateNew(wideTemporary, handle);
            if (error == ERROR_SUCCESS)
            {
                error = ops.Write(handle, Container::Span<const uint8_t>(bytes.data(), bytes.size()));
                if (error == ERROR_SUCCESS)
                {
                    error = ops.Flush(handle);
                }
                const uint32_t closeError = ops.Close(handle);
                if (error == ERROR_SUCCESS)
                {
                    error = closeError;
                }
                if (error == ERROR_SUCCESS)
                {
                    error = ops.MoveReplace(wideTemporary, widePath);
                }
            }
            if (error != ERROR_SUCCESS)
            {
                (void)Platform::DeleteFileIfExists(temporary);
            }
            return error;
        }
    } // namespace

    NorvesLib::Core::Container::String ValidateBridgeCaptureParams(const BridgeCaptureParams& params)
    {
        if (!IsValidOutputName(params.OutputName))
        {
            return "invalid_output_name";
        }
        if (!IsAbsoluteCaptureDir(params.CaptureDir))
        {
            return "capture_dir_not_absolute";
        }
        if (ContainsParentReference(params.CaptureDir))
        {
            return "capture_dir_parent_reference";
        }
        Container::WideString wideDir;
        if (!Utf8ToWide(params.CaptureDir, wideDir))
        {
            return "capture_dir_not_found";
        }
        const DWORD attributes = GetFileAttributesW(wideDir.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            return "capture_dir_not_found";
        }
        return ValidateBridgeDeterministicCaptureRequest(MakeRequest(params));
    }

    BridgeCaptureAcceptResult BridgeCaptureProtocol::Accept(
        const BridgeCaptureParams& params, const BridgeDeterministicFrameSource& source)
    {
        BridgeCaptureAcceptResult result;
        if (IsBusy())
        {
            result.Status = BridgeCaptureAcceptStatus::Busy;
            result.Reason = "capture_busy";
            return result;
        }
        if (!IsCaptureAvailable(source))
        {
            result.Status = BridgeCaptureAcceptStatus::Disabled;
            result.Reason = "deterministic_capture_disabled";
            return result;
        }
        result.Reason = ValidateBridgeCaptureParams(params);
        if (!result.Reason.empty())
        {
            result.Status = BridgeCaptureAcceptStatus::InvalidParams;
            return result;
        }

        m_Params = params;
        m_Source = source;
        m_CaptureId = "cap-";
        AppendNumber(m_CaptureId, m_NextCaptureId++);
        m_RequestId = 0;
        m_DrainFrames = 0;
        m_bRequestIssued = false;
        m_bResultConsumed = false;
        m_State = State::Pending;
        result.Status = BridgeCaptureAcceptStatus::Accepted;
        result.CaptureId = m_CaptureId;
        return result;
    }

    bool BridgeCaptureProtocol::IsBusy() const
    {
        return m_State != State::Idle;
    }

    bool BridgeCaptureProtocol::IsCaptureAvailable(const BridgeDeterministicFrameSource& source)
    {
        return source.IsDeterministicEnabled != nullptr && source.IsSceneReady != nullptr
            && source.BeginEpoch != nullptr && source.GetEpochFrames != nullptr
            && source.GetNextFrameNumber != nullptr && source.RequestFrameCapture != nullptr
            && source.TryConsumeCapturedFrame != nullptr && source.IsDeterministicEnabled(source.Context);
    }

    bool BridgeCaptureProtocol::Drive(BridgeCaptureCompletedEvent& outEvent)
    {
        if (m_State == State::Idle)
        {
            return false;
        }
        if (m_State == State::Draining)
        {
            Rendering::CapturedFrame frame;
            if (m_Source.TryConsumeCapturedFrame(m_Source.Context, frame) && frame.RequestId == m_RequestId)
            {
                EnterIdle();
            }
            else if (++m_DrainFrames >= MaxDrainFrames)
            {
                NORVES_LOG_WARNING("Bridge", reinterpret_cast<const char*>(u8"撮影結果の回収が時間切れになった: requestId=%llu"),
                    static_cast<unsigned long long>(m_RequestId));
                EnterIdle();
            }
            return false;
        }
        if (m_State == State::Pending || !m_bRequestIssued)
        {
            // この撮影より前の要求が後から完了した場合も、枠を空けてから進める。
            Rendering::CapturedFrame staleFrame;
            (void)m_Source.TryConsumeCapturedFrame(m_Source.Context, staleFrame);
        }
        if (m_State == State::Pending)
        {
            const auto reason = m_Session.Begin(MakeRequest(m_Params), MakeWrappedSource());
            if (!reason.empty())
            {
                MakeFailureEvent(outEvent, m_CaptureId, reason);
                FinishOrDrain();
                return true;
            }
            m_State = State::Running;
            return false;
        }

        const BridgeDeterministicCaptureStatus status = m_Session.Tick();
        if (status == BridgeDeterministicCaptureStatus::Failed)
        {
            MakeFailureEvent(outEvent, m_CaptureId, m_Session.GetResult().ErrorCode);
            FinishOrDrain();
            return true;
        }
        if (status != BridgeDeterministicCaptureStatus::Completed)
        {
            return false;
        }

        const auto& capture = m_Session.GetResult();
        const auto digest = ComputePixelDigest(capture);
        if (digest.empty())
        {
            MakeFailureEvent(outEvent, m_CaptureId, "png_decode_failed");
            FinishOrDrain();
            return true;
        }
        const auto path = MakeOutputPath(m_Params);
        const uint32_t error = WritePngFile(path, capture.PngBytes);
        if (error != ERROR_SUCCESS)
        {
            MakeFailureEvent(outEvent, m_CaptureId, "output_write_failed", error);
            FinishOrDrain();
            return true;
        }

        outEvent = {};
        outEvent.CaptureId = m_CaptureId;
        outEvent.bSucceeded = true;
        outEvent.Path = path;
        outEvent.Width = capture.Width;
        outEvent.Height = capture.Height;
        outEvent.Frame = m_Params.SettleFrames;
        outEvent.PixelDigestHex = digest;
        outEvent.FileBytes = capture.PngBytes.size();
        FinishOrDrain();
        return true;
    }

    BridgeDeterministicFrameSource BridgeCaptureProtocol::MakeWrappedSource()
    {
        BridgeDeterministicFrameSource source;
        source.Context = this;
        source.IsDeterministicEnabled = WrappedIsDeterministicEnabled;
        source.IsSceneReady = WrappedIsSceneReady;
        source.BeginEpoch = WrappedBeginEpoch;
        source.GetEpochFrames = WrappedGetEpochFrames;
        source.GetNextFrameNumber = WrappedGetNextFrameNumber;
        source.RequestFrameCapture = WrappedRequestFrameCapture;
        source.TryConsumeCapturedFrame = WrappedTryConsumeCapturedFrame;
        return source;
    }

    bool BridgeCaptureProtocol::WrappedIsDeterministicEnabled(void* context)
    {
        const auto& source = static_cast<BridgeCaptureProtocol*>(context)->m_Source;
        return source.IsDeterministicEnabled(source.Context);
    }

    bool BridgeCaptureProtocol::WrappedIsSceneReady(void* context)
    {
        const auto& source = static_cast<BridgeCaptureProtocol*>(context)->m_Source;
        return source.IsSceneReady(source.Context);
    }

    void BridgeCaptureProtocol::WrappedBeginEpoch(void* context)
    {
        const auto& source = static_cast<BridgeCaptureProtocol*>(context)->m_Source;
        source.BeginEpoch(source.Context);
    }

    uint64_t BridgeCaptureProtocol::WrappedGetEpochFrames(void* context)
    {
        const auto& source = static_cast<BridgeCaptureProtocol*>(context)->m_Source;
        return source.GetEpochFrames(source.Context);
    }

    uint64_t BridgeCaptureProtocol::WrappedGetNextFrameNumber(void* context)
    {
        const auto& source = static_cast<BridgeCaptureProtocol*>(context)->m_Source;
        return source.GetNextFrameNumber(source.Context);
    }

    NorvesLib::Core::Rendering::FrameCaptureRequestResult BridgeCaptureProtocol::WrappedRequestFrameCapture(void* context)
    {
        auto& protocol = *static_cast<BridgeCaptureProtocol*>(context);
        const auto result = protocol.m_Source.RequestFrameCapture(protocol.m_Source.Context);
        if (result.IsAccepted())
        {
            protocol.m_bRequestIssued = true;
            protocol.m_RequestId = result.RequestId;
        }
        return result;
    }

    bool BridgeCaptureProtocol::WrappedTryConsumeCapturedFrame(void* context, Rendering::CapturedFrame& frame)
    {
        auto& protocol = *static_cast<BridgeCaptureProtocol*>(context);
        if (!protocol.m_Source.TryConsumeCapturedFrame(protocol.m_Source.Context, frame))
        {
            return false;
        }
        if (protocol.m_bRequestIssued && frame.RequestId == protocol.m_RequestId)
        {
            protocol.m_bResultConsumed = true;
        }
        return true;
    }

    void BridgeCaptureProtocol::FinishOrDrain()
    {
        if (m_bRequestIssued && !m_bResultConsumed)
        {
            m_DrainFrames = 0;
            m_State = State::Draining;
        }
        else
        {
            EnterIdle();
        }
    }

    void BridgeCaptureProtocol::EnterIdle()
    {
        m_State = State::Idle;
        m_Source = {};
        m_Params = {};
        m_CaptureId.clear();
    }
} // namespace Game::Bridge
