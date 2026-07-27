#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
    using namespace NorvesLib::Core;

    Container::String ReadSourceFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return {};
        }

        Container::String result;
        char buffer[4096]{};
        while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0)
        {
            result.append(buffer, static_cast<size_t>(input.gcount()));
        }
        return result;
    }

    Container::String StripCommentsAndLiterals(const Container::String& source)
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
        Container::String result = source;
        for (size_t index = 0; index < result.size(); ++index)
        {
            const char character = result[index];
            const char nextCharacter = index + 1 < result.size() ? result[index + 1] : '\0';
            if (state == ScanState::Code)
            {
                if (character == '/' && nextCharacter == '/')
                {
                    result[index] = result[++index] = ' ';
                    state = ScanState::LineComment;
                }
                else if (character == '/' && nextCharacter == '*')
                {
                    result[index] = result[++index] = ' ';
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
            }
            else if (state == ScanState::LineComment)
            {
                if (character == '\n')
                {
                    state = ScanState::Code;
                }
                else
                {
                    result[index] = ' ';
                }
            }
            else if (state == ScanState::BlockComment)
            {
                result[index] = character == '\n' ? '\n' : ' ';
                if (character == '*' && nextCharacter == '/')
                {
                    result[++index] = ' ';
                    state = ScanState::Code;
                }
            }
            else
            {
                if (character == '\\' && nextCharacter != '\0')
                {
                    result[index] = result[++index] = ' ';
                }
                else
                {
                    result[index] = character == '\n' ? '\n' : ' ';
                    if ((state == ScanState::StringLiteral && character == '"') ||
                        (state == ScanState::CharacterLiteral && character == '\''))
                    {
                        state = ScanState::Code;
                    }
                }
            }
        }
        return result;
    }

    bool ExtractFunctionBlock(
        const Container::String& source,
        const Container::String& signature,
        Container::String& outBlock)
    {
        const size_t signatureOffset = source.find(signature);
        if (signatureOffset == Container::String::npos)
        {
            return false;
        }

        const size_t openingBrace = source.find('{', signatureOffset);
        if (openingBrace == Container::String::npos)
        {
            return false;
        }

        uint32_t depth = 0;
        for (size_t index = openingBrace; index < source.size(); ++index)
        {
            if (source[index] == '{')
            {
                ++depth;
            }
            else if (source[index] == '}' && --depth == 0)
            {
                outBlock = source.substr(openingBrace, index - openingBrace + 1);
                return true;
            }
        }
        return false;
    }

    bool ExtractTickBlock(const Container::String& source, Container::String& outBlock)
    {
        return ExtractFunctionBlock(source, "void ApplicationProcessor::Tick()", outBlock);
    }

    bool TestTickCallsShouldAdvanceSimulationExactlyOnce(const Container::String& tick)
    {
        const size_t first = tick.find("ShouldAdvanceSimulation");
        return first != Container::String::npos &&
            tick.find("ShouldAdvanceSimulation", first + 1) == Container::String::npos;
    }

    bool TestTickPipelineOrderMatchesFixedTimeContract(const Container::String& tick)
    {
        const Container::VariableArray<Container::String> tokens =
        {
            Container::String("CalculateRawDeltaTimeNanoseconds"),
            Container::String("ClampVariableDeltaTime"),
            Container::String("OnUpdate"),
            Container::String("BeginFrameMaintenance"),
            Container::String("ShouldAdvanceSimulation"),
            Container::String("UpdateGameModeStateMachine"),
            Container::String("GetWorld().Tick"),
            Container::String("GetParticleSystem().Tick"),
            Container::String("AdvanceFixedSimulation"),
            Container::String("SyncToSceneView"),
            Container::String("EndFrameMaintenance"),
            Container::String("TickAll")
        };
        size_t previous = 0;
        for (const Container::String& token : tokens)
        {
            const size_t current = tick.find(token, previous);
            if (current == Container::String::npos)
            {
                return false;
            }
            previous = current + token.size();
        }
        return true;
    }

    bool TestPauseBranchExecutesZeroFixedLoops(const Container::String& tick)
    {
        const size_t advance = tick.find("AdvanceFixedSimulation(rawDeltaNanoseconds, bAdvanceSim)");
        const size_t sync = tick.find("SyncToSceneView", advance);
        return advance != Container::String::npos &&
            sync != Container::String::npos &&
            tick.find("for (", advance) > sync;
    }

    bool TestFixedStepPipelineOrderMatchesTransformContract(const Container::String& advance)
    {
        const Container::VariableArray<Container::String> tokens =
        {
            Container::String("world.UpdateWorldTransforms()"),
            Container::String("DispatchPreFixedTick"),
            Container::String("world.DispatchFixedTick"),
            Container::String("DispatchFixedTick"),
            Container::String("world.UpdateWorldTransforms()"),
            Container::String("world.CleanupAfterFixedStep()")
        };
        size_t previous = 0;
        for (const Container::String& token : tokens)
        {
            const size_t current = advance.find(token, previous);
            if (current == Container::String::npos)
            {
                return false;
            }
            previous = current + token.size();
        }
        return true;
    }
}

int main()
{
    const std::filesystem::path sourcePath =
        std::filesystem::path(NORVES_SOURCE_ROOT) / "Library/Core/Private/Engine/ApplicationProcessor.cpp";
    const Container::String source = ReadSourceFile(sourcePath);
    Container::String tick;
    Container::String advance;
    const bool bPassed = !source.empty() &&
        ExtractTickBlock(StripCommentsAndLiterals(source), tick) &&
        ExtractFunctionBlock(
            source,
            "FixedStepAdvanceResult ApplicationProcessor::AdvanceFixedSimulation(",
            advance) &&
        TestTickCallsShouldAdvanceSimulationExactlyOnce(tick) &&
        TestTickPipelineOrderMatchesFixedTimeContract(tick) &&
        TestPauseBranchExecutesZeroFixedLoops(tick) &&
        TestFixedStepPipelineOrderMatchesTransformContract(advance);
    std::cout << (bPassed
        ? "ApplicationFixedStepTickSourceContractTest passed\n"
        : "ApplicationFixedStepTickSourceContractTest failed\n");
    return bPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
