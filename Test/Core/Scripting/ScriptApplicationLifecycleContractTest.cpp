#include "Application/ApplicationHandlerBase.h"
#include "Boot/BootConfig.h"
#include "Component/ScriptComponent.h"
#include "Engine/ApplicationProcessor.h"
#include "Engine/Engine.h"
#include "Engine/NorvesEngine.h"
#include "Logging/LoggingModule.h"
#include "Thread/JobSystem.h"

#include <Windows.h>

#include "Library/ThirdParty/angelscript/upstream/sdk/angelscript/include/angelscript.h"

#include <crtdbg.h>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

class asIScriptEngine;

namespace NorvesLib::Core::Scripting
{
    asIScriptEngine* GetActiveAngelScriptEngine();
}

namespace
{
    constexpr DWORD kExpectedAbortExitCode = 3;

    enum class ChildMode
    {
        Advance,
        Pause,
        DoubleShutdown,
        InitializeFalse,
        PreInitializeThrows,
        PostInitializeThrows,
        PreShutdownThrows,
        ShutdownThrows,
        RetainedFailFast
    };

    struct ChildState
    {
        ChildMode Mode = ChildMode::Advance;
        std::filesystem::path MarkerPath;
        std::vector<std::string> Callbacks;
        NorvesLib::Core::Entity* Owner = nullptr;
        NorvesLib::Math::Vector3 InitialPosition{};
        NorvesLib::Math::Vector3 FinalPosition{};
        uint64_t InitialGcStepCount = 0;
        uint64_t FinalGcStepCount = 0;
        uint32_t InitialBindingCount = 0;
        uint32_t FinalBindingCount = 0;
        uint32_t UpdateCount = 0;
        uint32_t PreRenderCount = 0;
        uint32_t PostRenderCount = 0;
        uint32_t PreShutdownCount = 0;
        uint32_t ShutdownCount = 0;
        uint64_t FrameCount = 0;
        bool bRuntimeObserved = false;
        bool bActiveEngineObserved = false;
        bool bRenderWorldObserved = false;
        bool bRuntimeCleaned = false;
        bool bActiveEngineCleaned = false;
        bool bEngineCleaned = false;
        bool bJobSystemCleaned = false;
        bool bPassed = true;
        std::string Failure;
    };

    ChildState GChildState;

    void Fail(const char* message)
    {
        if (GChildState.bPassed)
        {
            GChildState.bPassed = false;
            GChildState.Failure = message;
        }
    }

    std::string StripCommentsAndLiterals(const std::string& source)
    {
        enum class ScanState
        {
            Code,
            LineComment,
            BlockComment,
            StringLiteral,
            CharacterLiteral
        };

        ScanState state = ScanState::Code;
        std::string result = source;
        for (size_t index = 0; index < result.size(); ++index)
        {
            const char character = result[index];
            const char nextCharacter = index + 1 < result.size() ? result[index + 1] : '\0';

            if (state == ScanState::Code)
            {
                if (character == '/' && nextCharacter == '/')
                {
                    result[index] = ' ';
                    result[++index] = ' ';
                    state = ScanState::LineComment;
                }
                else if (character == '/' && nextCharacter == '*')
                {
                    result[index] = ' ';
                    result[++index] = ' ';
                    state = ScanState::BlockComment;
                }
                else if (character == '"')
                {
                    result[index] = ' ';
                    state = ScanState::StringLiteral;
                }
                else if (character == '\'')
                {
                    result[index] = ' ';
                    state = ScanState::CharacterLiteral;
                }
                continue;
            }

            if (state == ScanState::LineComment)
            {
                if (character == '\n')
                {
                    state = ScanState::Code;
                }
                else
                {
                    result[index] = ' ';
                }
                continue;
            }

            if (state == ScanState::BlockComment)
            {
                result[index] = character == '\n' ? '\n' : ' ';
                if (character == '*' && nextCharacter == '/')
                {
                    result[++index] = ' ';
                    state = ScanState::Code;
                }
                continue;
            }

            if (character == '\\' && nextCharacter != '\0')
            {
                result[index] = ' ';
                result[++index] = ' ';
                continue;
            }

            result[index] = character == '\n' ? '\n' : ' ';
            if ((state == ScanState::StringLiteral && character == '"') ||
                (state == ScanState::CharacterLiteral && character == '\''))
            {
                state = ScanState::Code;
            }
        }
        return result;
    }

    bool ExtractBraceDelimitedBlock(const std::string& source, const std::string& signature, std::string& outBlock)
    {
        const size_t signatureIndex = source.find(signature);
        if (signatureIndex == std::string::npos)
        {
            return false;
        }

        const size_t openingBraceIndex = source.find('{', signatureIndex + signature.size());
        if (openingBraceIndex == std::string::npos)
        {
            return false;
        }

        uint32_t braceDepth = 0;
        for (size_t index = openingBraceIndex; index < source.size(); ++index)
        {
            if (source[index] == '{')
            {
                ++braceDepth;
            }
            else if (source[index] == '}')
            {
                --braceDepth;
                if (braceDepth == 0)
                {
                    outBlock = source.substr(openingBraceIndex, index - openingBraceIndex + 1);
                    return true;
                }
            }
        }
        return false;
    }

    std::string RemoveWhitespace(const std::string& source)
    {
        std::string result;
        result.reserve(source.size());
        for (char character : source)
        {
            if (!std::isspace(static_cast<unsigned char>(character)))
            {
                result.push_back(character);
            }
        }
        return result;
    }

    size_t CountOccurrences(const std::string& source, const std::string& token)
    {
        size_t count = 0;
        size_t index = 0;
        while ((index = source.find(token, index)) != std::string::npos)
        {
            ++count;
            index += token.size();
        }
        return count;
    }

    bool IsIdentifierCharacter(char character)
    {
        return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
    }

    size_t FindExactTokenAfter(const std::string& source, const std::string& token, size_t beginIndex)
    {
        size_t tokenIndex = source.find(token, beginIndex);
        while (tokenIndex != std::string::npos)
        {
            const size_t tokenEndIndex = tokenIndex + token.size();
            const bool bHasIdentifierPrefix = tokenIndex > 0 && IsIdentifierCharacter(source[tokenIndex - 1]);
            const bool bHasIdentifierSuffix =
                tokenEndIndex < source.size() && IsIdentifierCharacter(source[tokenEndIndex]);
            if (!bHasIdentifierPrefix && !bHasIdentifierSuffix)
            {
                return tokenIndex;
            }
            tokenIndex = source.find(token, tokenIndex + 1);
        }
        return std::string::npos;
    }

    bool VerifySourceContracts()
    {
        const std::filesystem::path sourcePath =
            std::filesystem::path(NORVES_SOURCE_ROOT) / "Library/Core/Private/Engine/ApplicationProcessor.cpp";
        std::ifstream input(sourcePath, std::ios::binary);
        if (!input)
        {
            std::cout << "source contract could not open ApplicationProcessor.cpp\n";
            return false;
        }

        const std::string tokenizedSource = StripCommentsAndLiterals(
            std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()));
        std::string transactionBlock;
        std::string initializeBlock;
        std::string shutdownBlock;
        if (!ExtractBraceDelimitedBlock(tokenizedSource, "class ApplicationInitializeTransaction", transactionBlock) ||
            !ExtractBraceDelimitedBlock(tokenizedSource, "bool ApplicationProcessor::Initialize(", initializeBlock) ||
            !ExtractBraceDelimitedBlock(tokenizedSource, "void ApplicationProcessor::Shutdown()", shutdownBlock))
        {
            std::cout << "source contract could not extract required implementation blocks\n";
            return false;
        }

        const std::string compactTransactionBlock = RemoveWhitespace(transactionBlock);
        const bool bSameInstanceShutdown =
            compactTransactionBlock.find("ApplicationProcessor&m_Processor;") != std::string::npos &&
            compactTransactionBlock.find("m_Processor.Shutdown();") != std::string::npos &&
            CountOccurrences(compactTransactionBlock, ".Shutdown();") == 1;
        if (!bSameInstanceShutdown)
        {
            std::cout << "source contract Initialize failure does not delegate to the same processor Shutdown\n";
            return false;
        }

        if (FindExactTokenAfter(initializeBlock, "GEngine->GetRenderWorld().Shutdown", 0) != std::string::npos ||
            FindExactTokenAfter(initializeBlock, "m_Device.reset", 0) != std::string::npos)
        {
            std::cout << "source contract Initialize contains individual cleanup\n";
            return false;
        }

        const std::vector<std::string> cleanupTokens =
        {
            "WaitForRender",
            "QuiesceAsyncAssetProducersAndWait",
            "StopAcceptingTasks",
            "DrainAcceptedFiniteTasks",
            "OnPreShutdown",
            "stateMachine->Shutdown",
            "GetSceneQuery().Clear",
            "GetWorld().Finalize",
            "GetScriptRuntime().Shutdown",
            "GetRenderWorld().Shutdown",
            "m_Device.reset",
            "DestroyEngine",
            "JobSystem::Get().Shutdown"
        };
        size_t previousIndex = 0;
        for (const std::string& cleanupToken : cleanupTokens)
        {
            const size_t tokenIndex = FindExactTokenAfter(shutdownBlock, cleanupToken, previousIndex);
            if (tokenIndex == std::string::npos)
            {
                std::cout << "source contract shutdown order mismatch at " << cleanupToken << "\n";
                return false;
            }
            previousIndex = tokenIndex + cleanupToken.size();
        }

        std::cout << "source contracts passed\n";
        return true;
    }

    void RecordCallback(const char* callback)
    {
        GChildState.Callbacks.emplace_back(callback);
    }

    const char* GetChildModeName(ChildMode mode)
    {
        switch (mode)
        {
        case ChildMode::Advance:
            return "advance";
        case ChildMode::Pause:
            return "pause";
        case ChildMode::DoubleShutdown:
            return "double-shutdown";
        case ChildMode::InitializeFalse:
            return "initialize-false";
        case ChildMode::PreInitializeThrows:
            return "pre-initialize-throws";
        case ChildMode::PostInitializeThrows:
            return "post-initialize-throws";
        case ChildMode::PreShutdownThrows:
            return "pre-shutdown-throws";
        case ChildMode::ShutdownThrows:
            return "shutdown-throws";
        case ChildMode::RetainedFailFast:
            return "retained-failfast";
        }
        return "unknown";
    }

    bool HasOrderedCallbacks(const std::vector<std::string>& expected)
    {
        size_t expectedIndex = 0;
        for (const std::string& callback : GChildState.Callbacks)
        {
            if (expectedIndex < expected.size() && callback == expected[expectedIndex])
            {
                ++expectedIndex;
            }
        }
        return expectedIndex == expected.size();
    }

    bool HasExactCallbacks(const std::vector<std::string>& expected)
    {
        return GChildState.Callbacks == expected;
    }

    bool IsInitializeFailureMode(ChildMode mode)
    {
        return mode == ChildMode::InitializeFalse ||
            mode == ChildMode::PreInitializeThrows ||
            mode == ChildMode::PostInitializeThrows;
    }

    void ObserveStartedResources()
    {
        using namespace NorvesLib::Core;

        GChildState.bRuntimeObserved = GEngine.GetScriptRuntime().IsInitialized();
        GChildState.bActiveEngineObserved = Scripting::GetActiveAngelScriptEngine() != nullptr;
        GChildState.bRenderWorldObserved = Engine::GEngine != nullptr &&
            Engine::GEngine->GetRenderWorld().IsInitialized();
    }

    void VerifyCleanupResources()
    {
        using namespace NorvesLib::Core;

        GChildState.bRuntimeCleaned = !GEngine.GetScriptRuntime().IsInitialized();
        GChildState.bActiveEngineCleaned = Scripting::GetActiveAngelScriptEngine() == nullptr;
        GChildState.bEngineCleaned = Engine::GEngine == nullptr;
        GChildState.bJobSystemCleaned = NorvesLib::Thread::JobSystem::Get().GetWorkerThreadCount() == 0;
        if (!GChildState.bRuntimeCleaned || !GChildState.bActiveEngineCleaned ||
            !GChildState.bEngineCleaned || !GChildState.bJobSystemCleaned)
        {
            Fail("Shutdown left a runtime, AngelScript engine, Engine, or JobSystem resource active");
        }
    }

    void WriteMarker()
    {
        std::ofstream marker(GChildState.MarkerPath, std::ios::binary | std::ios::trunc);
        if (!marker)
        {
            return;
        }

        marker << "mode=" << GetChildModeName(GChildState.Mode)
               << "\nposition_x=" << GChildState.FinalPosition.x
               << "\ninitial_position_x=" << GChildState.InitialPosition.x
               << "\nbinding_initial=" << GChildState.InitialBindingCount
               << "\nbinding_final=" << GChildState.FinalBindingCount
               << "\ngc_initial=" << GChildState.InitialGcStepCount
               << "\ngc_final=" << GChildState.FinalGcStepCount
               << "\nupdate_count=" << GChildState.UpdateCount
               << "\npre_render_count=" << GChildState.PreRenderCount
               << "\npost_render_count=" << GChildState.PostRenderCount
               << "\npre_shutdown_count=" << GChildState.PreShutdownCount
               << "\nshutdown_count=" << GChildState.ShutdownCount
               << "\nframe_count=" << GChildState.FrameCount
               << "\nruntime_observed=" << GChildState.bRuntimeObserved
               << "\nactive_engine_observed=" << GChildState.bActiveEngineObserved
               << "\nrender_world_observed=" << GChildState.bRenderWorldObserved
               << "\nruntime_cleaned=" << GChildState.bRuntimeCleaned
               << "\nactive_engine_cleaned=" << GChildState.bActiveEngineCleaned
               << "\nengine_cleaned=" << GChildState.bEngineCleaned
               << "\njob_system_cleaned=" << GChildState.bJobSystemCleaned
               << "\ncallbacks=";
        for (size_t index = 0; index < GChildState.Callbacks.size(); ++index)
        {
            if (index != 0)
            {
                marker << ',';
            }
            marker << GChildState.Callbacks[index];
        }
        marker << "\nresult=" << (GChildState.bPassed ? "pass" : "fail")
               << "\nfailure=" << GChildState.Failure << "\n";
    }

    void WriteRetainedFailFastMarker()
    {
        std::ofstream marker(GChildState.MarkerPath, std::ios::binary | std::ios::trunc);
        if (!marker)
        {
            return;
        }

        marker << "mode=retained-failfast\n"
               << "handler_reached=1\n"
               << "engine_ref_retained=1\n";
        marker.flush();
    }

    class LifecycleHandler final : public NorvesLib::Core::Application::ApplicationHandlerBase
    {
    public:
        bool OnPreInitialize(const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String>& arguments) override
        {
            (void)arguments;
            RecordCallback("OnPreInitialize");
            if (GChildState.Mode == ChildMode::PreInitializeThrows)
            {
                throw std::runtime_error("OnPreInitialize test exception");
            }
            return true;
        }

        bool OnInitialize() override
        {
            using namespace NorvesLib::Core;

            RecordCallback("OnInitialize");
            ObserveStartedResources();
            if (!GChildState.bRuntimeObserved || !GChildState.bActiveEngineObserved ||
                !GChildState.bRenderWorldObserved)
            {
                Fail("OnInitialize did not observe the started runtime, AngelScript engine, and RenderWorld");
            }
            if (GChildState.Mode == ChildMode::InitializeFalse)
            {
                return false;
            }
            ScriptRuntime& runtime = GEngine.GetScriptRuntime();
            if (!runtime.IsInitialized())
            {
                Fail("ScriptRuntime was not initialized before OnInitialize");
                return true;
            }

            if (GChildState.Mode == ChildMode::RetainedFailFast)
            {
                asIScriptEngine* engine = Scripting::GetActiveAngelScriptEngine();
                if (engine == nullptr || engine->AddRef() <= 0)
                {
                    Fail("retained fail-fast child could not retain the active AngelScript engine");
                    return false;
                }
                WriteRetainedFailFastMarker();
            }

            World& world = Engine::GEngine->GetWorld();
            GChildState.Owner = world.SpawnEntity<Entity>();
            if (GChildState.Owner == nullptr)
            {
                Fail("World failed to spawn test entity");
                return true;
            }

            GChildState.Owner->SetPosition(3.0f, 0.0f, 0.0f);
            GChildState.InitialPosition = GChildState.Owner->GetPosition();
            Component::ScriptComponent* component = new Component::ScriptComponent();
            if (component == nullptr)
            {
                Fail("World failed to create ScriptComponent");
                return true;
            }

            component->getScriptPath() = Container::String("Scripts/Test/ScriptComponentMover.as");
            component->getScriptClassName() = Container::String("ScriptComponentMover");
            if (!GChildState.Owner->AddComponent(component))
            {
                delete component;
                Fail("Entity failed to attach ScriptComponent");
                return true;
            }

            GChildState.InitialBindingCount = runtime.GetDiagnostics().ActiveBindingCount;
            GChildState.InitialGcStepCount = runtime.GetDiagnostics().GcStepCount;
            if (GChildState.InitialBindingCount == 0)
            {
                Fail("ScriptComponent did not create an active binding");
            }
            return true;
        }

        void OnPostInitialize() override
        {
            RecordCallback("OnPostInitialize");
            if (GChildState.Mode == ChildMode::PostInitializeThrows)
            {
                throw std::runtime_error("OnPostInitialize test exception");
            }
        }

        void OnUpdate(float deltaTime) override
        {
            (void)deltaTime;
            RecordCallback("OnUpdate");
            ++GChildState.UpdateCount;
        }

        bool ShouldAdvanceSimulation() const override
        {
            RecordCallback("ShouldAdvanceSimulation");
            return GChildState.Mode == ChildMode::Advance;
        }

        void OnPreRender() override
        {
            RecordCallback("OnPreRender");
            ++GChildState.PreRenderCount;
        }

        void OnPostRender() override
        {
            RecordCallback("OnPostRender");
            ++GChildState.PostRenderCount;
        }

        void OnPreShutdown() override
        {
            using namespace NorvesLib::Core;

            RecordCallback("OnPreShutdown");
            ++GChildState.PreShutdownCount;
            GChildState.FrameCount = Engine::GEngine != nullptr ? Engine::GEngine->GetFrameCount() : 0;
            if (GChildState.Mode != ChildMode::PostInitializeThrows && GChildState.FrameCount < 1)
            {
                Fail("no application frame elapsed before OnPreShutdown");
            }
            if (!GEngine.GetScriptRuntime().IsInitialized())
            {
                Fail("ScriptRuntime was not initialized during OnPreShutdown");
            }

            GChildState.FinalBindingCount = GEngine.GetScriptRuntime().GetDiagnostics().ActiveBindingCount;
            GChildState.FinalGcStepCount = GEngine.GetScriptRuntime().GetDiagnostics().GcStepCount;
            if (GChildState.FinalBindingCount == 0)
            {
                Fail("active ScriptComponent binding disappeared before shutdown");
            }
            if (GChildState.Mode == ChildMode::Pause &&
                GChildState.FinalGcStepCount <= GChildState.InitialGcStepCount)
            {
                Fail("EndFrameMaintenance did not advance GcStepCount");
            }
            if (GChildState.Owner == nullptr)
            {
                Fail("test entity did not survive until OnPreShutdown");
            }
            else if (GChildState.Mode == ChildMode::Advance &&
                     GChildState.Owner->GetPosition().x <= GChildState.InitialPosition.x)
            {
                Fail("advance mode did not move ScriptComponentMover entity");
            }
            else if (GChildState.Mode == ChildMode::Pause &&
                     GChildState.Owner->GetPosition().x != GChildState.InitialPosition.x)
            {
                Fail("pause mode advanced ScriptComponentMover entity");
            }
            if (GChildState.Owner != nullptr)
            {
                GChildState.FinalPosition = GChildState.Owner->GetPosition();
            }
            if (GChildState.Mode == ChildMode::PreShutdownThrows)
            {
                throw std::runtime_error("OnPreShutdown test exception");
            }
        }

        void OnShutdown() override
        {
            RecordCallback("OnShutdown");
            ++GChildState.ShutdownCount;
            if (GChildState.Mode == ChildMode::ShutdownThrows)
            {
                throw std::runtime_error("OnShutdown test exception");
            }
        }
    };

    NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::Application::IApplicationHandler> CreateHandler()
    {
        return NorvesLib::Core::Container::MakeShared<LifecycleHandler>();
    }

    void ConfigureChildProcessReporting()
    {
#ifdef _MSC_VER
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    }

    NorvesLib::Core::Boot::BootConfig MakeBootConfig()
    {
        NorvesLib::Core::Boot::BootConfig config;
        config.WindowTitle = NorvesLib::Core::Container::String("ScriptApplicationLifecycleContractTest");
        config.WindowWidth = 64;
        config.WindowHeight = 64;
        config.bVSync = false;
        config.bEnableMultiThreadedRendering = false;
        config.bEnableDebugConsole = false;
        config.bEnableRHIValidation = false;
        config.bLogToStdout = false;
        config.TargetFrameRate = 0.0f;
        config.Arguments.push_back(NorvesLib::Core::Container::String("--exit-after-frames=2"));
        config.CreateHandler = &CreateHandler;
        return config;
    }

    int RunChild(ChildMode mode, const std::filesystem::path& markerPath)
    {
        using namespace NorvesLib::Core;

        ConfigureChildProcessReporting();
        GChildState = {};
        GChildState.Mode = mode;
        GChildState.MarkerPath = markerPath;

        const Logging::LogConfig logConfig = Logging::CreateLogConfig(
            Logging::LogLevel::Trace,
            Logging::LogOutput::None,
            Container::String("ScriptApplicationLifecycleContractTest.log"),
            false);
        if (!Logging::InitializeLogging(logConfig))
        {
            Fail("logging initialization failed");
        }

        Engine::ApplicationProcessor& processor = Engine::ApplicationProcessor::GetInstance();
        const bool bInitialized = processor.Initialize(MakeBootConfig());
        if (IsInitializeFailureMode(mode))
        {
            if (bInitialized)
            {
                Fail("ApplicationProcessor::Initialize succeeded for an initialize failure child");
            }
        }
        else if (!bInitialized)
        {
            Fail("ApplicationProcessor::Initialize failed before handler execution");
        }
        else
        {
            const int runExitCode = processor.Run();
            if (runExitCode != EXIT_SUCCESS)
            {
                Fail("ApplicationProcessor::Run returned nonzero");
            }
        }

        processor.Shutdown();
        VerifyCleanupResources();
        if (mode == ChildMode::DoubleShutdown)
        {
            const uint32_t preShutdownCount = GChildState.PreShutdownCount;
            const uint32_t shutdownCount = GChildState.ShutdownCount;
            processor.Shutdown();
            if (GChildState.PreShutdownCount != preShutdownCount || GChildState.ShutdownCount != shutdownCount)
            {
                Fail("second Shutdown invoked lifecycle callbacks");
            }
        }
        Engine::ApplicationProcessor::DestroyInstance();

        if (mode == ChildMode::InitializeFalse && !HasExactCallbacks(
            { "OnPreInitialize", "OnInitialize" }))
        {
            Fail("OnInitialize false did not stop before shutdown callbacks");
        }
        if (mode == ChildMode::PreInitializeThrows && !HasExactCallbacks(
            { "OnPreInitialize" }))
        {
            Fail("OnPreInitialize exception reached a later lifecycle callback");
        }
        if (mode == ChildMode::PostInitializeThrows && !HasExactCallbacks(
            { "OnPreInitialize", "OnInitialize", "OnPostInitialize", "OnPreShutdown", "OnShutdown" }))
        {
            Fail("OnPostInitialize exception did not execute exact-once shutdown callbacks");
        }
        if (mode == ChildMode::InitializeFalse || mode == ChildMode::PreInitializeThrows)
        {
            if (GChildState.PreShutdownCount != 0 || GChildState.ShutdownCount != 0)
            {
                Fail("uninitialized handler received a shutdown callback");
            }
        }

        const std::vector<std::string> expectedCallbacks =
        {
            "OnPreInitialize",
            "OnInitialize",
            "OnPostInitialize",
            "OnUpdate",
            "ShouldAdvanceSimulation",
            "OnPreRender",
            "OnPostRender",
            "OnPreShutdown",
            "OnShutdown"
        };
        if (!IsInitializeFailureMode(mode) && !HasOrderedCallbacks(expectedCallbacks))
        {
            Fail("lifecycle callback order was not observed");
        }
        if (!IsInitializeFailureMode(mode) &&
            (GChildState.UpdateCount == 0 || GChildState.PreRenderCount == 0 || GChildState.PostRenderCount == 0))
        {
            Fail("frame callbacks did not run");
        }
        if ((mode == ChildMode::PostInitializeThrows || !IsInitializeFailureMode(mode)) &&
            (GChildState.PreShutdownCount != 1 || GChildState.ShutdownCount != 1))
        {
            Fail("shutdown callbacks were not exact-once");
        }

        WriteMarker();
        Logging::ShutdownLogging();
        return GChildState.bPassed ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    bool ReadMarker(const std::filesystem::path& markerPath, std::string& outMarker)
    {
        std::ifstream input(markerPath, std::ios::binary);
        if (!input)
        {
            return false;
        }
        outMarker.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        return true;
    }

    bool RunChildProcess(const char* childMode, const char* expectedMode, bool bExpectFailFast = false)
    {
        wchar_t executablePath[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, executablePath, MAX_PATH) == 0)
        {
            std::cout << "failed to find test executable\n";
            return false;
        }

        const std::filesystem::path markerPath = std::filesystem::temp_directory_path() /
            (std::string("norves-script-application-") + std::to_string(GetCurrentProcessId()) + "-" + childMode + ".marker");
        std::error_code removeError;
        std::filesystem::remove(markerPath, removeError);

        const std::wstring markerArgument = markerPath.wstring();
        wchar_t commandLine[4096]{};
        if (swprintf_s(commandLine, L"\"%s\" --child=%S \"%s\"", executablePath, childMode, markerArgument.c_str()) <= 0)
        {
            std::cout << "failed to form child command line\n";
            return false;
        }

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};
        if (CreateProcessW(nullptr, commandLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &processInfo) == FALSE)
        {
            std::cout << "failed to create " << childMode << " child\n";
            return false;
        }

        const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 45000);
        if (waitResult == WAIT_TIMEOUT)
        {
            TerminateProcess(processInfo.hProcess, EXIT_FAILURE);
            WaitForSingleObject(processInfo.hProcess, INFINITE);
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            std::filesystem::remove(markerPath, removeError);
            std::cout << childMode << " child timed out\n";
            return false;
        }

        DWORD exitCode = EXIT_FAILURE;
        const bool bReadExitCode = GetExitCodeProcess(processInfo.hProcess, &exitCode) != FALSE;
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);

        std::string marker;
        const bool bReadMarker = ReadMarker(markerPath, marker);
        std::filesystem::remove(markerPath, removeError);
        if (!bReadExitCode || waitResult != WAIT_OBJECT_0 || !bReadMarker)
        {
            std::cout << childMode << " child failed exit=" << exitCode << " marker=" << (bReadMarker ? marker : "missing") << "\n";
            return false;
        }
        if (bExpectFailFast)
        {
            if (exitCode != kExpectedAbortExitCode ||
                marker.find("handler_reached=1") == std::string::npos ||
                marker.find("engine_ref_retained=1") == std::string::npos)
            {
                std::cout << childMode << " child did not reach the expected fail-fast boundary: exit=" << exitCode
                          << " marker=" << marker << "\n";
                return false;
            }

            std::cout << childMode << " child reached fail-fast: exit=" << exitCode << " marker=" << marker;
            return true;
        }
        if (exitCode != EXIT_SUCCESS)
        {
            std::cout << childMode << " child failed exit=" << exitCode << " marker=" << marker << "\n";
            return false;
        }
        if (marker.find(std::string("mode=") + expectedMode) == std::string::npos ||
            marker.find("result=pass") == std::string::npos)
        {
            std::cout << childMode << " child reported invalid marker: " << marker << "\n";
            return false;
        }

        std::cout << childMode << " child passed: " << marker;
        return true;
    }

    bool TryParseChildMode(const char* argument, ChildMode& outMode)
    {
        if (std::strcmp(argument, "--child=advance") == 0)
        {
            outMode = ChildMode::Advance;
            return true;
        }
        if (std::strcmp(argument, "--child=pause") == 0)
        {
            outMode = ChildMode::Pause;
            return true;
        }
        if (std::strcmp(argument, "--child=double-shutdown") == 0)
        {
            outMode = ChildMode::DoubleShutdown;
            return true;
        }
        if (std::strcmp(argument, "--child=initialize-false") == 0)
        {
            outMode = ChildMode::InitializeFalse;
            return true;
        }
        if (std::strcmp(argument, "--child=pre-initialize-throws") == 0)
        {
            outMode = ChildMode::PreInitializeThrows;
            return true;
        }
        if (std::strcmp(argument, "--child=post-initialize-throws") == 0)
        {
            outMode = ChildMode::PostInitializeThrows;
            return true;
        }
        if (std::strcmp(argument, "--child=pre-shutdown-throws") == 0)
        {
            outMode = ChildMode::PreShutdownThrows;
            return true;
        }
        if (std::strcmp(argument, "--child=shutdown-throws") == 0)
        {
            outMode = ChildMode::ShutdownThrows;
            return true;
        }
        if (std::strcmp(argument, "--child=retained-failfast") == 0)
        {
            outMode = ChildMode::RetainedFailFast;
            return true;
        }
        return false;
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount == 3)
    {
        ChildMode childMode{};
        if (!TryParseChildMode(arguments[1], childMode))
        {
            return EXIT_FAILURE;
        }
        return RunChild(childMode, std::filesystem::path(arguments[2]));
    }

    if (argumentCount != 1)
    {
        std::cout << "unexpected arguments\n";
        return EXIT_FAILURE;
    }

    bool bBehaviorPassed = true;
    bBehaviorPassed &= RunChildProcess("advance", "advance");
    bBehaviorPassed &= RunChildProcess("pause", "pause");
    bBehaviorPassed &= RunChildProcess("double-shutdown", "double-shutdown");
    bBehaviorPassed &= RunChildProcess("initialize-false", "initialize-false");
    bBehaviorPassed &= RunChildProcess("pre-initialize-throws", "pre-initialize-throws");
    bBehaviorPassed &= RunChildProcess("post-initialize-throws", "post-initialize-throws");
    bBehaviorPassed &= RunChildProcess("pre-shutdown-throws", "pre-shutdown-throws");
    bBehaviorPassed &= RunChildProcess("shutdown-throws", "shutdown-throws");
    bBehaviorPassed &= RunChildProcess("retained-failfast", "retained-failfast", true);
    const bool bSourceContractsPassed = VerifySourceContracts();
    const bool bPassed = bBehaviorPassed && bSourceContractsPassed;
    std::cout << (bPassed ? "ScriptApplicationLifecycleContractTest passed\n" : "ScriptApplicationLifecycleContractTest failed\n");
    return bPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
