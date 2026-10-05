#include "Rendering/MegaGeometry/CookedMeshPageSource.h"

#include "Container/Containers.h"
#include "Logging/LogMacros.h"
#include "Thread/JobSystem.h"
#include "Thread/Mutex.h"
#include "Thread/Task.h"

#include <cstring>
#include <exception>
#include <utility>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    namespace
    {
        // ページの中身は、頂点（32 バイト）・クラスタのインデックス（uint32）を並べて返す
        static_assert(sizeof(Asset::CookedMeshVertex) == 32, "ページの頂点の大きさが Mesh3DVertex と一致しません");

        // ページの表から写した、読み込みに要る情報
        struct PageRecord
        {
            uint64_t FileOffset = 0;
            uint32_t Size = 0;
            uint64_t Hash = 0;
            bool bRoot = false;
            uint32_t ClusterCount = 0;
            uint32_t VertexCount = 0;
            uint32_t IndexCount = 0;
        };

        // ジョブが窓口より長く生きてもよいように、結果の置き場を共有する
        struct Shared
        {
            Asset::AssetFileReader Reader;
            Asset::AssetReadRequest Request;
            uint64_t BaseOffset = 0;
            uint32_t LodLevelCount = 1;
            uint32_t GroupCount = 0;
            Container::VariableArray<PageRecord> Pages;

            Thread::Mutex Mutex;
            Container::VariableArray<GeometryPageReadResult> Completed;
        };

        // ページを 1 つ読んで、頂点とインデックスの並びへ直す（失敗は result の bSucceeded が false）
        GeometryPageReadResult ReadPage(const Shared &shared, uint32_t pageId)
        {
            GeometryPageReadResult result;
            result.PageId = pageId;
            try
            {
                const PageRecord &page = shared.Pages[pageId];
                uint64_t fileOffset = 0;
                if (page.FileOffset > ~0ull - shared.BaseOffset)
                {
                    return result;
                }
                fileOffset = shared.BaseOffset + page.FileOffset;
                const Asset::AssetReadResult read =
                    shared.Reader.ReadRange(shared.Request, fileOffset, static_cast<size_t>(page.Size));
                if (!read.Succeeded() || read.Blob.GetSize() != page.Size)
                {
                    return result;
                }
                const Container::Span<const uint8_t> pageBytes = read.Blob.GetSpan();
                if (Asset::ComputeCookedMeshPayloadHash(pageBytes) != page.Hash)
                {
                    return result;
                }

                Asset::CookedMeshPageContent content;
                if (Asset::ParseCookedMeshPage(pageBytes, pageId, page.bRoot, shared.LodLevelCount, shared.GroupCount,
                                               content) != Asset::CookedMeshParseStatus::Success ||
                    content.Vertices.size() != page.VertexCount || content.Indices.size() != page.IndexCount ||
                    content.Clusters.size() != page.ClusterCount)
                {
                    return result;
                }

                const size_t vertexBytes = static_cast<size_t>(page.VertexCount) * sizeof(Asset::CookedMeshVertex);
                const size_t indexBytes = static_cast<size_t>(page.IndexCount) * sizeof(uint32_t);
                result.Data.resize(vertexBytes + indexBytes);
                std::memcpy(result.Data.data(), content.Vertices.data(), vertexBytes);
                std::memcpy(result.Data.data() + vertexBytes, content.Indices.data(), indexBytes);
                result.bSucceeded = true;
            }
            catch (const std::exception &)
            {
                result.bSucceeded = false;
                result.Data.clear();
            }
            return result;
        }

        class CookedMeshPageSource final : public IGeometryPageSource
        {
        public:
            explicit CookedMeshPageSource(Container::TSharedPtr<Shared> shared) : m_Shared(std::move(shared)) {}

            bool BeginRead(uint32_t pageId) override
            {
                if (pageId >= m_Shared->Pages.size() || m_Shared->Pages[pageId].bRoot)
                {
                    return false;
                }
                Container::TSharedPtr<Shared> shared = m_Shared;
                auto readPage = [shared, pageId]()
                {
                    GeometryPageReadResult result = ReadPage(*shared, pageId);
                    Thread::ScopedLock lock(shared->Mutex);
                    shared->Completed.push_back(std::move(result));
                };
                Thread::TaskPtr task = Thread::Task::Create(readPage, Thread::TaskPriority::NORMAL);
                return Thread::JobSystem::Get().SubmitTask(task);
            }

            void CollectCompleted(VariableArray<GeometryPageReadResult> &out) override
            {
                Thread::ScopedLock lock(m_Shared->Mutex);
                for (GeometryPageReadResult &result : m_Shared->Completed)
                {
                    out.push_back(std::move(result));
                }
                m_Shared->Completed.clear();
            }

        private:
            Container::TSharedPtr<Shared> m_Shared;
        };
    } // namespace

    Container::TSharedPtr<IGeometryPageSource> MakeCookedMeshPageSource(const Asset::CookedMeshData &cooked,
                                                                       const Asset::AssetFileReader &reader,
                                                                       const Asset::AssetReadRequest &request,
                                                                       uint64_t baseOffset)
    {
        if (cooked.Pages.size() < 2)
        {
            return nullptr;
        }
        Container::TSharedPtr<Shared> shared = Container::MakeShared<Shared>();
        shared->Reader = reader;
        shared->Request = request;
        shared->BaseOffset = baseOffset;
        shared->LodLevelCount = cooked.LODLevelCount;
        shared->GroupCount = static_cast<uint32_t>(cooked.Groups.size());
        shared->Pages.reserve(cooked.Pages.size());
        for (const Asset::CookedMeshPage &page : cooked.Pages)
        {
            PageRecord record;
            record.FileOffset = page.FileOffset;
            record.Size = page.Size;
            record.Hash = page.Hash;
            record.bRoot = page.bIsRoot;
            record.ClusterCount = page.ClusterCount;
            record.VertexCount = page.VertexCount;
            record.IndexCount = page.IndexCount;
            shared->Pages.push_back(record);
        }
        return Container::MakeShared<CookedMeshPageSource>(std::move(shared));
    }
} // namespace NorvesLib::Core::Rendering::MegaGeometry
