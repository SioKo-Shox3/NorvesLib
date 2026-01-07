#include "Resource/MeshResource.h"
#include "Object/Reflection.h"
#include "Logging/LogMacros.h"
#include <cmath>

namespace NorvesLib::Core
{

    // ========================================
    // リフレクション実装
    // ========================================

    // IMPLEMENT_CLASSマクロを使用してリフレクション実装を生成
    IMPLEMENT_CLASS(MeshResource, Resource)

    // ========================================
    // コンストラクタ・デストラクタ
    // ========================================

    MeshResource::MeshResource()
        : Resource(), m_VertexCount(0), m_Topology(Rendering::PrimitiveTopology::TriangleList)
    {
    }

    MeshResource::MeshResource(const FieldInitializer *initializer)
        : Resource(initializer), m_VertexCount(0), m_Topology(Rendering::PrimitiveTopology::TriangleList)
    {
    }

    MeshResource::MeshResource(const IUnknown *sourceObject)
        : Resource(sourceObject), m_VertexCount(0), m_Topology(Rendering::PrimitiveTopology::TriangleList)
    {
        // ソースがMeshResourceの場合、データをコピー
        if (sourceObject)
        {
            const MeshResource *sourceMesh = dynamic_cast<const MeshResource *>(sourceObject);
            if (sourceMesh)
            {
                m_VertexData = sourceMesh->m_VertexData;
                m_IndexData = sourceMesh->m_IndexData;
                m_VertexLayout = sourceMesh->m_VertexLayout;
                m_VertexCount = sourceMesh->m_VertexCount;
                m_Topology = sourceMesh->m_Topology;
            }
        }
    }

    MeshResource::~MeshResource()
    {
        Unload();
    }

    // ========================================
    // 初期化・終了処理
    // ========================================

    void MeshResource::Initialize()
    {
        Resource::Initialize();
    }

    void MeshResource::Finalize()
    {
        Unload();
        Resource::Finalize();
    }

    // ========================================
    // Resource実装
    // ========================================

    bool MeshResource::Load()
    {
        // MeshResourceはファイルからの直接ロードではなく、
        // Package経由でのデシリアライズまたはファクトリで作成される
        // ここでは状態のみ更新
        if (!m_VertexData.empty() && !m_IndexData.empty())
        {
            SetResourceState(ResourceState::Loaded);
            return true;
        }
        return false;
    }

    void MeshResource::Unload()
    {
        m_VertexData.clear();
        m_VertexData.shrink_to_fit();
        m_IndexData.clear();
        m_IndexData.shrink_to_fit();
        m_SubMeshes.clear();
        m_MaterialSlots.clear();
        m_VertexCount = 0;
        SetResourceState(ResourceState::Unloaded);
    }

    size_t MeshResource::GetMemorySize() const
    {
        size_t size = 0;
        size += m_VertexData.size();
        size += m_IndexData.size() * sizeof(uint32_t);
        size += m_SubMeshes.size() * sizeof(Rendering::SubMesh);
        size += m_MaterialSlots.size() * sizeof(Rendering::MaterialSlot);
        return size;
    }

    // ========================================
    // プリミティブ生成ファクトリ
    // ========================================

    Container::TSharedPtr<MeshResource> MeshResource::CreateBox(float width, float height, float depth)
    {
        auto mesh = Container::MakeShared<MeshResource>();
        if (!mesh)
        {
            return nullptr;
        }

        mesh->Initialize();

        float hw = width * 0.5f;
        float hh = height * 0.5f;
        float hd = depth * 0.5f;

        // 頂点データ（Position + Normal + UV）
        struct Vertex
        {
            float Position[3];
            float Normal[3];
            float UV[2];
        };

        Container::VariableArray<Vertex> vertices = {
            // Front face
            {{-hw, -hh, hd}, {0, 0, 1}, {0, 1}},
            {{hw, -hh, hd}, {0, 0, 1}, {1, 1}},
            {{hw, hh, hd}, {0, 0, 1}, {1, 0}},
            {{-hw, hh, hd}, {0, 0, 1}, {0, 0}},
            // Back face
            {{hw, -hh, -hd}, {0, 0, -1}, {0, 1}},
            {{-hw, -hh, -hd}, {0, 0, -1}, {1, 1}},
            {{-hw, hh, -hd}, {0, 0, -1}, {1, 0}},
            {{hw, hh, -hd}, {0, 0, -1}, {0, 0}},
            // Top face
            {{-hw, hh, hd}, {0, 1, 0}, {0, 1}},
            {{hw, hh, hd}, {0, 1, 0}, {1, 1}},
            {{hw, hh, -hd}, {0, 1, 0}, {1, 0}},
            {{-hw, hh, -hd}, {0, 1, 0}, {0, 0}},
            // Bottom face
            {{-hw, -hh, -hd}, {0, -1, 0}, {0, 1}},
            {{hw, -hh, -hd}, {0, -1, 0}, {1, 1}},
            {{hw, -hh, hd}, {0, -1, 0}, {1, 0}},
            {{-hw, -hh, hd}, {0, -1, 0}, {0, 0}},
            // Right face
            {{hw, -hh, hd}, {1, 0, 0}, {0, 1}},
            {{hw, -hh, -hd}, {1, 0, 0}, {1, 1}},
            {{hw, hh, -hd}, {1, 0, 0}, {1, 0}},
            {{hw, hh, hd}, {1, 0, 0}, {0, 0}},
            // Left face
            {{-hw, -hh, -hd}, {-1, 0, 0}, {0, 1}},
            {{-hw, -hh, hd}, {-1, 0, 0}, {1, 1}},
            {{-hw, hh, hd}, {-1, 0, 0}, {1, 0}},
            {{-hw, hh, -hd}, {-1, 0, 0}, {0, 0}},
        };

        Container::VariableArray<uint32_t> indices = {
            0, 1, 2, 0, 2, 3,       // Front
            4, 5, 6, 4, 6, 7,       // Back
            8, 9, 10, 8, 10, 11,    // Top
            12, 13, 14, 12, 14, 15, // Bottom
            16, 17, 18, 16, 18, 19, // Right
            20, 21, 22, 20, 22, 23  // Left
        };

        // 頂点データをバイト配列に変換
        Container::VariableArray<uint8_t> vertexData(vertices.size() * sizeof(Vertex));
        std::memcpy(vertexData.data(), vertices.data(), vertexData.size());

        mesh->SetVertexData(std::move(vertexData));
        mesh->SetIndexData(std::move(indices));
        mesh->m_VertexCount = 24;

        // 頂点レイアウト設定
        Rendering::VertexLayout layout;
        layout.AddElement(Rendering::VertexElement(Rendering::VertexSemantic::Position, Rendering::VertexFormat::Float3, 0));
        layout.AddElement(Rendering::VertexElement(Rendering::VertexSemantic::Normal, Rendering::VertexFormat::Float3, 12));
        layout.AddElement(Rendering::VertexElement(Rendering::VertexSemantic::TexCoord0, Rendering::VertexFormat::Float2, 24));
        layout.Stride = sizeof(Vertex);
        mesh->SetVertexLayout(layout);

        // サブメッシュ
        mesh->AddSubMesh(Rendering::SubMesh(0, 36, 0, 0));

        // マテリアルスロット
        mesh->AddMaterialSlot(Rendering::MaterialSlot("Default"));

        // バウンディング
        Rendering::BoundingBox bounds;
        bounds.MinX = -hw;
        bounds.MinY = -hh;
        bounds.MinZ = -hd;
        bounds.MaxX = hw;
        bounds.MaxY = hh;
        bounds.MaxZ = hd;
        mesh->SetBounds(bounds);

        Rendering::BoundingSphere sphere;
        sphere.CenterX = 0;
        sphere.CenterY = 0;
        sphere.CenterZ = 0;
        sphere.Radius = std::sqrt(hw * hw + hh * hh + hd * hd);
        mesh->SetBoundingSphere(sphere);

        mesh->SetResourceState(ResourceState::Loaded);
        mesh->SetResourceName(Identity("Primitive_Box"));

        return mesh;
    }

    Container::TSharedPtr<MeshResource> MeshResource::CreateSphere(float radius, uint32_t segments, uint32_t rings)
    {
        auto mesh = Container::MakeShared<MeshResource>();
        if (!mesh)
        {
            return nullptr;
        }

        mesh->Initialize();

        struct Vertex
        {
            float Position[3];
            float Normal[3];
            float UV[2];
        };

        Container::VariableArray<Vertex> vertices;
        Container::VariableArray<uint32_t> indices;

        const float pi = 3.14159265359f;

        // 頂点生成
        for (uint32_t y = 0; y <= rings; ++y)
        {
            for (uint32_t x = 0; x <= segments; ++x)
            {
                float xSegment = static_cast<float>(x) / static_cast<float>(segments);
                float ySegment = static_cast<float>(y) / static_cast<float>(rings);
                float xPos = std::cos(xSegment * 2.0f * pi) * std::sin(ySegment * pi);
                float yPos = std::cos(ySegment * pi);
                float zPos = std::sin(xSegment * 2.0f * pi) * std::sin(ySegment * pi);

                Vertex v;
                v.Position[0] = xPos * radius;
                v.Position[1] = yPos * radius;
                v.Position[2] = zPos * radius;
                v.Normal[0] = xPos;
                v.Normal[1] = yPos;
                v.Normal[2] = zPos;
                v.UV[0] = xSegment;
                v.UV[1] = ySegment;
                vertices.push_back(v);
            }
        }

        // インデックス生成
        for (uint32_t y = 0; y < rings; ++y)
        {
            for (uint32_t x = 0; x < segments; ++x)
            {
                uint32_t current = y * (segments + 1) + x;
                uint32_t next = current + segments + 1;

                indices.push_back(current);
                indices.push_back(next);
                indices.push_back(current + 1);

                indices.push_back(current + 1);
                indices.push_back(next);
                indices.push_back(next + 1);
            }
        }

        // 頂点データをバイト配列に変換
        Container::VariableArray<uint8_t> vertexData(vertices.size() * sizeof(Vertex));
        std::memcpy(vertexData.data(), vertices.data(), vertexData.size());

        mesh->SetVertexData(std::move(vertexData));
        mesh->SetIndexData(std::move(indices));
        mesh->m_VertexCount = static_cast<uint32_t>(vertices.size());

        // 頂点レイアウト設定
        Rendering::VertexLayout layout;
        layout.AddElement(Rendering::VertexElement(Rendering::VertexSemantic::Position, Rendering::VertexFormat::Float3, 0));
        layout.AddElement(Rendering::VertexElement(Rendering::VertexSemantic::Normal, Rendering::VertexFormat::Float3, 12));
        layout.AddElement(Rendering::VertexElement(Rendering::VertexSemantic::TexCoord0, Rendering::VertexFormat::Float2, 24));
        layout.Stride = sizeof(Vertex);
        mesh->SetVertexLayout(layout);

        // サブメッシュ
        mesh->AddSubMesh(Rendering::SubMesh(0, static_cast<uint32_t>(indices.size()), 0, 0));

        // マテリアルスロット
        mesh->AddMaterialSlot(Rendering::MaterialSlot("Default"));

        // バウンディング
        Rendering::BoundingBox bounds;
        bounds.MinX = -radius;
        bounds.MinY = -radius;
        bounds.MinZ = -radius;
        bounds.MaxX = radius;
        bounds.MaxY = radius;
        bounds.MaxZ = radius;
        mesh->SetBounds(bounds);

        Rendering::BoundingSphere sphere;
        sphere.CenterX = 0;
        sphere.CenterY = 0;
        sphere.CenterZ = 0;
        sphere.Radius = radius;
        mesh->SetBoundingSphere(sphere);

        mesh->SetResourceState(ResourceState::Loaded);
        mesh->SetResourceName(Identity("Primitive_Sphere"));

        return mesh;
    }

    Container::TSharedPtr<MeshResource> MeshResource::CreatePlane(
        float width, float height, uint32_t subdivisionsX, uint32_t subdivisionsY)
    {
        auto mesh = Container::MakeShared<MeshResource>();
        if (!mesh)
        {
            return nullptr;
        }

        mesh->Initialize();

        struct Vertex
        {
            float Position[3];
            float Normal[3];
            float UV[2];
        };

        Container::VariableArray<Vertex> vertices;
        Container::VariableArray<uint32_t> indices;

        float hw = width * 0.5f;
        float hh = height * 0.5f;

        // 頂点生成
        for (uint32_t y = 0; y <= subdivisionsY; ++y)
        {
            for (uint32_t x = 0; x <= subdivisionsX; ++x)
            {
                float xPos = -hw + (static_cast<float>(x) / static_cast<float>(subdivisionsX)) * width;
                float zPos = -hh + (static_cast<float>(y) / static_cast<float>(subdivisionsY)) * height;

                Vertex v;
                v.Position[0] = xPos;
                v.Position[1] = 0.0f;
                v.Position[2] = zPos;
                v.Normal[0] = 0.0f;
                v.Normal[1] = 1.0f;
                v.Normal[2] = 0.0f;
                v.UV[0] = static_cast<float>(x) / static_cast<float>(subdivisionsX);
                v.UV[1] = static_cast<float>(y) / static_cast<float>(subdivisionsY);
                vertices.push_back(v);
            }
        }

        // インデックス生成
        for (uint32_t y = 0; y < subdivisionsY; ++y)
        {
            for (uint32_t x = 0; x < subdivisionsX; ++x)
            {
                uint32_t current = y * (subdivisionsX + 1) + x;
                uint32_t next = current + subdivisionsX + 1;

                indices.push_back(current);
                indices.push_back(next);
                indices.push_back(current + 1);

                indices.push_back(current + 1);
                indices.push_back(next);
                indices.push_back(next + 1);
            }
        }

        // 頂点データをバイト配列に変換
        Container::VariableArray<uint8_t> vertexData(vertices.size() * sizeof(Vertex));
        std::memcpy(vertexData.data(), vertices.data(), vertexData.size());

        mesh->SetVertexData(std::move(vertexData));
        mesh->SetIndexData(std::move(indices));
        mesh->m_VertexCount = static_cast<uint32_t>(vertices.size());

        // 頂点レイアウト設定
        Rendering::VertexLayout layout;
        layout.AddElement(Rendering::VertexElement(Rendering::VertexSemantic::Position, Rendering::VertexFormat::Float3, 0));
        layout.AddElement(Rendering::VertexElement(Rendering::VertexSemantic::Normal, Rendering::VertexFormat::Float3, 12));
        layout.AddElement(Rendering::VertexElement(Rendering::VertexSemantic::TexCoord0, Rendering::VertexFormat::Float2, 24));
        layout.Stride = sizeof(Vertex);
        mesh->SetVertexLayout(layout);

        // サブメッシュ
        mesh->AddSubMesh(Rendering::SubMesh(0, static_cast<uint32_t>(indices.size()), 0, 0));

        // マテリアルスロット
        mesh->AddMaterialSlot(Rendering::MaterialSlot("Default"));

        // バウンディング
        Rendering::BoundingBox bounds;
        bounds.MinX = -hw;
        bounds.MinY = 0.0f;
        bounds.MinZ = -hh;
        bounds.MaxX = hw;
        bounds.MaxY = 0.0f;
        bounds.MaxZ = hh;
        mesh->SetBounds(bounds);

        Rendering::BoundingSphere sphere;
        sphere.CenterX = 0;
        sphere.CenterY = 0;
        sphere.CenterZ = 0;
        sphere.Radius = std::sqrt(hw * hw + hh * hh);
        mesh->SetBoundingSphere(sphere);

        mesh->SetResourceState(ResourceState::Loaded);
        mesh->SetResourceName(Identity("Primitive_Plane"));

        return mesh;
    }

    Container::TSharedPtr<MeshResource> MeshResource::CreateCylinder(
        float radius, float height, uint32_t segments)
    {
        // TODO: 円柱メッシュ生成実装
        auto mesh = Container::MakeShared<MeshResource>();
        mesh->Initialize();
        mesh->SetResourceState(ResourceState::Loaded);
        mesh->SetResourceName(Identity("Primitive_Cylinder"));
        return mesh;
    }

    Container::TSharedPtr<MeshResource> MeshResource::CreateCone(
        float radius, float height, uint32_t segments)
    {
        // TODO: 円錐メッシュ生成実装
        auto mesh = Container::MakeShared<MeshResource>();
        mesh->Initialize();
        mesh->SetResourceState(ResourceState::Loaded);
        mesh->SetResourceName(Identity("Primitive_Cone"));
        return mesh;
    }

    Container::TSharedPtr<MeshResource> MeshResource::CreateFromInfo(
        const Rendering::MeshCreateInfo &createInfo)
    {
        auto mesh = Container::MakeShared<MeshResource>();
        if (!mesh)
        {
            return nullptr;
        }

        mesh->Initialize();

        // 頂点データをコピー
        if (createInfo.VertexData && createInfo.VertexDataSize > 0)
        {
            Container::VariableArray<uint8_t> vertexData(createInfo.VertexDataSize);
            std::memcpy(vertexData.data(), createInfo.VertexData, createInfo.VertexDataSize);
            mesh->SetVertexData(std::move(vertexData));
            mesh->m_VertexCount = createInfo.VertexCount;
        }

        // インデックスデータをコピー
        if (createInfo.IndexData && createInfo.IndexCount > 0)
        {
            Container::VariableArray<uint32_t> indexData(createInfo.IndexCount);
            std::memcpy(indexData.data(), createInfo.IndexData, createInfo.IndexCount * sizeof(uint32_t));
            mesh->SetIndexData(std::move(indexData));
        }

        mesh->SetVertexLayout(createInfo.Layout);
        mesh->SetTopology(createInfo.Topology);
        mesh->SetBounds(createInfo.Bounds);

        for (const auto &subMesh : createInfo.SubMeshes)
        {
            mesh->AddSubMesh(subMesh);
        }

        for (const auto &slot : createInfo.MaterialSlots)
        {
            mesh->AddMaterialSlot(slot);
        }

        mesh->SetResourceState(ResourceState::Loaded);

        return mesh;
    }

} // namespace NorvesLib::Core
