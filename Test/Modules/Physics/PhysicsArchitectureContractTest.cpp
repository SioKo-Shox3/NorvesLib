// PhysicsArchitectureContractTest — M8 の依存方向・公開境界・禁止範囲を固定する。

#include "Container/Containers.h"
#include "GameMode/GameModeScope.h"
#include "Module/IModule.h"
#include "Object/World.h"
#include "Physics/ColliderComponent.h"
#include "Physics/IPhysicsModule.h"
#include "Physics/PhysicsBroadphase.h"
#include "Physics/PhysicsModule.h"
#include "Physics/RigidBodyComponent.h"
#include "Rendering/FramePacket.h"
#include "Rendering/RenderThread.h"
#include "Rendering/SceneProxy.h"
#include "Scene/SceneQuery.h"

#include <Windows.h>

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Scene;
    using namespace NorvesLib::Modules::Physics;

    template <typename T>
    using TArrayElement = typename std::remove_cvref_t<T>::value_type;

    static_assert(std::is_base_of_v<Module::IModule, IPhysicsModule>);
    static_assert(!std::is_base_of_v<IPhysicsSceneQueryProvider, IPhysicsModule>);
    static_assert(std::is_final_v<PhysicsModule>);
    static_assert(std::is_base_of_v<IPhysicsSceneQueryProvider, PhysicsModule>);
    static_assert(!std::is_convertible_v<PhysicsModule*, IPhysicsSceneQueryProvider*>);
    static_assert(std::is_same_v<
        decltype(&ColliderComponent::GetColliderHandle),
        ColliderHandle (ColliderComponent::*)() const>);
    static_assert(std::is_same_v<
        decltype(&RigidBodyComponent::GetBodyHandle),
        BodyHandle (RigidBodyComponent::*)() const>);
    static_assert(!std::is_pointer_v<decltype(PhysicsRaycastHit::Collider)>);
    static_assert(!std::is_pointer_v<decltype(PhysicsRaycastHit::Body)>);
    static_assert(!std::is_pointer_v<decltype(PhysicsRaycastHit::Entity)>);
    static_assert(!std::is_pointer_v<decltype(PhysicsOverlapHit::Collider)>);
    static_assert(!std::is_pointer_v<decltype(PhysicsOverlapHit::Body)>);
    static_assert(!std::is_pointer_v<decltype(PhysicsOverlapHit::Entity)>);
    static_assert(!std::is_pointer_v<decltype(PhysicsContactEvent::Self)>);
    static_assert(!std::is_pointer_v<decltype(PhysicsContactEvent::Other)>);
    static_assert(!std::is_pointer_v<decltype(PhysicsContactEvent::Contact)>);
    static_assert(!std::is_pointer_v<decltype(Rendering::FramePacket::Scene)>);
    static_assert(!std::is_pointer_v<decltype(Rendering::FramePacket::DrawCommands)>);
    static_assert(!std::is_pointer_v<decltype(Rendering::FramePacket::InstanceData)>);
    static_assert(!std::is_pointer_v<decltype(Rendering::FramePacket::DebugLineVertices)>);
    static_assert(!std::is_pointer_v<decltype(Rendering::FramePacket::Views)>);
    static_assert(!std::is_pointer_v<decltype(Rendering::SceneProxy::MainCamera)>);
    static_assert(!std::is_pointer_v<decltype(Rendering::SceneProxy::MeshProxies)>);
    static_assert(!std::is_pointer_v<decltype(Rendering::SceneProxy::LightProxies)>);
    static_assert(!std::is_same_v<TArrayElement<decltype(Rendering::FramePacket::DrawCommands)>, ColliderComponent>);
    static_assert(!std::is_same_v<TArrayElement<decltype(Rendering::FramePacket::InstanceData)>, RigidBodyComponent>);
    static_assert(!std::is_same_v<TArrayElement<decltype(Rendering::FramePacket::Views)>, Entity>);
    static_assert(!std::is_same_v<TArrayElement<decltype(Rendering::SceneProxy::MeshProxies)>, ColliderComponent>);
    static_assert(!std::is_same_v<TArrayElement<decltype(Rendering::SceneProxy::LightProxies)>, Entity>);
    static_assert(sizeof(Rendering::FramePacket) == 648);
    static_assert(alignof(Rendering::FramePacket) == 8);
    static_assert(sizeof(Rendering::SceneProxy) == 288);
    static_assert(alignof(Rendering::SceneProxy) == 8);

    bool IsAsciiIdentifierCharacter(char character)
    {
        return character >= 'a' && character <= 'z'
            || character >= 'A' && character <= 'Z'
            || character >= '0' && character <= '9'
            || character == '_';
    }

    bool IsAsciiWhitespace(char character)
    {
        return character == ' ' || character == '\t' || character == '\r' || character == '\n';
    }

    bool EndsWith(const Container::String& value, const char* suffix)
    {
        const Container::String suffixString(suffix);
        return value.size() >= suffixString.size()
            && value.substr(value.size() - suffixString.size()) == suffixString;
    }

    Container::String MakeSourcePath(const char* relativePath)
    {
        Container::String path(NORVES_SOURCE_ROOT);
        path += "/";
        path += relativePath;
        return path;
    }

    bool ReadFileBytes(const Container::String& path, Container::String& outContents)
    {
        outContents.clear();
        HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        LARGE_INTEGER fileSize{};
        if (GetFileSizeEx(file, &fileSize) == FALSE || fileSize.QuadPart < 0
            || static_cast<uint64_t>(fileSize.QuadPart) > std::numeric_limits<uint32_t>::max())
        {
            CloseHandle(file);
            return false;
        }

        Container::VariableArray<char> bytes;
        bytes.resize(static_cast<size_t>(fileSize.QuadPart));
        size_t offset = 0;
        while (offset < bytes.size())
        {
            const size_t remaining = bytes.size() - offset;
            const DWORD requested = static_cast<DWORD>(remaining > static_cast<size_t>(std::numeric_limits<DWORD>::max())
                ? std::numeric_limits<DWORD>::max() : remaining);
            DWORD bytesRead = 0;
            if (ReadFile(file, bytes.data() + offset, requested, &bytesRead, nullptr) == FALSE || bytesRead == 0)
            {
                CloseHandle(file);
                return false;
            }
            offset += bytesRead;
        }
        CloseHandle(file);
        outContents.append(bytes.data(), bytes.size());
        return true;
    }

    Container::String StripCommentsAndLiterals(const Container::String& source)
    {
        enum class EScanState
        {
            Code,
            LineComment,
            BlockComment,
            StringLiteral,
            CharacterLiteral,
        };

        Container::String result = source;
        EScanState state = EScanState::Code;
        for (size_t index = 0; index < result.size(); ++index)
        {
            const char character = result[index];
            const char nextCharacter = index + 1 < result.size() ? result[index + 1] : '\0';
            if (state == EScanState::Code)
            {
                if (character == '/' && nextCharacter == '/')
                {
                    result[index] = result[++index] = ' ';
                    state = EScanState::LineComment;
                }
                else if (character == '/' && nextCharacter == '*')
                {
                    result[index] = result[++index] = ' ';
                    state = EScanState::BlockComment;
                }
                else if (character == '"')
                {
                    result[index] = ' ';
                    state = EScanState::StringLiteral;
                }
                else if (character == '\'')
                {
                    result[index] = ' ';
                    state = EScanState::CharacterLiteral;
                }
            }
            else if (state == EScanState::LineComment)
            {
                if (character == '\n')
                {
                    state = EScanState::Code;
                }
                else
                {
                    result[index] = ' ';
                }
            }
            else if (state == EScanState::BlockComment)
            {
                result[index] = character == '\n' ? '\n' : ' ';
                if (character == '*' && nextCharacter == '/')
                {
                    result[++index] = ' ';
                    state = EScanState::Code;
                }
            }
            else if (character == '\\' && nextCharacter != '\0')
            {
                result[index] = result[++index] = ' ';
            }
            else
            {
                result[index] = character == '\n' ? '\n' : ' ';
                if ((state == EScanState::StringLiteral && character == '"')
                    || (state == EScanState::CharacterLiteral && character == '\''))
                {
                    state = EScanState::Code;
                }
            }
        }
        return result;
    }

    bool HasIdentifier(const Container::String& code, const char* identifier)
    {
        const Container::String token(identifier);
        size_t offset = code.find(token);
        while (offset != Container::String::npos)
        {
            const bool bBeforeIsIdentifier = offset > 0 && IsAsciiIdentifierCharacter(code[offset - 1]);
            const size_t end = offset + token.size();
            const bool bAfterIsIdentifier = end < code.size() && IsAsciiIdentifierCharacter(code[end]);
            if (!bBeforeIsIdentifier && !bAfterIsIdentifier)
            {
                return true;
            }
            offset = code.find(token, end);
        }
        return false;
    }

    Container::VariableArray<Container::String> TokenizeCode(const Container::String& source)
    {
        const Container::String code = StripCommentsAndLiterals(source);
        Container::VariableArray<Container::String> tokens;
        for (size_t index = 0; index < code.size();)
        {
            if (IsAsciiWhitespace(code[index]))
            {
                ++index;
                continue;
            }
            if (IsAsciiIdentifierCharacter(code[index]))
            {
                const size_t begin = index;
                while (index < code.size() && IsAsciiIdentifierCharacter(code[index]))
                {
                    ++index;
                }
                tokens.emplace_back(code.substr(begin, index - begin));
                continue;
            }
            tokens.emplace_back(1, code[index++]);
        }
        return tokens;
    }

    uint32_t CountRawPointerType(
        const Container::VariableArray<Container::String>& tokens,
        const char* typeName)
    {
        uint32_t count = 0;
        for (size_t index = 0; index + 1 < tokens.size(); ++index)
        {
            if (tokens[index] == Container::String(typeName) && tokens[index + 1] == Container::String("*"))
            {
                ++count;
            }
        }
        return count;
    }

    struct IncludeMacro
    {
        Container::String Name;
        Container::String Path;
    };

    void SkipPreprocessorWhitespace(const Container::String& line, size_t& inOutOffset)
    {
        while (inOutOffset < line.size() && (line[inOutOffset] == ' ' || line[inOutOffset] == '\t' || line[inOutOffset] == '\r'))
        {
            ++inOutOffset;
        }
    }

    Container::String ReadPreprocessorIdentifier(const Container::String& line, size_t& inOutOffset)
    {
        const size_t begin = inOutOffset;
        while (inOutOffset < line.size() && IsAsciiIdentifierCharacter(line[inOutOffset]))
        {
            ++inOutOffset;
        }
        return line.substr(begin, inOutOffset - begin);
    }

    bool ReadIncludeLiteral(const Container::String& line, size_t& inOutOffset, Container::String& outPath)
    {
        SkipPreprocessorWhitespace(line, inOutOffset);
        if (inOutOffset >= line.size() || (line[inOutOffset] != '"' && line[inOutOffset] != '<'))
        {
            return false;
        }

        const char terminator = line[inOutOffset++] == '"' ? '"' : '>';
        const size_t begin = inOutOffset;
        while (inOutOffset < line.size() && line[inOutOffset] != terminator)
        {
            ++inOutOffset;
        }
        if (inOutOffset >= line.size())
        {
            return false;
        }
        outPath = line.substr(begin, inOutOffset - begin);
        return true;
    }

    Container::VariableArray<Container::String> CollectIncludePaths(const Container::String& source)
    {
        Container::VariableArray<Container::String> includes;
        Container::VariableArray<IncludeMacro> macros;
        bool bInBlockComment = false;
        size_t lineBegin = 0;
        while (lineBegin < source.size())
        {
            const size_t lineEnd = source.find('\n', lineBegin);
            const Container::String line = source.substr(lineBegin, lineEnd == Container::String::npos ? lineEnd : lineEnd - lineBegin);
            size_t offset = 0;
            while (offset + 1 < line.size())
            {
                if (bInBlockComment)
                {
                    const size_t commentEnd = line.find("*/", offset);
                    if (commentEnd == Container::String::npos)
                    {
                        offset = line.size();
                        break;
                    }
                    bInBlockComment = false;
                    offset = commentEnd + 2;
                }
                else if (line[offset] == '/' && line[offset + 1] == '*')
                {
                    bInBlockComment = true;
                    offset += 2;
                }
                else if (line[offset] == '/' && line[offset + 1] == '/')
                {
                    offset = line.size();
                }
                else if (line[offset] == ' ' || line[offset] == '\t' || line[offset] == '\r')
                {
                    ++offset;
                }
                else
                {
                    break;
                }
            }

            if (!bInBlockComment && offset < line.size() && line[offset] == '#')
            {
                ++offset;
                SkipPreprocessorWhitespace(line, offset);
                const Container::String directive = ReadPreprocessorIdentifier(line, offset);
                if (directive == Container::String("define"))
                {
                    SkipPreprocessorWhitespace(line, offset);
                    const Container::String name = ReadPreprocessorIdentifier(line, offset);
                    Container::String path;
                    if (!name.empty() && ReadIncludeLiteral(line, offset, path))
                    {
                        macros.push_back({name, path});
                    }
                }
                else if (directive == Container::String("include"))
                {
                    Container::String path;
                    if (ReadIncludeLiteral(line, offset, path))
                    {
                        includes.push_back(path);
                    }
                    else
                    {
                        SkipPreprocessorWhitespace(line, offset);
                        const Container::String macroName = ReadPreprocessorIdentifier(line, offset);
                        for (const IncludeMacro& macro : macros)
                        {
                            if (macro.Name == macroName)
                            {
                                includes.push_back(macro.Path);
                                break;
                            }
                        }
                    }
                }
            }

            lineBegin = lineEnd == Container::String::npos ? source.size() : lineEnd + 1;
        }
        return includes;
    }

    bool HasPhysicsInclude(const Container::String& source)
    {
        const Container::VariableArray<Container::String> includes = CollectIncludePaths(source);
        for (const Container::String& includePath : includes)
        {
            size_t segmentBegin = 0;
            while (segmentBegin < includePath.size())
            {
                while (segmentBegin < includePath.size() && (includePath[segmentBegin] == '/' || includePath[segmentBegin] == '\\'))
                {
                    ++segmentBegin;
                }
                size_t segmentEnd = segmentBegin;
                while (segmentEnd < includePath.size() && includePath[segmentEnd] != '/' && includePath[segmentEnd] != '\\')
                {
                    ++segmentEnd;
                }
                if (includePath.substr(segmentBegin, segmentEnd - segmentBegin) == Container::String("Physics"))
                {
                    return true;
                }
                segmentBegin = segmentEnd + 1;
            }
        }
        return false;
    }

    bool HasVulkanBackendInclude(const Container::String& source)
    {
        const Container::VariableArray<Container::String> includes = CollectIncludePaths(source);
        for (const Container::String& includePath : includes)
        {
            Container::String previousSegment;
            size_t segmentBegin = 0;
            while (segmentBegin < includePath.size())
            {
                while (segmentBegin < includePath.size() && (includePath[segmentBegin] == '/' || includePath[segmentBegin] == '\\'))
                {
                    ++segmentBegin;
                }
                size_t segmentEnd = segmentBegin;
                while (segmentEnd < includePath.size() && includePath[segmentEnd] != '/' && includePath[segmentEnd] != '\\')
                {
                    ++segmentEnd;
                }
                const Container::String segment = includePath.substr(segmentBegin, segmentEnd - segmentBegin);
                if (previousSegment == Container::String("RHI") && segment == Container::String("Vulkan"))
                {
                    return true;
                }
                previousSegment = segment;
                segmentBegin = segmentEnd + 1;
            }
        }
        return false;
    }

    bool ViolatesCorePhysicsDependency(const Container::String& source)
    {
        const Container::String code = StripCommentsAndLiterals(source);
        return HasPhysicsInclude(source)
            || HasIdentifier(code, "NorvesModule_Physics")
            || HasIdentifier(code, "PhysicsModule")
            || HasIdentifier(code, "IPhysicsModule");
    }

    Container::VariableArray<Container::String> TokenizeCMake(const Container::String& source)
    {
        Container::VariableArray<Container::String> tokens;
        bool bInComment = false;
        for (size_t index = 0; index < source.size();)
        {
            const char character = source[index];
            if (bInComment)
            {
                if (character == '\n')
                {
                    bInComment = false;
                }
                ++index;
                continue;
            }
            if (character == '#')
            {
                bInComment = true;
                ++index;
                continue;
            }
            if (character == '"')
            {
                const size_t begin = ++index;
                while (index < source.size())
                {
                    if (source[index] == '\\' && index + 1 < source.size())
                    {
                        index += 2;
                    }
                    else if (source[index] == '"')
                    {
                        tokens.emplace_back(source.substr(begin, index - begin));
                        ++index;
                        break;
                    }
                    else
                    {
                        ++index;
                    }
                }
                continue;
            }
            if (IsAsciiWhitespace(character))
            {
                ++index;
                continue;
            }
            if (character == '(' || character == ')')
            {
                tokens.emplace_back(1, character);
                ++index;
                continue;
            }
            const size_t begin = index;
            while (index < source.size() && !IsAsciiWhitespace(source[index])
                && source[index] != '(' && source[index] != ')' && source[index] != '#'
                && source[index] != '"')
            {
                ++index;
            }
            if (index > begin)
            {
                tokens.emplace_back(source.substr(begin, index - begin));
            }
        }
        return tokens;
    }

    bool HasCMakeTargetLink(
        const Container::VariableArray<Container::String>& tokens,
        const char* target,
        const char* visibility,
        const char* dependency)
    {
        for (size_t index = 0; index + 2 < tokens.size(); ++index)
        {
            if (tokens[index] == Container::String("target_link_libraries")
                && tokens[index + 1] == Container::String("(")
                && tokens[index + 2] == Container::String(target))
            {
                Container::String activeVisibility;
                for (size_t argument = index + 3; argument < tokens.size() && tokens[argument] != Container::String(")"); ++argument)
                {
                    if (tokens[argument] == Container::String("PRIVATE")
                        || tokens[argument] == Container::String("PUBLIC")
                        || tokens[argument] == Container::String("INTERFACE"))
                    {
                        activeVisibility = tokens[argument];
                    }
                    else if (activeVisibility == Container::String(visibility)
                        && tokens[argument] == Container::String(dependency))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool IsSourceFile(const Container::String& path)
    {
        return EndsWith(path, ".cpp") || EndsWith(path, ".h") || EndsWith(path, ".hpp") || EndsWith(path, ".inl");
    }

    bool CollectSourceFiles(const Container::String& directory, Container::VariableArray<Container::String>& outFiles)
    {
        Container::String pattern = directory;
        pattern += "/*";
        WIN32_FIND_DATAA findData{};
        HANDLE findHandle = FindFirstFileA(pattern.c_str(), &findData);
        if (findHandle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        do
        {
            const Container::String name(findData.cFileName);
            if (name == Container::String(".") || name == Container::String(".."))
            {
                continue;
            }
            Container::String path = directory;
            path += "/";
            path += name;
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                if (!CollectSourceFiles(path, outFiles))
                {
                    FindClose(findHandle);
                    return false;
                }
            }
            else if (IsSourceFile(path))
            {
                outFiles.push_back(path);
            }
        } while (FindNextFileA(findHandle, &findData) != FALSE);

        FindClose(findHandle);
        return true;
    }

    bool ExtractFunctionBlock(
        const Container::String& source,
        const char* signature,
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

    bool HasOwnerGuardBeforeMemberMutation(const Container::String& source, const char* signature)
    {
        Container::String block;
        if (!ExtractFunctionBlock(StripCommentsAndLiterals(source), signature, block))
        {
            return false;
        }
        const size_t guard = block.find("if (!IsOwnerThread(");
        const size_t mutation = block.find("m_");
        if (guard == Container::String::npos || (mutation != Container::String::npos && mutation < guard))
        {
            return false;
        }

        const size_t branchBegin = block.find('{', guard);
        if (branchBegin == Container::String::npos)
        {
            return false;
        }
        uint32_t depth = 0;
        for (size_t index = branchBegin; index < block.size(); ++index)
        {
            if (block[index] == '{')
            {
                ++depth;
            }
            else if (block[index] == '}' && --depth == 0)
            {
                return block.substr(branchBegin, index - branchBegin).find("return") != Container::String::npos;
            }
        }
        return false;
    }

    bool HasValidationBeforeComponentMutation(
        const Container::String& source,
        const char* signature,
        const char* validation)
    {
        Container::String block;
        if (!ExtractFunctionBlock(StripCommentsAndLiterals(source), signature, block))
        {
            return false;
        }
        const size_t guard = block.find(validation);
        const size_t mutation = block.find("component.m_");
        if (guard == Container::String::npos || mutation == Container::String::npos || guard > mutation)
        {
            return false;
        }

        const size_t successCheck = block.find("result != EPhysicsResult::Success", guard);
        const size_t rejection = block.find("return result", successCheck);
        return successCheck != Container::String::npos && rejection != Container::String::npos && rejection < mutation;
    }

    bool HasReadinessGuardBeforeMemberRead(const Container::String& source, const char* signature)
    {
        Container::String block;
        if (!ExtractFunctionBlock(StripCommentsAndLiterals(source), signature, block))
        {
            return false;
        }

        const size_t readiness = block.find("GetReadinessResult(");
        const size_t rejection = block.find("return readiness", readiness);
        const size_t memberRead = block.find("m_", readiness);
        return readiness != Container::String::npos && rejection != Container::String::npos
            && (memberRead == Container::String::npos || rejection < memberRead);
    }

    bool HasIdentifierPrefix(const Container::String& code, const char* prefix)
    {
        const Container::VariableArray<Container::String> tokens = TokenizeCode(code);
        const Container::String prefixString(prefix);
        for (const Container::String& token : tokens)
        {
            if (token.size() >= prefixString.size() && token.substr(0, prefixString.size()) == prefixString)
            {
                return true;
            }
        }
        return false;
    }

    bool HasForbiddenPhysicsFeature(const Container::String& source)
    {
        const Container::VariableArray<Container::String> tokens = TokenizeCode(source);
        const char* forbiddenIdentifiers[] =
        {
            "CCD", "Continuous", "Substep", "RunSubsteps", "Friction", "Restitution", "Joint", "Manifold",
            "MeshCollision", "TriangleMesh", "ConvexMesh",
        };
        for (const Container::String& token : tokens)
        {
            for (const char* identifier : forbiddenIdentifiers)
            {
                if (token == Container::String(identifier))
                {
                    return true;
                }
            }
            if ((token.size() >= Container::String("Sweep").size()
                    && token.substr(0, Container::String("Sweep").size()) == Container::String("Sweep")
                    && token != Container::String("SweepEndpoint"))
                || (token.size() > Container::String("Continuous").size()
                    && token.substr(0, Container::String("Continuous").size()) == Container::String("Continuous"))
                || (token.size() > Container::String("Substep").size()
                    && token.substr(0, Container::String("Substep").size()) == Container::String("Substep"))
                || (token.size() >= Container::String("OverlapStay").size()
                    && token.substr(0, Container::String("OverlapStay").size()) == Container::String("OverlapStay"))
                || (token.size() >= Container::String("HitStay").size()
                    && token.substr(0, Container::String("HitStay").size()) == Container::String("HitStay"))
                || (token.size() >= Container::String("EndHit").size()
                    && token.substr(0, Container::String("EndHit").size()) == Container::String("EndHit"))
                || (token.size() >= Container::String("OnOverlapStay").size()
                    && token.substr(0, Container::String("OnOverlapStay").size()) == Container::String("OnOverlapStay"))
                || (token.size() >= Container::String("OnHitStay").size()
                    && token.substr(0, Container::String("OnHitStay").size()) == Container::String("OnHitStay"))
                || (token.size() >= Container::String("OnEndHit").size()
                    && token.substr(0, Container::String("OnEndHit").size()) == Container::String("OnEndHit")))
            {
                return true;
            }
        }
        return false;
    }

    bool ViolatesPhysicsPrivateScope(const Container::String& source)
    {
        const Container::String code = StripCommentsAndLiterals(source);
        return HasForbiddenPhysicsFeature(code)
            || HasIdentifier(code, "GetWorld")
            || HasIdentifier(code, "GetObjects");
    }

    bool ViolatesBroadphaseIsolation(const Container::String& source)
    {
        const Container::String code = StripCommentsAndLiterals(source);
        const Container::VariableArray<Container::String> includes = CollectIncludePaths(source);
        for (const Container::String& includePath : includes)
        {
            if (includePath == Container::String("Scene/SceneQuery.h"))
            {
                continue;
            }
            if (includePath.find("SceneQuery") != Container::String::npos || includePath.find("BVH") != Container::String::npos)
            {
                return true;
            }
        }
        const char* forbiddenIdentifiers[] = {"SceneQuery"};
        for (const char* identifier : forbiddenIdentifiers)
        {
            if (HasIdentifier(code, identifier))
            {
                return true;
            }
        }
        return HasForbiddenPhysicsFeature(code) || HasIdentifierPrefix(code, "BVH");
    }

    bool HasForbiddenPublicRawPointer(const Container::String& source)
    {
        const Container::VariableArray<Container::String> tokens = TokenizeCode(source);
        for (size_t index = 0; index + 1 < tokens.size(); ++index)
        {
            if (tokens[index + 1] == Container::String("*") && tokens[index] != Container::String("IPhysicsModule"))
            {
                return true;
            }
        }
        return false;
    }

    bool HasTickOwnerGuardOrNoMemberAccess(const Container::String& source, const char* signature)
    {
        Container::String block;
        if (!ExtractFunctionBlock(StripCommentsAndLiterals(source), signature, block))
        {
            return false;
        }
        return block.find("m_") == Container::String::npos || HasOwnerGuardBeforeMemberMutation(source, signature);
    }

    uint32_t CountTextOccurrences(const Container::String& source, const char* text)
    {
        const Container::String needle(text);
        uint32_t count = 0;
        size_t offset = source.find(needle);
        while (offset != Container::String::npos)
        {
            ++count;
            offset = source.find(needle, offset + needle.size());
        }
        return count;
    }

    bool HasM8ScopeOwnedSpawnContract(const Container::String& source)
    {
        Container::String block;
        if (!ExtractFunctionBlock(
                StripCommentsAndLiterals(source),
                "bool M8MinimalPhysicsSmoke::Enter(",
                block))
        {
            return false;
        }
        return CountTextOccurrences(block, "ctx.ScopeRef.SpawnObject<Entity>()") == 4
            && block.find("world.SpawnObject<Entity>()") == Container::String::npos;
    }

    bool HasM8EnterBeforeRenderingRoutineSideEffects(const Container::String& source)
    {
        Container::String block;
        if (!ExtractFunctionBlock(
                StripCommentsAndLiterals(source),
                "GameModeEnterResult Rendering3DTestRoutine::Enter(",
                block))
        {
            return false;
        }

        const size_t smokeEnter = block.find("m_M8MinimalPhysicsSmoke.Enter(ctx)");
        const size_t particleCreate = block.find("CreateEmitter(");
        const size_t controllerRegister = block.find("RegisterController(");
        const size_t asyncLoad = block.find("LoadTextureAsync(");
        return smokeEnter != Container::String::npos
            && particleCreate != Container::String::npos && smokeEnter < particleCreate
            && controllerRegister != Container::String::npos && smokeEnter < controllerRegister
            && asyncLoad != Container::String::npos && smokeEnter < asyncLoad;
    }

    bool HasM8DistinctPhysicsSnapshotContract(const Container::String& source)
    {
        Container::String block;
        if (!ExtractFunctionBlock(
                StripCommentsAndLiterals(source),
                "void M8MinimalPhysicsSmoke::Update(",
                block))
        {
            return false;
        }

        const size_t sequenceQuery = block.find("GetPublishedSnapshotSequence(");
        const size_t sameSequenceGuard = block.find("publishedSnapshotSequence == m_LastPublishedSnapshotSequence");
        const size_t baselineGuard = block.find("!m_bHasSnapshotBaseline");
        const size_t stableIncrement = block.find("++m_StableObservationCount");
        return sequenceQuery != Container::String::npos
            && sameSequenceGuard != Container::String::npos && sequenceQuery < sameSequenceGuard
            && baselineGuard != Container::String::npos && sameSequenceGuard < baselineGuard
            && stableIncrement != Container::String::npos && baselineGuard < stableIncrement;
    }

    bool HasM8TransformDeltaAndLowVelocityContract(const Container::String& source)
    {
        Container::String block;
        const Container::String code = StripCommentsAndLiterals(source);
        if (!ExtractFunctionBlock(code, "bool M8MinimalPhysicsSmoke::ObserveStack(", block))
        {
            return false;
        }

        return code.find("constexpr float kMaximumSettledSpeed = 0.5f") != Container::String::npos
            && block.find("boxPosition - m_PreviousBoxPosition") != Container::String::npos
            && block.find("spherePosition - m_PreviousSpherePosition") != Container::String::npos
            && block.find("capsulePosition - m_PreviousCapsulePosition") != Container::String::npos
            && CountTextOccurrences(block, "kMaximumSnapshotTranslation * kMaximumSnapshotTranslation") == 3
            && CountTextOccurrences(block, "kMaximumSettledSpeed * kMaximumSettledSpeed") == 3;
    }

    void TestScannerIgnoresCommentsAndLiterals()
    {
        const Container::String ignoredSource(
            "// #include \"Physics/PhysicsModule.h\" NorvesModule_Physics\n"
            "const char* text = \"PhysicsModule IPhysicsModule\";\n"
            "const char character = '\'';\n");
        const Container::String realSource("#include \"Physics/PhysicsModule.h\"\n");
        const Container::String ignoredCMake(
            "# target_link_libraries(Core PRIVATE NorvesModule_Physics)\n"
            "set(note \"NorvesModule_Physics\")\n");
        const Container::String realCMake("target_link_libraries(Core PRIVATE NorvesModule_Physics)\n");

        assert(!ViolatesCorePhysicsDependency(ignoredSource));
        assert(ViolatesCorePhysicsDependency(realSource));
        assert(!HasCMakeTargetLink(TokenizeCMake(ignoredCMake), "Core", "PRIVATE", "NorvesModule_Physics"));
        assert(HasCMakeTargetLink(TokenizeCMake(realCMake), "Core", "PRIVATE", "NorvesModule_Physics"));
    }

    void TestScannerRejectsDependencyEvasions()
    {
        const Container::String relativeInclude("#include \"../Physics/PhysicsModule.h\"\n");
        const Container::String macroInclude(
            "#define PHYSICS_HEADER \"Physics/PhysicsModule.h\"\n"
            "#include PHYSICS_HEADER\n");
        const Container::String relativeVulkanInclude("#include \"../../RHI/Vulkan/VulkanDevice.h\"\n");
        const Container::String macroVulkanInclude(
            "#define VULKAN_HEADER \"RHI\\\\Vulkan\\\\VulkanDevice.h\"\n"
            "#include VULKAN_HEADER\n");
        const Container::String quotedLink("target_link_libraries(Core PRIVATE OtherDependency \"NorvesModule_Physics\")\n");
        const Container::String ineffectiveGuard(
            "void PhysicsModule::BadGuard()\n"
            "{\n"
            "    IsOwnerThread();\n"
            "    m_State = 1;\n"
            "}\n");
        const Container::String localGuard(
            "void PhysicsModule::LocalGuard()\n"
            "{\n"
            "    const bool IsOwnerThread = true;\n"
            "    if (!IsOwnerThread)\n"
            "    {\n"
            "        return;\n"
            "    }\n"
            "    m_State = 1;\n"
            "}\n");
        const Container::String uncheckedValidation(
            "EPhysicsResult PhysicsModule::BadValidation(ColliderComponent& component)\n"
            "{\n"
            "    const EPhysicsResult result = ValidateCollider(component);\n"
            "    component.m_Radius = 1.0f;\n"
            "    return result;\n"
            "}\n");
        const Container::String uncheckedReadiness(
            "EPhysicsSceneQueryResult PhysicsModule::BadQuery()\n"
            "{\n"
            "    GetReadinessResult();\n"
            "    return m_PublishedResult;\n"
            "}\n");

        assert(HasPhysicsInclude(relativeInclude));
        assert(HasPhysicsInclude(macroInclude));
        assert(HasVulkanBackendInclude(relativeVulkanInclude));
        assert(HasVulkanBackendInclude(macroVulkanInclude));
        assert(HasCMakeTargetLink(TokenizeCMake(quotedLink), "Core", "PRIVATE", "NorvesModule_Physics"));
        assert(!HasOwnerGuardBeforeMemberMutation(ineffectiveGuard, "void PhysicsModule::BadGuard("));
        assert(!HasOwnerGuardBeforeMemberMutation(localGuard, "void PhysicsModule::LocalGuard("));
        assert(!HasValidationBeforeComponentMutation(
            uncheckedValidation, "EPhysicsResult PhysicsModule::BadValidation(", "ValidateCollider"));
        assert(!HasReadinessGuardBeforeMemberRead(
            uncheckedReadiness, "EPhysicsSceneQueryResult PhysicsModule::BadQuery("));
    }

    void TestArchitectureMutationPredicates()
    {
        const Container::String concreteSceneQuery("class SceneQuery {};\n");
        const Container::String bvhReuse("struct BVHNode {};\n");
        const Container::String forbiddenFeature("void SweepCapsule();\n");
        const Container::String futureSweepFeature("void SweepConvex();\n");
        const Container::String lateWorldScan("GetWorld().GetObjects();\n");
        const Container::String futurePublicHeader("class FuturePhysicsApi { FuturePhysicsApi* Next; };\n");
        const Container::String sapEndpoint("struct SweepEndpoint { float Coordinate; };\n");
        const Container::String physicsTypesInclude("#include \"Scene/SceneQuery.h\"\n");
        const Container::String sceneQueryImplementationInclude("#include \"Scene/SceneQueryBVH.h\"\n");
        const Container::String futurePrivateFile("void SweepConvex();\n");
        const Container::String emptyTick("void PhysicsModule::Tick(float)\n{\n}\n");
        const Container::String unguardedTick("void PhysicsModule::Tick(float)\n{\n    m_State = 1;\n}\n");
        const Container::String guardedTick(
            "void PhysicsModule::Tick(float)\n{\n"
            "    if (!IsOwnerThread())\n    {\n        return;\n    }\n"
            "    m_State = 1;\n}\n");
        const Container::String scopedSpawn(
            "bool M8MinimalPhysicsSmoke::Enter(GameModeContext& ctx)\n{\n"
            "ctx.ScopeRef.SpawnObject<Entity>();\nctx.ScopeRef.SpawnObject<Entity>();\n"
            "ctx.ScopeRef.SpawnObject<Entity>();\nctx.ScopeRef.SpawnObject<Entity>();\n}\n");
        const Container::String deferredTrack(
            "bool M8MinimalPhysicsSmoke::Enter(GameModeContext& ctx)\n{\n"
            "world.SpawnObject<Entity>();\nworld.SpawnObject<Entity>();\n"
            "world.SpawnObject<Entity>();\nworld.SpawnObject<Entity>();\nctx.ScopeRef.TrackObject(nullptr);\n}\n");
        const Container::String smokeBeforeSideEffects(
            "GameModeEnterResult Rendering3DTestRoutine::Enter(GameModeContext& ctx, Data& data)\n{\n"
            "data.m_M8MinimalPhysicsSmoke.Enter(ctx);\nCreateEmitter();\nRegisterController();\nLoadTextureAsync();\n}\n");
        const Container::String smokeAfterSideEffects(
            "GameModeEnterResult Rendering3DTestRoutine::Enter(GameModeContext& ctx, Data& data)\n{\n"
            "CreateEmitter();\nRegisterController();\nLoadTextureAsync();\ndata.m_M8MinimalPhysicsSmoke.Enter(ctx);\n}\n");
        const Container::String distinctSnapshotObservation(
            "void M8MinimalPhysicsSmoke::Update(GameModeContext& ctx)\n{\n"
            "GetPublishedSnapshotSequence(publishedSnapshotSequence);\n"
            "if (publishedSnapshotSequence == m_LastPublishedSnapshotSequence) { return; }\n"
            "if (!m_bHasSnapshotBaseline) { return; }\n++m_StableObservationCount;\n}\n");
        const Container::String renderedFrameOnlyObservation(
            "void M8MinimalPhysicsSmoke::Update(GameModeContext& ctx)\n{\n"
            "GetRenderedFrameCount();\n++m_StableObservationCount;\n}\n");
        const Container::String transformDeltaAndLowVelocity(
            "constexpr float kMaximumSettledSpeed = 0.5f;\n"
            "bool M8MinimalPhysicsSmoke::ObserveStack() const\n{\n"
            "return (boxPosition - m_PreviousBoxPosition).LengthSquared() <= kMaximumSnapshotTranslation * kMaximumSnapshotTranslation\n"
            "&& (spherePosition - m_PreviousSpherePosition).LengthSquared() <= kMaximumSnapshotTranslation * kMaximumSnapshotTranslation\n"
            "&& (capsulePosition - m_PreviousCapsulePosition).LengthSquared() <= kMaximumSnapshotTranslation * kMaximumSnapshotTranslation\n"
            "&& boxVelocity.LengthSquared() <= kMaximumSettledSpeed * kMaximumSettledSpeed\n"
            "&& sphereVelocity.LengthSquared() <= kMaximumSettledSpeed * kMaximumSettledSpeed\n"
            "&& capsuleVelocity.LengthSquared() <= kMaximumSettledSpeed * kMaximumSettledSpeed;\n}\n");
        const Container::String velocityOnlyObservation(
            "constexpr float kMaximumSettledSpeed = 1.0f;\n"
            "bool M8MinimalPhysicsSmoke::ObserveStack() const\n{\n"
            "return boxVelocity.LengthSquared() <= kMaximumSettledSpeed * kMaximumSettledSpeed;\n}\n");

        assert(ViolatesBroadphaseIsolation(concreteSceneQuery));
        assert(ViolatesBroadphaseIsolation(bvhReuse));
        assert(ViolatesBroadphaseIsolation(forbiddenFeature));
        assert(ViolatesBroadphaseIsolation(futureSweepFeature));
        assert(!ViolatesBroadphaseIsolation(sapEndpoint));
        assert(!ViolatesBroadphaseIsolation(physicsTypesInclude));
        assert(ViolatesBroadphaseIsolation(sceneQueryImplementationInclude));
        assert(ViolatesPhysicsPrivateScope(futurePrivateFile));
        assert(HasTickOwnerGuardOrNoMemberAccess(emptyTick, "void PhysicsModule::Tick("));
        assert(!HasTickOwnerGuardOrNoMemberAccess(unguardedTick, "void PhysicsModule::Tick("));
        assert(HasTickOwnerGuardOrNoMemberAccess(guardedTick, "void PhysicsModule::Tick("));
        assert(HasIdentifier(StripCommentsAndLiterals(lateWorldScan), "GetWorld"));
        assert(HasIdentifier(StripCommentsAndLiterals(lateWorldScan), "GetObjects"));
        assert(HasForbiddenPublicRawPointer(futurePublicHeader));
        assert(HasM8ScopeOwnedSpawnContract(scopedSpawn));
        assert(!HasM8ScopeOwnedSpawnContract(deferredTrack));
        assert(HasM8EnterBeforeRenderingRoutineSideEffects(smokeBeforeSideEffects));
        assert(!HasM8EnterBeforeRenderingRoutineSideEffects(smokeAfterSideEffects));
        assert(HasM8DistinctPhysicsSnapshotContract(distinctSnapshotObservation));
        assert(!HasM8DistinctPhysicsSnapshotContract(renderedFrameOnlyObservation));
        assert(HasM8TransformDeltaAndLowVelocityContract(transformDeltaAndLowVelocity));
        assert(!HasM8TransformDeltaAndLowVelocityContract(velocityOnlyObservation));
    }

    void TestM8GameFailureContracts()
    {
        Container::String smokeSource;
        Container::String routineSource;
        assert(ReadFileBytes(
            MakeSourcePath("Game/GameModes/Rendering3DTest/M8MinimalPhysicsSmoke.cpp"),
            smokeSource));
        assert(ReadFileBytes(
            MakeSourcePath("Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp"),
            routineSource));
        assert(HasM8ScopeOwnedSpawnContract(smokeSource));
        assert(HasM8EnterBeforeRenderingRoutineSideEffects(routineSource));
        assert(HasM8DistinctPhysicsSnapshotContract(smokeSource));
        assert(HasM8TransformDeltaAndLowVelocityContract(smokeSource));
    }

    void TestScopeOwnedSpawnsCleanupAfterPartialSetup()
    {
        World world;
        world.Initialize();

        GameMode::GameModeScope scope(&world, nullptr);
        assert(scope.SpawnObject<Entity>() != nullptr);
        assert(scope.SpawnObject<Entity>() != nullptr);
        assert(world.GetObjectCount() == 2);

        scope.Cleanup();
        assert(scope.IsEmpty());
        assert(world.GetObjectCount() == 0);
        world.Finalize();
    }

    void TestCoreAndCMakeDependencyDirection()
    {
        Container::String coreCMake;
        Container::String physicsCMake;
        assert(ReadFileBytes(MakeSourcePath("Library/Core/CMakeLists.txt"), coreCMake));
        assert(ReadFileBytes(MakeSourcePath("Library/Modules/Physics/CMakeLists.txt"), physicsCMake));
        const Container::VariableArray<Container::String> coreCMakeTokens = TokenizeCMake(coreCMake);
        const Container::VariableArray<Container::String> physicsCMakeTokens = TokenizeCMake(physicsCMake);
        assert(!HasCMakeTargetLink(coreCMakeTokens, "Core", "PRIVATE", "NorvesModule_Physics"));
        assert(HasCMakeTargetLink(physicsCMakeTokens, "${MODULE_NAME}", "PRIVATE", "Core"));

        Container::VariableArray<Container::String> coreFiles;
        assert(CollectSourceFiles(MakeSourcePath("Library/Core/Public"), coreFiles));
        assert(CollectSourceFiles(MakeSourcePath("Library/Core/Private"), coreFiles));
        for (const Container::String& path : coreFiles)
        {
            Container::String source;
            assert(ReadFileBytes(path, source));
            assert(!ViolatesCorePhysicsDependency(source));
        }
    }

    void TestPublicAndPrivatePhysicsBoundaries()
    {
        Container::String moduleHeader;
        Container::String broadphaseHeader;
        assert(ReadFileBytes(MakeSourcePath("Library/Modules/Physics/Private/Physics/PhysicsModule.h"), moduleHeader));
        assert(ReadFileBytes(MakeSourcePath("Library/Modules/Physics/Private/Physics/PhysicsBroadphase.h"), broadphaseHeader));

        const Container::String moduleCode = StripCommentsAndLiterals(moduleHeader);
        assert(moduleCode.find("class PhysicsModule final : public IPhysicsModule, private Core::Scene::IPhysicsSceneQueryProvider")
            != Container::String::npos);
        assert(!ViolatesBroadphaseIsolation(broadphaseHeader));

        Container::VariableArray<Container::String> publicHeaders;
        assert(CollectSourceFiles(MakeSourcePath("Library/Modules/Physics/Public/Physics"), publicHeaders));
        uint32_t moduleBorrowedPointerCount = 0;
        for (const Container::String& path : publicHeaders)
        {
            Container::String header;
            assert(ReadFileBytes(path, header));
            const Container::String code = StripCommentsAndLiterals(header);
            const Container::VariableArray<Container::String> tokens = TokenizeCode(header);
            assert(!HasForbiddenPublicRawPointer(header));
            assert(CountRawPointerType(tokens, "Component") == 0);
            assert(CountRawPointerType(tokens, "Entity") == 0);
            assert(CountRawPointerType(tokens, "PhysicsModule") == 0);
            assert(CountRawPointerType(tokens, "IPhysicsSceneQueryProvider") == 0);
            moduleBorrowedPointerCount += CountRawPointerType(tokens, "IPhysicsModule");
            assert(!HasIdentifier(code, "MulticastDelegate"));
            assert(!HasIdentifier(code, "IPhysicsSceneQueryProvider"));
        }
        assert(moduleBorrowedPointerCount == 2);
    }

    void TestPhysicsScopeAndThreadGuards()
    {
        Container::String moduleSource;
        Container::String broadphaseSource;
        assert(ReadFileBytes(MakeSourcePath("Library/Modules/Physics/Private/Physics/PhysicsModule.cpp"), moduleSource));
        assert(ReadFileBytes(MakeSourcePath("Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp"), broadphaseSource));
        const Container::String moduleCode = StripCommentsAndLiterals(moduleSource);
        const Container::String broadphaseCode = StripCommentsAndLiterals(broadphaseSource);
        const Container::VariableArray<Container::String> moduleTokens = TokenizeCode(moduleSource);
        const Container::VariableArray<Container::String> broadphaseTokens = TokenizeCode(broadphaseSource);

        Container::VariableArray<Container::String> privatePhysicsFiles;
        assert(CollectSourceFiles(MakeSourcePath("Library/Modules/Physics/Private/Physics"), privatePhysicsFiles));
        for (const Container::String& path : privatePhysicsFiles)
        {
            Container::String source;
            assert(ReadFileBytes(path, source));
            assert(!ViolatesPhysicsPrivateScope(source));
        }

        const char* forbiddenIdentifiers[] =
        {
            "CCD", "Continuous", "Substep", "OverlapStay", "HitStay", "EndHit", "Friction", "Restitution", "Joint", "Manifold",
            "MeshCollision", "TriangleMesh", "ConvexMesh",
            "SweepSphere", "SweepBox", "SweepCapsule", "ContinuousCollision", "RunSubsteps", "OnOverlapStay",
        };
        for (const char* identifier : forbiddenIdentifiers)
        {
            assert(!HasIdentifier(moduleCode, identifier));
            assert(!HasIdentifier(broadphaseCode, identifier));
        }
        assert(!HasIdentifier(moduleCode, "GetWorld"));
        assert(!HasIdentifier(moduleCode, "GetObjects"));
        assert(!HasIdentifier(moduleCode, "GetEntities"));
        assert(!HasIdentifier(moduleCode, "GetAllEntities"));
        assert(!HasIdentifier(moduleCode, "GetRootEntities"));
        assert(!HasIdentifier(moduleCode, "GetEntityCount"));
        assert(!HasIdentifier(moduleCode, "IMPLEMENT_CLASS"));
        assert(!ViolatesBroadphaseIsolation(broadphaseSource));
        assert(moduleTokens.size() > 0 && broadphaseTokens.size() > 0);

        assert(HasTickOwnerGuardOrNoMemberAccess(moduleSource, "void PhysicsModule::Tick("));
        assert(HasOwnerGuardBeforeMemberMutation(moduleSource, "bool PhysicsModule::Install("));
        assert(HasOwnerGuardBeforeMemberMutation(moduleSource, "bool PhysicsModule::Initialize("));
        assert(HasOwnerGuardBeforeMemberMutation(moduleSource, "void PhysicsModule::PreFixedTick("));
        assert(HasOwnerGuardBeforeMemberMutation(moduleSource, "void PhysicsModule::FixedTick("));
        assert(HasOwnerGuardBeforeMemberMutation(moduleSource, "void PhysicsModule::Shutdown("));
        assert(HasOwnerGuardBeforeMemberMutation(moduleSource, "void PhysicsModule::Uninstall("));
        assert(HasOwnerGuardBeforeMemberMutation(moduleSource, "EPhysicsResult PhysicsModule::RegisterCollider("));
        assert(HasOwnerGuardBeforeMemberMutation(moduleSource, "EPhysicsResult PhysicsModule::RegisterRigidBody("));
        assert(HasOwnerGuardBeforeMemberMutation(moduleSource, "EPhysicsResult PhysicsModule::ValidateCollider("));
        assert(HasOwnerGuardBeforeMemberMutation(moduleSource, "EPhysicsResult PhysicsModule::ValidateRigidBody("));
        assert(HasOwnerGuardBeforeMemberMutation(moduleSource, "Core::Scene::EPhysicsSceneQueryResult PhysicsModule::GetReadinessResult("));
        assert(HasReadinessGuardBeforeMemberRead(moduleSource, "Core::Scene::EPhysicsSceneQueryResult PhysicsModule::Raycast("));
        assert(HasReadinessGuardBeforeMemberRead(moduleSource, "Core::Scene::EPhysicsSceneQueryResult PhysicsModule::OverlapSphere("));
        assert(HasReadinessGuardBeforeMemberRead(moduleSource, "Core::Scene::EPhysicsSceneQueryResult PhysicsModule::OverlapBox("));
        assert(HasReadinessGuardBeforeMemberRead(moduleSource, "Core::Scene::EPhysicsSceneQueryResult PhysicsModule::OverlapCapsule("));
        assert(HasReadinessGuardBeforeMemberRead(
            moduleSource, "Core::Scene::ColliderHandle collider,"));
        assert(HasReadinessGuardBeforeMemberRead(
            moduleSource, "Core::Scene::BodyHandle body,"));
        assert(HasValidationBeforeComponentMutation(moduleSource, "EPhysicsResult PhysicsModule::UnregisterCollider(", "ValidateCollider"));
        assert(HasValidationBeforeComponentMutation(moduleSource, "EPhysicsResult PhysicsModule::UnregisterRigidBody(", "ValidateRigidBody"));
        assert(HasValidationBeforeComponentMutation(moduleSource, "EPhysicsResult PhysicsModule::SetColliderSphere(", "ValidateCollider"));
        assert(HasValidationBeforeComponentMutation(moduleSource, "EPhysicsResult PhysicsModule::SetColliderBox(", "ValidateCollider"));
        assert(HasValidationBeforeComponentMutation(moduleSource, "EPhysicsResult PhysicsModule::SetColliderCapsule(", "ValidateCollider"));
        assert(HasValidationBeforeComponentMutation(moduleSource, "EPhysicsResult PhysicsModule::SetColliderTrigger(", "ValidateCollider"));
        assert(HasValidationBeforeComponentMutation(moduleSource, "EPhysicsResult PhysicsModule::SetBodyType(", "ValidateRigidBody"));
        assert(HasValidationBeforeComponentMutation(moduleSource, "EPhysicsResult PhysicsModule::SetBodyMass(", "ValidateRigidBody"));
        assert(HasValidationBeforeComponentMutation(moduleSource, "EPhysicsResult PhysicsModule::SetBodyGravityScale(", "ValidateRigidBody"));
        assert(HasValidationBeforeComponentMutation(moduleSource, "EPhysicsResult PhysicsModule::SetBodyLinearVelocity(", "ValidateRigidBody"));
        assert(HasValidationBeforeComponentMutation(moduleSource, "EPhysicsResult PhysicsModule::AddBodyImpulse(", "ValidateRigidBody"));
    }

    void TestRenderingBoundaryHasNoLivePhysicsOrVulkanConcreteTypes()
    {
        const char* forbiddenIdentifiers[] =
        {
            "ColliderComponent", "RigidBodyComponent", "PhysicsModule", "IPhysicsModule",
            "IPhysicsSceneQueryProvider", "Entity", "PhysicsRaycastHit", "PhysicsOverlapHit",
            "PhysicsContactEvent", "VulkanDevice", "VulkanSwapChain", "VulkanCommandList",
            "VkDevice", "VkSwapchainKHR", "VkCommandBuffer",
        };
        Container::VariableArray<Container::String> renderingFiles;
        assert(CollectSourceFiles(MakeSourcePath("Library/Core/Public/Rendering"), renderingFiles));
        assert(CollectSourceFiles(MakeSourcePath("Library/Core/Private/Rendering"), renderingFiles));
        for (const Container::String& path : renderingFiles)
        {
            Container::String source;
            assert(ReadFileBytes(path, source));
            const Container::String code = StripCommentsAndLiterals(source);
            assert(!HasPhysicsInclude(source));
            assert(!HasVulkanBackendInclude(source));
            for (const char* identifier : forbiddenIdentifiers)
            {
                assert(!HasIdentifier(code, identifier));
            }
        }
    }
} // namespace

int main()
{
    TestScannerIgnoresCommentsAndLiterals();
    TestScannerRejectsDependencyEvasions();
    TestArchitectureMutationPredicates();
    TestM8GameFailureContracts();
    TestScopeOwnedSpawnsCleanupAfterPartialSetup();
    TestCoreAndCMakeDependencyDirection();
    TestPublicAndPrivatePhysicsBoundaries();
    TestPhysicsScopeAndThreadGuards();
    TestRenderingBoundaryHasNoLivePhysicsOrVulkanConcreteTypes();
    std::cout << "PhysicsArchitectureContractTest passed\n";
    return EXIT_SUCCESS;
}
