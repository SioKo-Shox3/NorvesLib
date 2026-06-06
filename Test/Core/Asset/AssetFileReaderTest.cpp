#include "Asset/AssetFileReader.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace NorvesLib::Core::Asset;

namespace
{
    std::filesystem::path CreateTestRoot()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path root = std::filesystem::temp_directory_path() / ("NorvesLibAssetFileReaderTest_" + std::to_string(now));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        return root;
    }

    std::string ToAssetString(const std::filesystem::path &path)
    {
        std::string value = path.generic_string();
        return value;
    }

    bool EqualsAnsiString(const NorvesLib::Core::Container::AnsiString &left, const std::string &right)
    {
        return left == right.c_str();
    }

    void WriteBinaryFile(const std::filesystem::path &path, const std::initializer_list<uint8_t> bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        assert(output.is_open());
        for (const uint8_t value : bytes)
        {
            output.write(reinterpret_cast<const char *>(&value), sizeof(value));
        }
    }

    void WriteEmptyFile(const std::filesystem::path &path)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        assert(output.is_open());
    }
}

int main()
{
    std::cout << "AssetFileReaderTest start\n";

    const std::filesystem::path root = CreateTestRoot();
    const std::string rootPath = ToAssetString(root);

    WriteBinaryFile(root / "Textures" / "a.bin", {1, 2, 3});
    WriteBinaryFile(root / "Shaders" / "a.bin", {4, 5});
    WriteBinaryFile(root / "Absolute" / "file.bin", {9, 8, 7, 6});
    WriteEmptyFile(root / "empty.bin");

    AssetFileReader reader(rootPath.c_str());

    {
        AssetReadRequest request;
        request.InputPath = "Textures/a.bin";
        request.AssetRoot = rootPath.c_str();
        const AssetReadResult result = reader.Read(request);
        assert(result.Succeeded());
        assert(result.Status == AssetReadStatus::Success);
        assert(result.BytesRead == 3);
        assert(result.FileSize == 3);
        assert(result.Blob.IsValid());
        assert(!result.Blob.IsEmpty());
        assert(result.Blob.GetData()[0] == 1);
        assert(result.Path.GetLogicalPath() == "Textures/a.bin");
        assert(EqualsAnsiString(result.Path.GetResolvedPath(), rootPath + "/Textures/a.bin"));
        assert(result.Blob.GetSourcePath() == result.Path.GetResolvedPath());
    }

    {
        const AssetReadResult result = reader.Read("Assets/Textures/a.bin");
        assert(result.Succeeded());
        assert(result.Path.GetLogicalPath() == "Textures/a.bin");
        assert(EqualsAnsiString(result.Path.GetResolvedPath(), rootPath + "/Textures/a.bin"));
    }

    {
        const AssetReadResult result = reader.Read("Textures/../Shaders/a.bin");
        assert(result.Succeeded());
        assert(result.Path.GetLogicalPath() == "Shaders/a.bin");
        assert(EqualsAnsiString(result.Path.GetResolvedPath(), rootPath + "/Shaders/a.bin"));
        assert(result.Blob.GetSize() == 2);
        assert(result.Blob.GetData()[0] == 4);
    }

    {
        AssetReadRequest request;
        request.InputPath = ToAssetString(root / "Absolute" / "file.bin").c_str();
        request.AssetRoot = {};
        const AssetReadResult result = reader.Read(request);
        assert(result.Succeeded());
        assert(result.Path.IsAbsolute());
        assert(!result.Path.HasLogicalPath());
        assert(EqualsAnsiString(result.Path.GetResolvedPath(), ToAssetString(root / "Absolute" / "file.bin")));
        assert(result.Blob.GetSourcePath() == result.Path.GetResolvedPath());
        assert(result.BytesRead == 4);
    }

    {
        const AssetReadResult result = reader.Read("Textures/missing.bin");
        assert(!result.Succeeded());
        assert(result.Status == AssetReadStatus::FileNotFound || result.Status == AssetReadStatus::OpenFailed);
        assert(!result.Blob.IsValid());
        assert(result.BytesRead == 0);
    }

    {
        const AssetReadResult result = reader.Read("");
        assert(!result.Succeeded());
        assert(result.Status == AssetReadStatus::InvalidRequest);
        assert(!result.Blob.IsValid());
        assert(result.BytesRead == 0);
    }

    {
        const AssetReadResult result = reader.Read("../escape.bin");
        assert(!result.Succeeded());
        assert(result.Status == AssetReadStatus::InvalidPath);
        assert(!result.Blob.IsValid());
        assert(result.BytesRead == 0);
    }

    {
        AssetFileReader emptyRootReader("");
        const AssetReadResult result = emptyRootReader.Read("Textures/a.bin");
        assert(!result.Succeeded());
        assert(result.Status == AssetReadStatus::InvalidAssetRoot);
        assert(!result.Blob.IsValid());
    }

    {
        AssetFileReader driveRelativeRootReader("C:relative");
        const AssetReadResult result = driveRelativeRootReader.Read("Textures/a.bin");
        assert(!result.Succeeded());
        assert(result.Status == AssetReadStatus::InvalidAssetRoot);
    }

    {
        AssetFileReader uncRootReader("//server/share");
        const AssetReadResult result = uncRootReader.Read("Textures/a.bin");
        assert(!result.Succeeded());
        assert(result.Status == AssetReadStatus::InvalidAssetRoot);
    }

    {
        AssetFileReader relativeRootReader("Assets");
        const AssetReadResult result = relativeRootReader.Read("Textures/a.bin");
        assert(!result.Succeeded());
        assert(result.Status == AssetReadStatus::InvalidAssetRoot);
    }

    {
        AssetFileReader escapingRootReader("C:/Project/../Assets");
        const AssetReadResult result = escapingRootReader.Read("Textures/a.bin");
        assert(!result.Succeeded());
        assert(result.Status == AssetReadStatus::InvalidAssetRoot);
    }

    {
        const AssetReadResult result = reader.Read("empty.bin");
        assert(result.Succeeded());
        assert(result.Status == AssetReadStatus::Success);
        assert(result.Blob.IsValid());
        assert(result.Blob.IsEmpty());
        assert(result.BytesRead == 0);
        assert(result.FileSize == 0);
        assert(result.Blob.GetSourcePath() == result.Path.GetResolvedPath());
    }

    std::filesystem::remove_all(root);

    std::cout << "AssetFileReaderTest passed\n";
    return 0;
}
