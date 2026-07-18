#include "Bridge/NorvesLibBridgeAdapter.h"

#include "Core/Public/Rendering/FrameCaptureTypes.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

namespace
{
    using Game::Bridge::NorvesLibBridgeAdapter;
    using Norves::Bridge::JsonValue;
    using NorvesLib::Core::Rendering::CapturedFrame;
    using NorvesLib::Core::Rendering::FrameCaptureRequestResult;
    using NorvesLib::Core::Rendering::FrameCaptureRequestStatus;
    using NorvesLib::Core::Rendering::FrameCaptureResultStatus;

    struct ProviderState
    {
        int RequestCount = 0;
        FrameCaptureRequestStatus NextRequestStatus = FrameCaptureRequestStatus::Accepted;
        bool bHasFrame = false;
        CapturedFrame Frame;
    };

    JsonValue ParseJson(const char* text)
    {
        auto parsed = JsonValue::parse(text);
        assert(parsed.is_ok());
        return std::move(parsed).value();
    }

    std::string DumpThumbnail(NorvesLibBridgeAdapter& adapter, const char* paramsText)
    {
        JsonValue params = ParseJson(paramsText);
        auto result = adapter.viewportGetThumbnail(params);
        assert(result.is_ok());
        return result.value().dump();
    }

    std::string DumpThumbnail(NorvesLibBridgeAdapter& adapter)
    {
        JsonValue params;
        auto result = adapter.viewportGetThumbnail(params);
        assert(result.is_ok());
        return result.value().dump();
    }

    std::string DumpCapabilities(NorvesLibBridgeAdapter& adapter)
    {
        auto result = adapter.getCapabilities(ParseJson("{}"));
        assert(result.is_ok());
        return result.value().dump();
    }

    bool Contains(const std::string& text, const char* needle)
    {
        return text.find(needle) != std::string::npos;
    }

    std::optional<std::string> ExtractStringField(const std::string& text, const char* key)
    {
        std::string needle = "\"";
        needle += key;
        needle += "\":\"";
        const std::size_t begin = text.find(needle);
        if (begin == std::string::npos)
        {
            return std::nullopt;
        }
        std::size_t valueBegin = begin + needle.size();
        std::size_t valueEnd = text.find('"', valueBegin);
        if (valueEnd == std::string::npos)
        {
            return std::nullopt;
        }
        return text.substr(valueBegin, valueEnd - valueBegin);
    }

    std::optional<int> ExtractIntegerField(const std::string& text, const char* key)
    {
        std::string needle = "\"";
        needle += key;
        needle += "\":";
        const std::size_t begin = text.find(needle);
        if (begin == std::string::npos)
        {
            return std::nullopt;
        }
        std::size_t valueBegin = begin + needle.size();
        std::size_t valueEnd = valueBegin;
        while (valueEnd < text.size() && text[valueEnd] >= '0' && text[valueEnd] <= '9')
        {
            ++valueEnd;
        }
        assert(valueEnd > valueBegin);
        return std::stoi(text.substr(valueBegin, valueEnd - valueBegin));
    }

    void AssertThumbnailSuccess(const std::string& text)
    {
        assert(Contains(text, "\"imageBase64\":\""));
        assert(Contains(text, "\"mimeType\":\"image/png\""));
        assert(!Contains(text, "\"error\""));
        const std::optional<std::string> image = ExtractStringField(text, "imageBase64");
        assert(image.has_value());
        assert(!image.value().empty());
    }

    void AssertDimensions(const std::string& text, int width, int height)
    {
        const std::optional<int> actualWidth = ExtractIntegerField(text, "width");
        const std::optional<int> actualHeight = ExtractIntegerField(text, "height");
        assert(actualWidth.has_value());
        assert(actualHeight.has_value());
        assert(actualWidth.value() == width);
        assert(actualHeight.value() == height);
    }

    CapturedFrame MakeSuccessFrame(uint32_t width, uint32_t height)
    {
        CapturedFrame frame;
        frame.Status = FrameCaptureResultStatus::Success;
        frame.RequestId = 7;
        frame.FrameNumber = 11;
        frame.Width = width;
        frame.Height = height;
        frame.Format = NorvesLib::RHI::Format::R8G8B8A8_UNORM;
        frame.BytesPerPixel = 4;
        frame.RowPitchBytes = width * frame.BytesPerPixel;
        frame.Pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * frame.BytesPerPixel);
        for (std::size_t index = 0; index < frame.Pixels.size(); index += 4u)
        {
            frame.Pixels[index + 0u] = static_cast<uint8_t>((index / 4u) & 0xFFu);
            frame.Pixels[index + 1u] = 64u;
            frame.Pixels[index + 2u] = 128u;
            frame.Pixels[index + 3u] = 255u;
        }
        return frame;
    }

    CapturedFrame MakeCaptureFailureFrame()
    {
        CapturedFrame frame;
        frame.Status = FrameCaptureResultStatus::SourceUnavailable;
        frame.RequestId = 8;
        frame.FrameNumber = 12;
        return frame;
    }

    CapturedFrame MakeEncodeFailureFrame()
    {
        CapturedFrame frame = MakeSuccessFrame(1, 1);
        frame.Format = NorvesLib::RHI::Format::R16_FLOAT;
        return frame;
    }

    bool TryConsumeCapturedFrame(void* context, CapturedFrame& outFrame)
    {
        ProviderState* state = static_cast<ProviderState*>(context);
        assert(state != nullptr);
        if (!state->bHasFrame)
        {
            return false;
        }
        outFrame = state->Frame;
        state->bHasFrame = false;
        return true;
    }

    FrameCaptureRequestResult RequestFrameCapture(void* context)
    {
        ProviderState* state = static_cast<ProviderState*>(context);
        assert(state != nullptr);
        ++state->RequestCount;

        FrameCaptureRequestResult result;
        result.Status = state->NextRequestStatus;
        result.RequestId = static_cast<uint64_t>(state->RequestCount);
        return result;
    }

    void InstallProvider(NorvesLibBridgeAdapter& adapter, ProviderState& state)
    {
        NorvesLibBridgeAdapter::ViewportThumbnailCaptureProviderForTesting provider{};
        provider.Context = &state;
        provider.TryConsumeCapturedFrame = &TryConsumeCapturedFrame;
        provider.RequestFrameCapture = &RequestFrameCapture;
        adapter.SetViewportThumbnailCaptureProviderForTesting(provider);
    }

    void TestCapabilitiesIncludeViewportThumbnail()
    {
        NorvesLibBridgeAdapter adapter;
        const std::string caps = DumpCapabilities(adapter);
        assert(Contains(caps, "\"name\":\"viewport.thumbnail\""));
    }

    void TestLenientParamsReturnPlaceholderAndRequestCapture()
    {
        NorvesLibBridgeAdapter adapter;
        ProviderState state;
        InstallProvider(adapter, state);

        std::string text = DumpThumbnail(adapter, "null");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 1, 1);
        assert(state.RequestCount == 1);

        text = DumpThumbnail(adapter, "{}");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 1, 1);
        assert(state.RequestCount == 2);

        text = DumpThumbnail(adapter);
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 1, 1);
        assert(state.RequestCount == 3);

        text = DumpThumbnail(adapter, "42");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 1, 1);
        assert(state.RequestCount == 4);

        text = DumpThumbnail(adapter, R"({"unknown":true})");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 1, 1);
        assert(state.RequestCount == 5);
    }

    void TestInvalidDimensionsReturnSuccess()
    {
        const char* cases[] =
        {
            R"({"maxWidth":-1,"maxHeight":-2})",
            R"({"maxWidth":0,"maxHeight":0})",
            R"({"maxWidth":1.5,"maxHeight":2.25})",
            R"({"maxWidth":"16","maxHeight":"9"})",
            R"({"maxWidth":true,"maxHeight":false})",
            R"({"maxWidth":{},"maxHeight":{}})",
            R"({"maxWidth":[],"maxHeight":[]})"
        };

        NorvesLibBridgeAdapter adapter;
        ProviderState state;
        InstallProvider(adapter, state);

        for (const char* params : cases)
        {
            const std::string text = DumpThumbnail(adapter, params);
            AssertThumbnailSuccess(text);
            AssertDimensions(text, 1, 1);
        }
        assert(state.RequestCount == 7);
    }

    void TestPartialAndClampedCaps()
    {
        NorvesLibBridgeAdapter adapter;
        ProviderState state;
        InstallProvider(adapter, state);

        state.bHasFrame = true;
        state.Frame = MakeSuccessFrame(4, 2);
        std::string text = DumpThumbnail(adapter, R"({"maxWidth":2})");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 2, 1);

        state.bHasFrame = true;
        state.Frame = MakeSuccessFrame(4, 4);
        text = DumpThumbnail(adapter, R"({"maxHeight":2})");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 2, 2);

        state.bHasFrame = true;
        state.Frame = MakeSuccessFrame(1280, 720);
        text = DumpThumbnail(adapter, R"({"maxWidth":9999,"maxHeight":9999})");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 640, 360);

        state.bHasFrame = true;
        state.Frame = MakeSuccessFrame(8, 4);
        text = DumpThumbnail(adapter, R"({"a":{"maxWidth":1,"maxHeight":1},"maxWidth":4,"maxHeight":4})");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 4, 2);
    }

    void TestFallbackRequestsAndCache()
    {
        NorvesLibBridgeAdapter adapter;
        ProviderState state;
        InstallProvider(adapter, state);

        std::string text = DumpThumbnail(adapter, "{}");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 1, 1);
        assert(state.RequestCount == 1);

        state.NextRequestStatus = FrameCaptureRequestStatus::AlreadyPending;
        text = DumpThumbnail(adapter, "{}");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 1, 1);
        assert(state.RequestCount == 2);

        state.NextRequestStatus = FrameCaptureRequestStatus::NotInitialized;
        text = DumpThumbnail(adapter, "{}");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 1, 1);
        assert(state.RequestCount == 3);

        NorvesLibBridgeAdapter noProviderAdapter;
        text = DumpThumbnail(noProviderAdapter, "{}");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 1, 1);

        state.NextRequestStatus = FrameCaptureRequestStatus::Accepted;
        state.bHasFrame = true;
        state.Frame = MakeSuccessFrame(2, 2);
        text = DumpThumbnail(adapter, "{}");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 2, 2);
        const std::string actualImage = ExtractStringField(text, "imageBase64").value();

        text = DumpThumbnail(adapter, "{}");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 2, 2);
        assert(ExtractStringField(text, "imageBase64").value() == actualImage);
        assert(state.RequestCount == 4);

        text = DumpThumbnail(adapter, R"({"maxWidth":1,"maxHeight":1})");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 1, 1);
        assert(ExtractStringField(text, "imageBase64").value() != actualImage);
        assert(state.RequestCount == 5);

        state.bHasFrame = true;
        state.Frame = MakeCaptureFailureFrame();
        text = DumpThumbnail(adapter, "{}");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 2, 2);
        assert(ExtractStringField(text, "imageBase64").value() == actualImage);
        assert(state.RequestCount == 6);

        state.bHasFrame = true;
        state.Frame = MakeEncodeFailureFrame();
        text = DumpThumbnail(adapter, "{}");
        AssertThumbnailSuccess(text);
        AssertDimensions(text, 2, 2);
        assert(ExtractStringField(text, "imageBase64").value() == actualImage);
        assert(state.RequestCount == 7);
    }
} // namespace

int main()
{
    TestCapabilitiesIncludeViewportThumbnail();
    TestLenientParamsReturnPlaceholderAndRequestCapture();
    TestInvalidDimensionsReturnSuccess();
    TestPartialAndClampedCaps();
    TestFallbackRequestsAndCache();

    std::cout << "NorvesLibBridgeAdapterThumbnailTest passed\n";
    return 0;
}
