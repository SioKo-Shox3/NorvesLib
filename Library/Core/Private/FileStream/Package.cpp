#include "FileStream/Package.h"
#include "FileStream/FileStream.h"
#include "Logging/LogMacros.h"
#include "Object/Reflection.h"
#include "Thread/JobSystem.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <type_traits>

#ifdef UNICODE
#include <Windows.h>
#endif

namespace NorvesLib::FileStream
{
    namespace
    {
        using LoadProfileClock = std::chrono::steady_clock;
        using ByteArray = Core::Asset::AssetBlob::ByteArray;
        using StoragePtr = Core::Container::TSharedPtr<const ByteArray>;

        struct ParsedPackage
        {
            PackageFormat Format = PackageFormat::None;
            Core::Container::VariableArray<PackageEntry> Entries;
        };

        LoadProfileClock::time_point LoadProfileNow()
        {
            return LoadProfileClock::now();
        }

        double LoadProfileElapsedMs(LoadProfileClock::time_point startTime)
        {
            return std::chrono::duration<double, std::milli>(LoadProfileClock::now() - startTime).count();
        }

        Core::Container::AnsiString ToAnsiString(const Core::Container::String &value)
        {
            if (value.empty())
            {
                return {};
            }

            if constexpr (std::is_same_v<Core::Container::String::value_type, char>)
            {
                return Core::Container::AnsiString(value.c_str());
            }
#ifdef UNICODE
            const int requiredSize = ::WideCharToMultiByte(CP_UTF8,
                                                           0,
                                                           value.c_str(),
                                                           static_cast<int>(value.size()),
                                                           nullptr,
                                                           0,
                                                           nullptr,
                                                           nullptr);
            if (requiredSize <= 0)
            {
                return {};
            }

            Core::Container::VariableArray<char> buffer(static_cast<size_t>(requiredSize) + 1, '\0');
            const int convertedSize = ::WideCharToMultiByte(CP_UTF8,
                                                            0,
                                                            value.c_str(),
                                                            static_cast<int>(value.size()),
                                                            buffer.data(),
                                                            requiredSize,
                                                            nullptr,
                                                            nullptr);
            if (convertedSize <= 0)
            {
                return {};
            }

            return Core::Container::AnsiString(buffer.data());
#else
            return {};
#endif
        }

        bool ReadPackageFile(const Core::Container::String &path,
                             Core::Container::VariableArray<uint8_t> &outRawData,
                             const char *role)
        {
            auto readStartTime = LoadProfileNow();
            size_t bytesRead = 0;
            auto fileStream = FileStream::Create(path, FileMode::Read, FileAccess::Read, FileShare::Read);
            if (!fileStream || !fileStream->IsOpen())
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=package_read role=%s path=\"%s\" bytes=%zu ms=%.3f success=0",
                                role,
                                path.c_str(),
                                bytesRead,
                                LoadProfileElapsedMs(readStartTime));
                return false;
            }

            int64_t fileSize = fileStream->GetSize();
            if (fileSize <= 0)
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=package_read role=%s path=\"%s\" bytes=%zu file_size=%lld ms=%.3f success=0",
                                role,
                                path.c_str(),
                                bytesRead,
                                static_cast<long long>(fileSize),
                                LoadProfileElapsedMs(readStartTime));
                return false;
            }

            if (static_cast<uint64_t>(fileSize) > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
            {
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=package_read role=%s path=\"%s\" bytes=%zu file_size=%lld ms=%.3f success=0",
                                role,
                                path.c_str(),
                                bytesRead,
                                static_cast<long long>(fileSize),
                                LoadProfileElapsedMs(readStartTime));
                return false;
            }

            outRawData.resize(static_cast<size_t>(fileSize));
            bytesRead = fileStream->Read(outRawData.data(), outRawData.size());
            if (bytesRead != static_cast<size_t>(fileSize))
            {
                outRawData.clear();
                NORVES_LOG_INFO("AssetLoadProfile",
                                "stage=package_read role=%s path=\"%s\" bytes=%zu file_size=%lld ms=%.3f success=0",
                                role,
                                path.c_str(),
                                bytesRead,
                                static_cast<long long>(fileSize),
                                LoadProfileElapsedMs(readStartTime));
                return false;
            }

            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=package_read role=%s path=\"%s\" bytes=%zu file_size=%lld ms=%.3f success=1",
                            role,
                            path.c_str(),
                            bytesRead,
                            static_cast<long long>(fileSize),
                            LoadProfileElapsedMs(readStartTime));
            return true;
        }

        StoragePtr MakeStorage(Core::Container::VariableArray<uint8_t> &&bytes)
        {
            Core::Container::TSharedPtr<ByteArray> storage = Core::Container::MakeShared<ByteArray>(std::move(bytes));
            return storage;
        }

        uint16_t ReadLe16(const uint8_t *data, size_t offset)
        {
            return static_cast<uint16_t>(data[offset]) |
                   static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8);
        }

        uint32_t ReadLe32(const uint8_t *data, size_t offset)
        {
            return static_cast<uint32_t>(data[offset]) |
                   (static_cast<uint32_t>(data[offset + 1]) << 8) |
                   (static_cast<uint32_t>(data[offset + 2]) << 16) |
                   (static_cast<uint32_t>(data[offset + 3]) << 24);
        }

        uint64_t ReadLe64(const uint8_t *data, size_t offset)
        {
            return static_cast<uint64_t>(ReadLe32(data, offset)) |
                   (static_cast<uint64_t>(ReadLe32(data, offset + 4)) << 32);
        }

        bool ConvertRange(uint64_t offset64, uint64_t size64, size_t packageSize, size_t &outOffset, size_t &outSize)
        {
            if (offset64 > static_cast<uint64_t>(packageSize))
            {
                return false;
            }

            const uint64_t remaining = static_cast<uint64_t>(packageSize) - offset64;
            if (size64 > remaining)
            {
                return false;
            }

            if (offset64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
                size64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
            {
                return false;
            }

            outOffset = static_cast<size_t>(offset64);
            outSize = static_cast<size_t>(size64);
            return true;
        }

        bool IsAligned(uint64_t value, uint32_t alignment)
        {
            return alignment > 0 && (value % alignment) == 0;
        }

        bool IsPowerOfTwo(uint32_t value)
        {
            return value != 0 && (value & (value - 1)) == 0;
        }

        bool HasExactMagic(const ByteArray &storage)
        {
            using namespace Core::Asset::AssetPackageFormatV1;
            return storage.size() >= MagicSize && std::memcmp(storage.data(), Magic, MagicSize) == 0;
        }

        bool HasCorruptMagicPrefix(const ByteArray &storage)
        {
            if (storage.size() < 4)
            {
                return false;
            }

            return storage[0] == 'N' &&
                   storage[1] == 'V' &&
                   storage[2] == 'P' &&
                   storage[3] == 'K';
        }

        uint64_t ComputeFnv1a64(const uint8_t *data, size_t size)
        {
            uint64_t hash = Core::Asset::AssetPackageFormatV1::Fnv1a64OffsetBasis;
            for (size_t index = 0; index < size; ++index)
            {
                hash ^= static_cast<uint64_t>(data[index]);
                hash *= Core::Asset::AssetPackageFormatV1::Fnv1a64Prime;
            }
            return hash;
        }

        PackageEntry MakeRawEntry(const StoragePtr &storage)
        {
            PackageEntry entry;
            entry.Name = Core::Asset::AssetPackageFormatV1::RawEntryName;
            entry.Type = Core::Asset::AssetPackageFormatV1::RawEntryType;
            entry.Compression = Core::Asset::AssetPackageCompression::None;
            entry.DataOffset = 0;
            entry.StoredSize = storage ? storage->size() : 0;
            entry.UncompressedSize = entry.StoredSize;
            entry.PayloadHash = storage ? ComputeFnv1a64(storage->data(), storage->size())
                                        : Core::Asset::AssetPackageFormatV1::ZeroSizePayloadHash;
            return entry;
        }

        bool HasDuplicateEntry(const Core::Container::VariableArray<PackageEntry> &entries,
                               const Core::Container::AnsiString &name,
                               Core::Asset::AssetPackageFourCC type)
        {
            for (const PackageEntry &entry : entries)
            {
                if (entry.Name == name && entry.Type == type)
                {
                    return true;
                }
            }
            return false;
        }

        bool ParseRawPackage(const StoragePtr &storage, ParsedPackage &outParsed)
        {
            outParsed.Format = PackageFormat::Raw;
            outParsed.Entries.clear();
            outParsed.Entries.push_back(MakeRawEntry(storage));
            return true;
        }

        bool ParseV1Package(const StoragePtr &storage, ParsedPackage &outParsed)
        {
            using namespace Core::Asset::AssetPackageFormatV1;

            if (!storage || storage->size() < HeaderSize)
            {
                return false;
            }

            const ByteArray &bytes = *storage;
            const uint8_t *data = bytes.data();
            const size_t packageSize = bytes.size();

            const uint32_t headerSize = ReadLe32(data, HeaderOffset::HeaderSize);
            const uint16_t versionMajor = ReadLe16(data, HeaderOffset::VersionMajor);
            const uint16_t versionMinor = ReadLe16(data, HeaderOffset::VersionMinor);
            const uint32_t endianMarker = ReadLe32(data, HeaderOffset::EndianMarker);
            const uint32_t entryRecordSize = ReadLe32(data, HeaderOffset::EntryRecordSize);
            const uint64_t declaredPackageSize = ReadLe64(data, HeaderOffset::PackageSize);
            const uint32_t entryCount = ReadLe32(data, HeaderOffset::EntryCount);
            const uint32_t flags = ReadLe32(data, HeaderOffset::Flags);
            const uint64_t entryTableOffset64 = ReadLe64(data, HeaderOffset::EntryTableOffset);
            const uint64_t entryTableSize64 = ReadLe64(data, HeaderOffset::EntryTableSize);
            const uint64_t nameTableOffset64 = ReadLe64(data, HeaderOffset::NameTableOffset);
            const uint64_t nameTableSize64 = ReadLe64(data, HeaderOffset::NameTableSize);
            const uint64_t blobDataOffset64 = ReadLe64(data, HeaderOffset::BlobDataOffset);
            const uint32_t alignment = ReadLe32(data, HeaderOffset::Alignment);
            const uint32_t reserved0 = ReadLe32(data, HeaderOffset::Reserved0);
            const uint64_t reserved1 = ReadLe64(data, HeaderOffset::Reserved1);

            if (headerSize != HeaderSize ||
                versionMajor != VersionMajor ||
                versionMinor != VersionMinor ||
                endianMarker != EndianMarker ||
                entryRecordSize != EntryRecordSize ||
                declaredPackageSize != static_cast<uint64_t>(packageSize) ||
                flags != 0 ||
                reserved0 != 0 ||
                reserved1 != 0 ||
                alignment < MinimumAlignment ||
                !IsPowerOfTwo(alignment))
            {
                return false;
            }

            const uint64_t expectedEntryTableSize64 = static_cast<uint64_t>(entryCount) * static_cast<uint64_t>(EntryRecordSize);
            if (entryCount != 0 && expectedEntryTableSize64 / entryCount != EntryRecordSize)
            {
                return false;
            }
            if (entryTableSize64 != expectedEntryTableSize64)
            {
                return false;
            }

            size_t entryTableOffset = 0;
            size_t entryTableSize = 0;
            size_t nameTableOffset = 0;
            size_t nameTableSize = 0;
            size_t blobDataOffset = 0;
            size_t blobDataSize = 0;
            if (!ConvertRange(entryTableOffset64, entryTableSize64, packageSize, entryTableOffset, entryTableSize) ||
                !ConvertRange(nameTableOffset64, nameTableSize64, packageSize, nameTableOffset, nameTableSize) ||
                !ConvertRange(blobDataOffset64, 0, packageSize, blobDataOffset, blobDataSize) ||
                !IsAligned(entryTableOffset64, alignment) ||
                !IsAligned(nameTableOffset64, alignment) ||
                !IsAligned(blobDataOffset64, alignment))
            {
                return false;
            }

            const size_t entryTableEnd = entryTableOffset + entryTableSize;
            const size_t nameTableEnd = nameTableOffset + nameTableSize;
            if (entryTableOffset < HeaderSize ||
                entryTableEnd > nameTableOffset ||
                nameTableEnd > blobDataOffset)
            {
                return false;
            }

            outParsed.Format = PackageFormat::V1;
            outParsed.Entries.clear();
            outParsed.Entries.reserve(entryCount);

            for (uint32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
            {
                const size_t recordOffset = entryTableOffset + static_cast<size_t>(entryIndex) * EntryRecordSize;
                const uint64_t nameOffset64 = ReadLe64(data, recordOffset + EntryOffset::NameOffset);
                const uint32_t nameSize32 = ReadLe32(data, recordOffset + EntryOffset::NameSize);
                const uint32_t type = ReadLe32(data, recordOffset + EntryOffset::Type);
                const uint32_t compression = ReadLe32(data, recordOffset + EntryOffset::Compression);
                const uint32_t entryFlags = ReadLe32(data, recordOffset + EntryOffset::Flags);
                const uint64_t dataOffset64 = ReadLe64(data, recordOffset + EntryOffset::DataOffset);
                const uint64_t storedSize64 = ReadLe64(data, recordOffset + EntryOffset::StoredSize);
                const uint64_t uncompressedSize64 = ReadLe64(data, recordOffset + EntryOffset::UncompressedSize);
                const uint64_t payloadHash = ReadLe64(data, recordOffset + EntryOffset::PayloadHash);
                const uint64_t entryReserved0 = ReadLe64(data, recordOffset + EntryOffset::Reserved0);

                if (nameSize32 == 0 ||
                    compression != static_cast<uint32_t>(Core::Asset::AssetPackageCompression::None) ||
                    entryFlags != 0 ||
                    entryReserved0 != 0 ||
                    storedSize64 != uncompressedSize64 ||
                    !IsAligned(dataOffset64, alignment))
                {
                    return false;
                }

                size_t nameOffset = 0;
                size_t nameSize = 0;
                size_t dataOffset = 0;
                size_t storedSize = 0;
                if (!ConvertRange(nameOffset64, nameSize32, packageSize, nameOffset, nameSize) ||
                    !ConvertRange(dataOffset64, storedSize64, packageSize, dataOffset, storedSize) ||
                    dataOffset < blobDataOffset)
                {
                    return false;
                }

                if (nameOffset < nameTableOffset)
                {
                    return false;
                }

                const size_t relativeNameOffset = nameOffset - nameTableOffset;
                if (relativeNameOffset > nameTableSize ||
                    nameSize > nameTableSize - relativeNameOffset)
                {
                    return false;
                }

                Core::Container::AnsiString name;
                name.reserve(nameSize);
                for (size_t nameIndex = 0; nameIndex < nameSize; ++nameIndex)
                {
                    const char character = static_cast<char>(data[nameOffset + nameIndex]);
                    if (character == '\0')
                    {
                        return false;
                    }
                    name.push_back(character);
                }

                if (HasDuplicateEntry(outParsed.Entries, name, type))
                {
                    return false;
                }

                const uint64_t computedHash = ComputeFnv1a64(storedSize > 0 ? data + dataOffset : nullptr, storedSize);
                if (computedHash != payloadHash)
                {
                    return false;
                }

                PackageEntry entry;
                entry.Name = name;
                entry.Type = type;
                entry.Compression = Core::Asset::AssetPackageCompression::None;
                entry.DataOffset = dataOffset;
                entry.StoredSize = storedSize;
                entry.UncompressedSize = static_cast<size_t>(uncompressedSize64);
                entry.PayloadHash = payloadHash;
                outParsed.Entries.push_back(entry);
            }

            return true;
        }

        bool ParsePackageStorage(const StoragePtr &storage, ParsedPackage &outParsed)
        {
            if (!storage)
            {
                return false;
            }

            if (HasExactMagic(*storage))
            {
                return ParseV1Package(storage, outParsed);
            }

            if (HasCorruptMagicPrefix(*storage))
            {
                return false;
            }

            return ParseRawPackage(storage, outParsed);
        }

        bool IsSameEntry(const PackageEntry &left, const PackageEntry &right)
        {
            return left.Name == right.Name &&
                   left.Type == right.Type &&
                   left.Compression == right.Compression &&
                   left.DataOffset == right.DataOffset &&
                   left.StoredSize == right.StoredSize &&
                   left.UncompressedSize == right.UncompressedSize &&
                   left.PayloadHash == right.PayloadHash;
        }

        bool TryCopyMatchingEntry(const Core::Container::VariableArray<PackageEntry> &entries,
                                  const PackageEntry &requestedEntry,
                                  PackageEntry &outEntry)
        {
            for (const PackageEntry &entry : entries)
            {
                if (IsSameEntry(entry, requestedEntry))
                {
                    outEntry = entry;
                    return true;
                }
            }
            return false;
        }
    }

    // ========================================
    // Reflection
    // ========================================

    IMPLEMENT_CLASS(Package, Core::Object)

    // ========================================
    // Construction
    // ========================================

    Package::Package()
        : Object(), m_LoadState(PackageLoadState::Unloaded), m_LastAccessFrame(0)
    {
    }

    Package::Package(const Core::FieldInitializer *initializer)
        : Object(initializer), m_LoadState(PackageLoadState::Unloaded), m_LastAccessFrame(0)
    {
    }

    Package::Package(const Core::IUnknown *sourceObject)
        : Object(sourceObject), m_LoadState(PackageLoadState::Unloaded), m_LastAccessFrame(0)
    {
        if (sourceObject)
        {
            const Package *sourcePackage = dynamic_cast<const Package *>(sourceObject);
            if (sourcePackage)
            {
                Thread::ScopedLock lock(sourcePackage->m_LoadMutex);
                m_FilePath = sourcePackage->m_FilePath;
                m_SourcePathAnsi = sourcePackage->m_SourcePathAnsi;
                m_PackageName = sourcePackage->m_PackageName;
                m_Storage = sourcePackage->m_Storage;
                m_Entries = sourcePackage->m_Entries;
                m_Format = sourcePackage->m_Format;
                m_LoadState = sourcePackage->m_LoadState;
                m_LastAccessFrame = sourcePackage->m_LastAccessFrame;
            }
        }
    }

    Package::~Package()
    {
        Unload();
    }

    // ========================================
    // Object lifecycle
    // ========================================

    void Package::Initialize()
    {
        Object::Initialize();
    }

    void Package::Finalize()
    {
        Unload();
        Object::Finalize();
    }

    // ========================================
    // Loading
    // ========================================

    bool Package::Load(const Core::Container::String &path)
    {
        Thread::TaskPtr pendingTask;
        {
            Thread::ScopedLock lock(m_LoadMutex);
            if (m_LoadTask && !m_LoadTask->IsCompleted() && m_LoadState == PackageLoadState::Loading)
            {
                pendingTask = m_LoadTask;
            }
        }
        if (pendingTask)
        {
            pendingTask->Wait();
        }

        {
            Thread::ScopedLock lock(m_LoadMutex);
            m_LoadTask.reset();
            if (m_LoadState == PackageLoadState::Loaded && m_FilePath == path)
            {
                return true;
            }

            m_LoadState = PackageLoadState::Loading;
            m_FilePath = path;
            m_SourcePathAnsi = ToAnsiString(path);
            m_PackageName = Core::Identity(path);
            m_Storage.reset();
            m_Entries.clear();
            m_Format = PackageFormat::None;
        }

        Core::Container::VariableArray<uint8_t> rawData;
        if (!ReadPackageFile(path, rawData, "caller"))
        {
            Thread::ScopedLock lock(m_LoadMutex);
            m_Storage.reset();
            m_Entries.clear();
            m_Format = PackageFormat::None;
            m_LoadState = PackageLoadState::Failed;
            return false;
        }

        StoragePtr storage = MakeStorage(std::move(rawData));
        ParsedPackage parsed;
        if (!ParsePackageStorage(storage, parsed))
        {
            Thread::ScopedLock lock(m_LoadMutex);
            m_Storage.reset();
            m_Entries.clear();
            m_Format = PackageFormat::None;
            m_LoadState = PackageLoadState::Failed;
            return false;
        }

        {
            Thread::ScopedLock lock(m_LoadMutex);
            m_Storage = std::move(storage);
            m_Entries = std::move(parsed.Entries);
            m_Format = parsed.Format;
            m_LoadState = PackageLoadState::Loaded;
        }
        return true;
    }

    bool Package::LoadAsync(const Core::Container::String &path)
    {
        {
            Thread::ScopedLock lock(m_LoadMutex);
            if (m_LoadTask && !m_LoadTask->IsCompleted() && m_FilePath == path)
            {
                return true;
            }
        }

        Thread::TaskPtr pendingTask;
        {
            Thread::ScopedLock lock(m_LoadMutex);
            if (m_LoadTask && !m_LoadTask->IsCompleted())
            {
                pendingTask = m_LoadTask;
            }
        }
        if (pendingTask)
        {
            pendingTask->Wait();
        }

        {
            Thread::ScopedLock lock(m_LoadMutex);
            m_FilePath = path;
            m_SourcePathAnsi = ToAnsiString(path);
            m_PackageName = Core::Identity(path);
            m_Storage.reset();
            m_Entries.clear();
            m_Format = PackageFormat::None;
            m_LoadState = PackageLoadState::Loading;
        }

        auto task = Thread::Task::Create([this, path]()
        {
            Core::Container::VariableArray<uint8_t> rawData;
            bool bSuccess = ReadPackageFile(path, rawData, "worker");
            StoragePtr storage;
            ParsedPackage parsed;
            if (bSuccess)
            {
                storage = MakeStorage(std::move(rawData));
                bSuccess = ParsePackageStorage(storage, parsed);
            }

            Thread::ScopedLock lock(m_LoadMutex);
            if (m_FilePath != path)
            {
                return;
            }

            if (!bSuccess)
            {
                m_Storage.reset();
                m_Entries.clear();
                m_Format = PackageFormat::None;
                m_LoadState = PackageLoadState::Failed;
                return;
            }

            m_Storage = std::move(storage);
            m_Entries = std::move(parsed.Entries);
            m_Format = parsed.Format;
            m_LoadState = PackageLoadState::Loaded;
        }, Thread::TaskPriority::NORMAL);

        {
            Thread::ScopedLock lock(m_LoadMutex);
            m_LoadTask = task;
        }

        Thread::JobSystem::Get().SubmitTask(task);
        return true;
    }

    bool Package::LoadFromMemory(Core::Container::Span<const uint8_t> bytes,
                                 const Core::Container::String &sourcePath)
    {
        Thread::TaskPtr pendingTask;
        {
            Thread::ScopedLock lock(m_LoadMutex);
            if (m_LoadTask && !m_LoadTask->IsCompleted())
            {
                pendingTask = m_LoadTask;
            }
        }
        if (pendingTask)
        {
            pendingTask->Wait();
        }

        Core::Container::VariableArray<uint8_t> copiedBytes;
        copiedBytes.resize(bytes.size());
        if (!bytes.empty())
        {
            if (bytes.data() == nullptr)
            {
                Thread::ScopedLock lock(m_LoadMutex);
                m_LoadState = PackageLoadState::Failed;
                return false;
            }
            std::copy(bytes.begin(), bytes.end(), copiedBytes.begin());
        }

        StoragePtr storage = MakeStorage(std::move(copiedBytes));
        ParsedPackage parsed;
        if (!ParsePackageStorage(storage, parsed))
        {
            Thread::ScopedLock lock(m_LoadMutex);
            m_LoadTask.reset();
            m_FilePath = sourcePath;
            m_SourcePathAnsi = ToAnsiString(sourcePath);
            m_PackageName = Core::Identity(sourcePath);
            m_Storage.reset();
            m_Entries.clear();
            m_Format = PackageFormat::None;
            m_LoadState = PackageLoadState::Failed;
            return false;
        }

        {
            Thread::ScopedLock lock(m_LoadMutex);
            m_LoadTask.reset();
            m_FilePath = sourcePath;
            m_SourcePathAnsi = ToAnsiString(sourcePath);
            m_PackageName = Core::Identity(sourcePath);
            m_Storage = std::move(storage);
            m_Entries = std::move(parsed.Entries);
            m_Format = parsed.Format;
            m_LoadState = PackageLoadState::Loaded;
        }
        return true;
    }

    void Package::Unload()
    {
        Thread::TaskPtr pendingTask;
        {
            Thread::ScopedLock lock(m_LoadMutex);
            if (m_LoadTask && !m_LoadTask->IsCompleted())
            {
                pendingTask = m_LoadTask;
            }
        }
        if (pendingTask)
        {
            pendingTask->Wait();
        }

        {
            Thread::ScopedLock lock(m_LoadMutex);
            m_LoadTask.reset();
            m_Storage.reset();
            m_Entries.clear();
            m_Format = PackageFormat::None;
            m_LoadState = PackageLoadState::Unloaded;
        }
    }

    // ========================================
    // State access
    // ========================================

    PackageLoadState Package::GetLoadState() const
    {
        Thread::ScopedLock lock(m_LoadMutex);
        return m_LoadState;
    }

    bool Package::IsLoaded() const
    {
        return GetLoadState() == PackageLoadState::Loaded;
    }

    bool Package::IsLoading() const
    {
        return GetLoadState() == PackageLoadState::Loading;
    }

    PackageFormat Package::GetFormat() const
    {
        Thread::ScopedLock lock(m_LoadMutex);
        return m_Format;
    }

    size_t Package::GetEntryCount() const
    {
        Thread::ScopedLock lock(m_LoadMutex);
        return m_Entries.size();
    }

    bool Package::GetEntry(size_t index, PackageEntry &outEntry) const
    {
        Thread::ScopedLock lock(m_LoadMutex);
        if (m_LoadState != PackageLoadState::Loaded || index >= m_Entries.size())
        {
            return false;
        }

        outEntry = m_Entries[index];
        return true;
    }

    bool Package::FindEntry(const Core::Container::AnsiString &name,
                            Core::Asset::AssetPackageFourCC type,
                            PackageEntry &outEntry) const
    {
        Thread::ScopedLock lock(m_LoadMutex);
        if (m_LoadState != PackageLoadState::Loaded)
        {
            return false;
        }

        for (const PackageEntry &entry : m_Entries)
        {
            if (entry.Name == name && entry.Type == type)
            {
                outEntry = entry;
                return true;
            }
        }
        return false;
    }

    Core::Asset::AssetBlob Package::OpenEntry(const PackageEntry &entry) const
    {
        StoragePtr storage;
        PackageEntry matchedEntry;
        Core::Container::AnsiString sourcePath;
        {
            Thread::ScopedLock lock(m_LoadMutex);
            if (m_LoadState != PackageLoadState::Loaded || !m_Storage)
            {
                return Core::Asset::AssetBlob::Invalid();
            }

            if (!TryCopyMatchingEntry(m_Entries, entry, matchedEntry))
            {
                return Core::Asset::AssetBlob::Invalid();
            }

            storage = m_Storage;
            sourcePath = m_SourcePathAnsi;
        }

        return Core::Asset::AssetBlob::FromOwnedRange(storage, matchedEntry.DataOffset, matchedEntry.StoredSize, sourcePath);
    }

    Core::Asset::AssetBlob Package::OpenEntry(const Core::Container::AnsiString &name,
                                              Core::Asset::AssetPackageFourCC type) const
    {
        PackageEntry entry;
        if (!FindEntry(name, type, entry))
        {
            return Core::Asset::AssetBlob::Invalid();
        }
        return OpenEntry(entry);
    }

    // ========================================
    // Buffer access
    // ========================================

    const uint8_t *Package::GetData() const
    {
        StoragePtr storage;
        {
            Thread::ScopedLock lock(m_LoadMutex);
            if (m_LoadState != PackageLoadState::Loaded || !m_Storage || m_Storage->empty())
            {
                return nullptr;
            }
            storage = m_Storage;
        }
        return storage->data();
    }

    size_t Package::GetDataSize() const
    {
        Thread::ScopedLock lock(m_LoadMutex);
        return m_Storage ? m_Storage->size() : 0;
    }

    Core::Container::Span<const uint8_t> Package::GetDataSpan() const
    {
        StoragePtr storage;
        {
            Thread::ScopedLock lock(m_LoadMutex);
            if (m_LoadState != PackageLoadState::Loaded || !m_Storage || m_Storage->empty())
            {
                return Core::Container::Span<const uint8_t>();
            }
            storage = m_Storage;
        }
        return Core::Container::Span<const uint8_t>(storage->data(), storage->size());
    }

    size_t Package::GetMemorySize() const
    {
        return GetDataSize();
    }

} // namespace NorvesLib::FileStream
