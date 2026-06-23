#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef NORVES_SOURCE_DIR
#define NORVES_SOURCE_DIR "."
#endif

namespace
{
    std::string ReadForwardPassSource()
    {
        std::ifstream file(std::string(NORVES_SOURCE_DIR) + "/Library/Core/Private/Rendering/ForwardPass.cpp",
                           std::ios::binary);
        assert(file.good());

        std::ostringstream stream;
        stream << file.rdbuf();
        return stream.str();
    }

    void AssertContains(const std::string &source, const char *needle)
    {
        assert(source.find(needle) != std::string::npos);
    }

    void TestWorldBoardForwardPathContract()
    {
        const std::string source = ReadForwardPassSource();

        AssertContains(source, "struct WorldBoardForwardUBO");
        AssertContains(source, "m_WorldBoardPipeline");
        AssertContains(source, "m_ImpostorPipeline");
        AssertContains(source, "m_WorldBoardVertexShader");
        AssertContains(source, "m_ImpostorVertexShader");
        AssertContains(source, "m_WorldBoardUniformAllocator");
        AssertContains(source, "LoadShader(\"impostor.vert\"");
        AssertContains(source, "LoadShader(\"impostor.frag\"");
        AssertContains(source, "boardUboBinding.binding = 0");
        AssertContains(source, "boardTextureBinding.binding = 1");
        AssertContains(source, "boardInstanceBinding.binding = 7");
        AssertContains(source, "cmd.Draw.PayloadKind == DrawPayloadKind::Board");
        AssertContains(source, "cmd.Draw.BoardSubtype == BoardRenderSubtype::Impostor");
        AssertContains(source, "BindTexture(1, boardTexture)");
        AssertContains(source, "BindStorageBuffer(7");
        AssertContains(source, "boardTexture = m_DefaultWhiteTexture");

        const size_t boardBranch = source.find("cmd.Draw.PayloadKind == DrawPayloadKind::Board");
        const size_t meshResourceGate = source.find("if (!materials || !textures || !meshes)", boardBranch);
        assert(boardBranch != std::string::npos);
        assert(meshResourceGate != std::string::npos);
        assert(boardBranch < meshResourceGate);
    }
}

int main()
{
    std::cout << "ForwardPassWorldBoardRoutingTest start\n";
    TestWorldBoardForwardPathContract();
    std::cout << "ForwardPassWorldBoardRoutingTest passed\n";
    return 0;
}
