#include "Asset/AssetPath.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Asset;

int main()
{
    std::cout << "AssetPathTest start\n";

    {
        const AssetPath path = AssetPath::Normalize("Assets/Textures/a.png", "C:/Project/Assets");
        assert(path.IsValid());
        assert(!path.IsAbsolute());
        assert(path.HasLogicalPath());
        assert(path.GetLogicalPath() == "Textures/a.png");
        assert(path.GetResolvedPath() == "C:/Project/Assets/Textures/a.png");
    }

    {
        const AssetPath path = AssetPath::Normalize("Assets\\Textures\\a.png");
        assert(path.IsValid());
        assert(path.GetLogicalPath() == "Textures/a.png");
    }

    {
        const AssetPath path = AssetPath::Normalize("Textures/../Shaders/a.bin");
        assert(path.IsValid());
        assert(path.GetLogicalPath() == "Shaders/a.bin");
    }

    {
        const AssetPath path = AssetPath::Normalize("../escape.bin");
        assert(!path.IsValid());
    }

    {
        const AssetPath path = AssetPath::Normalize("C:\\tmp\\a.bin");
        assert(path.IsValid());
        assert(path.IsAbsolute());
        assert(!path.HasLogicalPath());
        assert(path.GetResolvedPath() == "C:/tmp/a.bin");
    }

    {
        const AssetPath path = AssetPath::Normalize("C:tmp\\a.bin");
        assert(!path.IsValid());
    }

    {
        const AssetPath path = AssetPath::Normalize("\\\\server\\share\\a.bin");
        assert(!path.IsValid());
    }

    {
        const AssetPath path = AssetPath::Normalize("");
        assert(!path.IsValid());
    }

    {
        const AssetPath path = AssetPath::Normalize("Assets/../escape.bin");
        assert(!path.IsValid());
    }

    std::cout << "AssetPathTest passed\n";
    return 0;
}
