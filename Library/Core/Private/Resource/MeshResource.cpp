#include "Resource/MeshResource.h"
#include "Object/Reflection.h"
#include "Logging/LogMacros.h"
#include <algorithm>
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
        radius = std::max(radius, 1e-4f);
        height = std::max(height, 1e-4f);
        segments = segments < 3u ? 3u : segments;

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

        const float halfH = height * 0.5f;
        const float pi = 3.14159265359f;

        // 側面頂点（シーム複製、放射状法線）
        for (uint32_t i = 0; i <= segments; ++i)
        {
            const float segment = static_cast<float>(i) / static_cast<float>(segments);
            const float angle = 2.0f * pi * segment;
            const float cx = std::cos(angle);
            const float cz = std::sin(angle);

            Vertex bottom;
            bottom.Position[0] = radius * cx;
            bottom.Position[1] = -halfH;
            bottom.Position[2] = radius * cz;
            bottom.Normal[0] = cx;
            bottom.Normal[1] = 0.0f;
            bottom.Normal[2] = cz;
            bottom.UV[0] = segment;
            bottom.UV[1] = 0.0f;
            vertices.push_back(bottom);

            Vertex top;
            top.Position[0] = radius * cx;
            top.Position[1] = halfH;
            top.Position[2] = radius * cz;
            top.Normal[0] = cx;
            top.Normal[1] = 0.0f;
            top.Normal[2] = cz;
            top.UV[0] = segment;
            top.UV[1] = 1.0f;
            vertices.push_back(top);
        }

        // 側面: CCW outward。各 quad は (bottom_i, top_i, bottom_next) と (bottom_next, top_i, top_next)。
        for (uint32_t i = 0; i < segments; ++i)
        {
            const uint32_t bottomIndex = i * 2u;
            const uint32_t topIndex = bottomIndex + 1u;
            const uint32_t nextBottomIndex = (i + 1u) * 2u;
            const uint32_t nextTopIndex = nextBottomIndex + 1u;

            indices.push_back(bottomIndex);
            indices.push_back(topIndex);
            indices.push_back(nextBottomIndex);

            indices.push_back(nextBottomIndex);
            indices.push_back(topIndex);
            indices.push_back(nextTopIndex);
        }

        const uint32_t topCenterIndex = static_cast<uint32_t>(vertices.size());
        Vertex topCenter;
        topCenter.Position[0] = 0.0f;
        topCenter.Position[1] = halfH;
        topCenter.Position[2] = 0.0f;
        topCenter.Normal[0] = 0.0f;
        topCenter.Normal[1] = 1.0f;
        topCenter.Normal[2] = 0.0f;
        topCenter.UV[0] = 0.5f;
        topCenter.UV[1] = 0.5f;
        vertices.push_back(topCenter);

        const uint32_t topRingStart = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i < segments; ++i)
        {
            const float segment = static_cast<float>(i) / static_cast<float>(segments);
            const float angle = 2.0f * pi * segment;
            const float cx = std::cos(angle);
            const float cz = std::sin(angle);

            Vertex ring;
            ring.Position[0] = radius * cx;
            ring.Position[1] = halfH;
            ring.Position[2] = radius * cz;
            ring.Normal[0] = 0.0f;
            ring.Normal[1] = 1.0f;
            ring.Normal[2] = 0.0f;
            ring.UV[0] = 0.5f + cx * 0.5f;
            ring.UV[1] = 0.5f + cz * 0.5f;
            vertices.push_back(ring);
        }

        // 上キャップ: CCW outward (+Y)。右手系で +Y を向くよう (center, ring_next, ring_i)。
        for (uint32_t i = 0; i < segments; ++i)
        {
            const uint32_t current = topRingStart + i;
            const uint32_t next = topRingStart + ((i + 1u) % segments);

            indices.push_back(topCenterIndex);
            indices.push_back(next);
            indices.push_back(current);
        }

        const uint32_t bottomCenterIndex = static_cast<uint32_t>(vertices.size());
        Vertex bottomCenter;
        bottomCenter.Position[0] = 0.0f;
        bottomCenter.Position[1] = -halfH;
        bottomCenter.Position[2] = 0.0f;
        bottomCenter.Normal[0] = 0.0f;
        bottomCenter.Normal[1] = -1.0f;
        bottomCenter.Normal[2] = 0.0f;
        bottomCenter.UV[0] = 0.5f;
        bottomCenter.UV[1] = 0.5f;
        vertices.push_back(bottomCenter);

        const uint32_t bottomRingStart = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i < segments; ++i)
        {
            const float segment = static_cast<float>(i) / static_cast<float>(segments);
            const float angle = 2.0f * pi * segment;
            const float cx = std::cos(angle);
            const float cz = std::sin(angle);

            Vertex ring;
            ring.Position[0] = radius * cx;
            ring.Position[1] = -halfH;
            ring.Position[2] = radius * cz;
            ring.Normal[0] = 0.0f;
            ring.Normal[1] = -1.0f;
            ring.Normal[2] = 0.0f;
            ring.UV[0] = 0.5f + cx * 0.5f;
            ring.UV[1] = 0.5f + cz * 0.5f;
            vertices.push_back(ring);
        }

        // 下キャップ: CCW outward (-Y)。上キャップと逆向きの (center, ring_i, ring_next)。
        for (uint32_t i = 0; i < segments; ++i)
        {
            const uint32_t current = bottomRingStart + i;
            const uint32_t next = bottomRingStart + ((i + 1u) % segments);

            indices.push_back(bottomCenterIndex);
            indices.push_back(current);
            indices.push_back(next);
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
        mesh->AddSubMesh(Rendering::SubMesh(0, static_cast<uint32_t>(mesh->GetIndexData().size()), 0, 0));

        // マテリアルスロット
        mesh->AddMaterialSlot(Rendering::MaterialSlot("Default"));

        // バウンディング
        Rendering::BoundingBox bounds;
        bounds.MinX = -radius;
        bounds.MinY = -halfH;
        bounds.MinZ = -radius;
        bounds.MaxX = radius;
        bounds.MaxY = halfH;
        bounds.MaxZ = radius;
        mesh->SetBounds(bounds);

        Rendering::BoundingSphere sphere;
        sphere.CenterX = 0.0f;
        sphere.CenterY = 0.0f;
        sphere.CenterZ = 0.0f;
        sphere.Radius = std::sqrt(radius * radius + halfH * halfH);
        mesh->SetBoundingSphere(sphere);

        mesh->SetResourceState(ResourceState::Loaded);
        mesh->SetResourceName(Identity("Primitive_Cylinder"));

        return mesh;
    }

    Container::TSharedPtr<MeshResource> MeshResource::CreateCone(
        float radius, float height, uint32_t segments)
    {
        radius = std::max(radius, 1e-4f);
        height = std::max(height, 1e-4f);
        segments = segments < 3u ? 3u : segments;

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

        const float halfH = height * 0.5f;
        const float pi = 3.14159265359f;

        auto setSideNormal = [radius, height](float angle, float normal[3])
        {
            const float nx = height * std::cos(angle);
            const float ny = radius;
            const float nz = height * std::sin(angle);
            const float length = std::sqrt(nx * nx + ny * ny + nz * nz);

            normal[0] = nx / length;
            normal[1] = ny / length;
            normal[2] = nz / length;
        };

        // 側面頂点（セグメントごとに頂点複製し、斜面法線を設定）
        for (uint32_t i = 0; i < segments; ++i)
        {
            const float currentSegment = static_cast<float>(i) / static_cast<float>(segments);
            const float nextSegment = static_cast<float>(i + 1u) / static_cast<float>(segments);
            const float apexSegment = (static_cast<float>(i) + 0.5f) / static_cast<float>(segments);
            const float currentAngle = 2.0f * pi * currentSegment;
            const float nextAngle = 2.0f * pi * nextSegment;
            const float apexAngle = 2.0f * pi * apexSegment;

            const float currentCx = std::cos(currentAngle);
            const float currentCz = std::sin(currentAngle);
            const float nextCx = std::cos(nextAngle);
            const float nextCz = std::sin(nextAngle);

            const uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

            Vertex baseCurrent;
            baseCurrent.Position[0] = radius * currentCx;
            baseCurrent.Position[1] = -halfH;
            baseCurrent.Position[2] = radius * currentCz;
            setSideNormal(currentAngle, baseCurrent.Normal);
            baseCurrent.UV[0] = currentSegment;
            baseCurrent.UV[1] = 0.0f;
            vertices.push_back(baseCurrent);

            Vertex baseNext;
            baseNext.Position[0] = radius * nextCx;
            baseNext.Position[1] = -halfH;
            baseNext.Position[2] = radius * nextCz;
            setSideNormal(nextAngle, baseNext.Normal);
            baseNext.UV[0] = nextSegment;
            baseNext.UV[1] = 0.0f;
            vertices.push_back(baseNext);

            Vertex apex;
            apex.Position[0] = 0.0f;
            apex.Position[1] = halfH;
            apex.Position[2] = 0.0f;
            setSideNormal(apexAngle, apex.Normal);
            apex.UV[0] = apexSegment;
            apex.UV[1] = 1.0f;
            vertices.push_back(apex);

            // 側面: CCW outward。右手系で外向きになるよう (apex_i, base_next, base_i)。
            indices.push_back(baseIndex + 2u);
            indices.push_back(baseIndex + 1u);
            indices.push_back(baseIndex);
        }

        const uint32_t bottomCenterIndex = static_cast<uint32_t>(vertices.size());
        Vertex bottomCenter;
        bottomCenter.Position[0] = 0.0f;
        bottomCenter.Position[1] = -halfH;
        bottomCenter.Position[2] = 0.0f;
        bottomCenter.Normal[0] = 0.0f;
        bottomCenter.Normal[1] = -1.0f;
        bottomCenter.Normal[2] = 0.0f;
        bottomCenter.UV[0] = 0.5f;
        bottomCenter.UV[1] = 0.5f;
        vertices.push_back(bottomCenter);

        const uint32_t bottomRingStart = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i < segments; ++i)
        {
            const float segment = static_cast<float>(i) / static_cast<float>(segments);
            const float angle = 2.0f * pi * segment;
            const float cx = std::cos(angle);
            const float cz = std::sin(angle);

            Vertex ring;
            ring.Position[0] = radius * cx;
            ring.Position[1] = -halfH;
            ring.Position[2] = radius * cz;
            ring.Normal[0] = 0.0f;
            ring.Normal[1] = -1.0f;
            ring.Normal[2] = 0.0f;
            ring.UV[0] = 0.5f + cx * 0.5f;
            ring.UV[1] = 0.5f + cz * 0.5f;
            vertices.push_back(ring);
        }

        // 底キャップ: CCW outward (-Y)。外向きになるよう (center, ring_i, ring_next)。
        for (uint32_t i = 0; i < segments; ++i)
        {
            const uint32_t current = bottomRingStart + i;
            const uint32_t next = bottomRingStart + ((i + 1u) % segments);

            indices.push_back(bottomCenterIndex);
            indices.push_back(current);
            indices.push_back(next);
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
        mesh->AddSubMesh(Rendering::SubMesh(0, static_cast<uint32_t>(mesh->GetIndexData().size()), 0, 0));

        // マテリアルスロット
        mesh->AddMaterialSlot(Rendering::MaterialSlot("Default"));

        // バウンディング
        Rendering::BoundingBox bounds;
        bounds.MinX = -radius;
        bounds.MinY = -halfH;
        bounds.MinZ = -radius;
        bounds.MaxX = radius;
        bounds.MaxY = halfH;
        bounds.MaxZ = radius;
        mesh->SetBounds(bounds);

        Rendering::BoundingSphere sphere;
        sphere.CenterX = 0.0f;
        sphere.CenterY = 0.0f;
        sphere.CenterZ = 0.0f;
        sphere.Radius = std::sqrt(radius * radius + halfH * halfH);
        mesh->SetBoundingSphere(sphere);

        mesh->SetResourceState(ResourceState::Loaded);
        mesh->SetResourceName(Identity("Primitive_Cone"));

        return mesh;
    }

    Container::TSharedPtr<MeshResource> MeshResource::CreateTorus(
        float majorRadius, float minorRadius, uint32_t majorSegments, uint32_t minorSegments)
    {
        majorRadius = std::max(majorRadius, 1e-4f);
        minorRadius = std::max(minorRadius, 1e-4f);
        majorRadius = std::max(majorRadius, minorRadius);
        majorSegments = majorSegments < 3u ? 3u : majorSegments;
        minorSegments = minorSegments < 3u ? 3u : minorSegments;

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
        const uint32_t ringStride = minorSegments + 1u;

        auto normalize = [](float normal[3])
        {
            const float length = std::sqrt(
                normal[0] * normal[0] +
                normal[1] * normal[1] +
                normal[2] * normal[2]);

            if (length > 0.0f)
            {
                normal[0] /= length;
                normal[1] /= length;
                normal[2] /= length;
            }
        };

        // 頂点生成（major/minor ともシーム複製）
        for (uint32_t u = 0; u <= majorSegments; ++u)
        {
            const float uSegment = static_cast<float>(u) / static_cast<float>(majorSegments);
            const float au = 2.0f * pi * uSegment;
            const float cosU = std::cos(au);
            const float sinU = std::sin(au);

            for (uint32_t v = 0; v <= minorSegments; ++v)
            {
                const float vSegment = static_cast<float>(v) / static_cast<float>(minorSegments);
                const float av = 2.0f * pi * vSegment;
                const float cosV = std::cos(av);
                const float sinV = std::sin(av);
                const float ringRadius = majorRadius + minorRadius * cosV;

                Vertex vertex;
                vertex.Position[0] = ringRadius * cosU;
                vertex.Position[1] = minorRadius * sinV;
                vertex.Position[2] = ringRadius * sinU;
                vertex.Normal[0] = cosV * cosU;
                vertex.Normal[1] = sinV;
                vertex.Normal[2] = cosV * sinU;
                normalize(vertex.Normal);
                vertex.UV[0] = uSegment;
                vertex.UV[1] = vSegment;
                vertices.push_back(vertex);
            }
        }

        // 格子: CCW outward。パラメータ順の外積が内向きなので v 辺を先に取る。
        for (uint32_t u = 0; u < majorSegments; ++u)
        {
            for (uint32_t v = 0; v < minorSegments; ++v)
            {
                const uint32_t current = u * ringStride + v;
                const uint32_t nextU = current + ringStride;

                indices.push_back(current);
                indices.push_back(current + 1u);
                indices.push_back(nextU);

                indices.push_back(current + 1u);
                indices.push_back(nextU + 1u);
                indices.push_back(nextU);
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
        mesh->AddSubMesh(Rendering::SubMesh(0, static_cast<uint32_t>(mesh->GetIndexData().size()), 0, 0));

        // マテリアルスロット
        mesh->AddMaterialSlot(Rendering::MaterialSlot("Default"));

        // バウンディング
        const float boundsRadius = majorRadius + minorRadius;

        Rendering::BoundingBox bounds;
        bounds.MinX = -boundsRadius;
        bounds.MinY = -minorRadius;
        bounds.MinZ = -boundsRadius;
        bounds.MaxX = boundsRadius;
        bounds.MaxY = minorRadius;
        bounds.MaxZ = boundsRadius;
        mesh->SetBounds(bounds);

        Rendering::BoundingSphere sphere;
        sphere.CenterX = 0.0f;
        sphere.CenterY = 0.0f;
        sphere.CenterZ = 0.0f;
        sphere.Radius = boundsRadius;
        mesh->SetBoundingSphere(sphere);

        mesh->SetResourceState(ResourceState::Loaded);
        mesh->SetResourceName(Identity("Primitive_Torus"));

        return mesh;
    }

    Container::TSharedPtr<MeshResource> MeshResource::CreateCapsule(
        float radius, float cylinderHeight, uint32_t segments, uint32_t rings)
    {
        radius = std::max(radius, 1e-4f);
        cylinderHeight = std::max(cylinderHeight, 1e-4f);
        segments = segments < 3u ? 3u : segments;
        rings = rings < 3u ? 3u : rings;

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

        const float halfH = cylinderHeight * 0.5f;
        const float pi = 3.14159265359f;
        const uint32_t ringStride = segments + 1u;

        auto normalize = [](float normal[3])
        {
            const float length = std::sqrt(
                normal[0] * normal[0] +
                normal[1] * normal[1] +
                normal[2] * normal[2]);

            if (length > 0.0f)
            {
                normal[0] /= length;
                normal[1] /= length;
                normal[2] /= length;
            }
        };

        // 円柱側面頂点（シーム複製、放射状法線）
        for (uint32_t i = 0; i <= segments; ++i)
        {
            const float segment = static_cast<float>(i) / static_cast<float>(segments);
            const float angle = 2.0f * pi * segment;
            const float cx = std::cos(angle);
            const float cz = std::sin(angle);

            Vertex bottom;
            bottom.Position[0] = radius * cx;
            bottom.Position[1] = -halfH;
            bottom.Position[2] = radius * cz;
            bottom.Normal[0] = cx;
            bottom.Normal[1] = 0.0f;
            bottom.Normal[2] = cz;
            normalize(bottom.Normal);
            bottom.UV[0] = segment;
            bottom.UV[1] = 0.0f;
            vertices.push_back(bottom);

            Vertex top;
            top.Position[0] = radius * cx;
            top.Position[1] = halfH;
            top.Position[2] = radius * cz;
            top.Normal[0] = cx;
            top.Normal[1] = 0.0f;
            top.Normal[2] = cz;
            normalize(top.Normal);
            top.UV[0] = segment;
            top.UV[1] = 1.0f;
            vertices.push_back(top);
        }

        // 円柱側面: CCW outward。
        for (uint32_t i = 0; i < segments; ++i)
        {
            const uint32_t bottomIndex = i * 2u;
            const uint32_t topIndex = bottomIndex + 1u;
            const uint32_t nextBottomIndex = (i + 1u) * 2u;
            const uint32_t nextTopIndex = nextBottomIndex + 1u;

            indices.push_back(bottomIndex);
            indices.push_back(topIndex);
            indices.push_back(nextBottomIndex);

            indices.push_back(nextBottomIndex);
            indices.push_back(topIndex);
            indices.push_back(nextTopIndex);
        }

        const uint32_t topRingStart = static_cast<uint32_t>(vertices.size());

        // 上半球頂点（k=0 が赤道、k=rings が極）
        for (uint32_t k = 0; k <= rings; ++k)
        {
            const float ringSegment = static_cast<float>(k) / static_cast<float>(rings);
            const float phi = (pi * 0.5f) * ringSegment;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);
            const float y = halfH + radius * sinPhi;
            const float horizontalRadius = radius * cosPhi;

            for (uint32_t i = 0; i <= segments; ++i)
            {
                const float segment = static_cast<float>(i) / static_cast<float>(segments);
                const float angle = 2.0f * pi * segment;
                const float cx = std::cos(angle);
                const float cz = std::sin(angle);

                Vertex vertex;
                vertex.Position[0] = horizontalRadius * cx;
                vertex.Position[1] = y;
                vertex.Position[2] = horizontalRadius * cz;
                if (k == rings)
                {
                    vertex.Normal[0] = 0.0f;
                    vertex.Normal[1] = 1.0f;
                    vertex.Normal[2] = 0.0f;
                }
                else
                {
                    vertex.Normal[0] = cosPhi * cx;
                    vertex.Normal[1] = sinPhi;
                    vertex.Normal[2] = cosPhi * cz;
                    normalize(vertex.Normal);
                }
                vertex.UV[0] = segment;
                vertex.UV[1] = 0.5f + ringSegment * 0.5f;
                vertices.push_back(vertex);
            }
        }

        // 上半球: CCW outward。極は最後のリング頂点を使った扇。
        for (uint32_t k = 0; k < rings; ++k)
        {
            for (uint32_t i = 0; i < segments; ++i)
            {
                const uint32_t current = topRingStart + k * ringStride + i;
                const uint32_t nextRing = current + ringStride;

                if (k + 1u == rings)
                {
                    indices.push_back(current);
                    indices.push_back(nextRing);
                    indices.push_back(current + 1u);
                }
                else
                {
                    indices.push_back(current);
                    indices.push_back(nextRing);
                    indices.push_back(current + 1u);

                    indices.push_back(current + 1u);
                    indices.push_back(nextRing);
                    indices.push_back(nextRing + 1u);
                }
            }
        }

        const uint32_t bottomRingStart = static_cast<uint32_t>(vertices.size());

        // 下半球頂点（k=0 が赤道、k=rings が極）
        for (uint32_t k = 0; k <= rings; ++k)
        {
            const float ringSegment = static_cast<float>(k) / static_cast<float>(rings);
            const float phi = (pi * 0.5f) * ringSegment;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);
            const float y = -halfH - radius * sinPhi;
            const float horizontalRadius = radius * cosPhi;

            for (uint32_t i = 0; i <= segments; ++i)
            {
                const float segment = static_cast<float>(i) / static_cast<float>(segments);
                const float angle = 2.0f * pi * segment;
                const float cx = std::cos(angle);
                const float cz = std::sin(angle);

                Vertex vertex;
                vertex.Position[0] = horizontalRadius * cx;
                vertex.Position[1] = y;
                vertex.Position[2] = horizontalRadius * cz;
                if (k == rings)
                {
                    vertex.Normal[0] = 0.0f;
                    vertex.Normal[1] = -1.0f;
                    vertex.Normal[2] = 0.0f;
                }
                else
                {
                    vertex.Normal[0] = cosPhi * cx;
                    vertex.Normal[1] = -sinPhi;
                    vertex.Normal[2] = cosPhi * cz;
                    normalize(vertex.Normal);
                }
                vertex.UV[0] = segment;
                vertex.UV[1] = 0.5f - ringSegment * 0.5f;
                vertices.push_back(vertex);
            }
        }

        // 下半球: CCW outward。上半球と逆順で下向き外側を向ける。
        for (uint32_t k = 0; k < rings; ++k)
        {
            for (uint32_t i = 0; i < segments; ++i)
            {
                const uint32_t current = bottomRingStart + k * ringStride + i;
                const uint32_t nextRing = current + ringStride;

                if (k + 1u == rings)
                {
                    indices.push_back(current);
                    indices.push_back(current + 1u);
                    indices.push_back(nextRing);
                }
                else
                {
                    indices.push_back(current);
                    indices.push_back(current + 1u);
                    indices.push_back(nextRing);

                    indices.push_back(current + 1u);
                    indices.push_back(nextRing + 1u);
                    indices.push_back(nextRing);
                }
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
        mesh->AddSubMesh(Rendering::SubMesh(0, static_cast<uint32_t>(mesh->GetIndexData().size()), 0, 0));

        // マテリアルスロット
        mesh->AddMaterialSlot(Rendering::MaterialSlot("Default"));

        // バウンディング
        const float verticalExtent = halfH + radius;

        Rendering::BoundingBox bounds;
        bounds.MinX = -radius;
        bounds.MinY = -verticalExtent;
        bounds.MinZ = -radius;
        bounds.MaxX = radius;
        bounds.MaxY = verticalExtent;
        bounds.MaxZ = radius;
        mesh->SetBounds(bounds);

        Rendering::BoundingSphere sphere;
        sphere.CenterX = 0.0f;
        sphere.CenterY = 0.0f;
        sphere.CenterZ = 0.0f;
        sphere.Radius = verticalExtent;
        mesh->SetBoundingSphere(sphere);

        mesh->SetResourceState(ResourceState::Loaded);
        mesh->SetResourceName(Identity("Primitive_Capsule"));

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
