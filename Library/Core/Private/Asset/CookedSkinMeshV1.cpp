#include "Asset/CookedSkinMeshV1.h"
#include "Asset/RigSplitWire.h"
#include "Asset/RigSplitAllocationTestAccess.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Asset/CookedSkeletalWireValidation.h"
#include "Resource/SkeletalSubmeshLayout.h"
#include "Resource/SkeletalLimits.h"
#include "Animation/SkeletalBindRowMath.h"
#include <algorithm>
#include <bit>
#include <cmath>
namespace NorvesLib::Core::Skeletal
{
    namespace C = Container;
    namespace W = SplitWire;
    bool IsSplitLogicalPath(const C::AnsiString& path) noexcept
    {
        if (path.empty() || path.size() > 4096)
        {
            return false;
        }
        size_t segment = 0;
        for (size_t i = 0; i <= path.size(); ++i)
        {
            if (i == path.size() || path[i] == '/')
            {
                const size_t length = i - segment;
                if (!length || (length == 1 && path[segment] == '.') ||
                    (length == 2 && path[segment] == '.' && path[segment + 1] == '.'))
                {
                    return false;
                }
                segment = i + 1;
            }
            else
            {
                const unsigned char ch = path[i];
                if (ch < 33 || ch > 126 || ch == '\\' || ch == ':' || ch == '%' || ch == '?' || ch == '#')
                {
                    return false;
                }
            }
        }
        return true;
    }
    namespace
    {
        constexpr uint32_t Codes[] = {W::Four('S', 'T', 'R', 'S'), W::Four('T', 'J', 'N', 'T'),
                                      W::Four('S', 'R', 'E', 'F'), W::Four('V', 'E', 'R', 'T'),
                                      W::Four('I', 'N', 'D', 'X'), W::Four('I', 'B', 'M', 'S'),
                                      W::Four('M', 'N', 'G', 'T'), W::Four('S', 'U', 'B', 'M'),
                                      W::Four('M', 'S', 'L', 'T'), W::Four('M', 'A', 'T', 'S')};
        constexpr uint32_t Records[] = {1, 24, 64, 64, 4, 64, 64, 64, 32, 128};
        bool MatrixValid(const C::FixedArray<float, 16>& v)
        {
            // glTF affineのみ。GPU用column列を行ベクトルとして読む既存規約と同じ。
            if (v[3] != 0 || v[7] != 0 || v[11] != 0 || v[15] != 1)
            {
                return false;
            }
            const Math::Matrix4x4 m(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12],
                                    v[13], v[14], v[15]);
            Math::Matrix4x4 inverse;
            return Animation::Detail::TryInverseMatrix(m, inverse);
        }
        bool Utf8(const C::String& name, C::AnsiString& out, const RigV1Limits& limits)
        {
            const C::Span<const C::String::value_type> input{name.data(), name.size()};
            const auto measured = Asset::MeasureSkeletalNameEncoding(2, input);
            if (!measured.Succeeded() || !measured.ByteCount || measured.ByteCount > limits.MaxNameBytes)
            {
                return false;
            }
            W::Bytes b(measured.ByteCount);
            if (!Asset::EncodeSkeletalWireName(2, input, C::Span<uint8_t>{b.data(), b.size()}).Succeeded())
            {
                return false;
            }
            out = C::AnsiString(C::AnsiStringView(reinterpret_cast<const char*>(b.data()), b.size()));
            return true;
        }
        RigV1Status Validate(const SkinMeshV1Data& d, const RigV1Limits& limits, uint64_t& strings)
        {
            if (!IsValidRigV1Limits(limits))
            {
                return RigV1Status::InvalidInput;
            }
            if (!IsSplitLogicalPath(d.SkeletonPath))
            {
                return RigV1Status::InvalidName;
            }
            if (d.Topology.Joints.empty() || d.Topology.Joints.size() > limits.MaxJoints || d.Vertices.empty() ||
                d.Vertices.size() > limits.MaxVertices || d.Indices.empty() || d.Indices.size() > limits.MaxIndices ||
                d.SubMeshes.empty() || d.SubMeshes.size() > MaximumSubmeshCount || d.Slots.empty() ||
                d.Slots.size() > MaximumMaterialSlotCount || d.Materials.empty() ||
                d.Materials.size() > MaximumMaterialSlotCount ||
                d.InverseBindMatrices.size() != d.Topology.Joints.size())
            {
                return RigV1Status::LimitExceeded;
            }
            if (d.Materials.size() != d.Slots.size())
            {
                return RigV1Status::InvalidInput;
            }
            strings = d.SkeletonPath.size();
            const auto add = [&](const C::AnsiString& name)
            {
                if (!W::ValidName(name, limits))
                {
                    return false;
                }
                strings += name.size();
                return strings <= limits.MaxStringBytes;
            };
            for (const auto& j : d.Topology.Joints)
            {
                if (!add(j.Name))
                {
                    return RigV1Status::InvalidName;
                }
            }
            for (size_t i = 0; i < d.Slots.size(); ++i)
            {
                const auto& slot = d.Slots[i];
                if (!add(slot.Name) || slot.MaterialIndex >= d.Materials.size())
                {
                    return RigV1Status::InvalidName;
                }
                for (size_t j = 0; j < i; ++j)
                {
                    if (slot.Name == d.Slots[j].Name)
                    {
                        return RigV1Status::InvalidName;
                    }
                }
            }
            for (const auto& material : d.Materials)
            {
                auto record = material.Record;
                record.Albedo = {};
                record.Normal = {};
                record.Arm = {};
                record.Emissive = {};
                // ARM flagの参照必須条件を所有pathで検査し、元wire offsetは参照しない。
                record.Arm.Length = material.Textures[2].empty() ? 0 : 1;
                if (Asset::ValidateCookedMaterialRecord(record, 1) != Asset::CookedMaterialStatus::Success)
                {
                    return RigV1Status::InvalidInput;
                }
                for (const auto& path : material.Textures)
                {
                    if (!path.empty() && (!IsSplitLogicalPath(path) || !add(path)))
                    {
                        return RigV1Status::InvalidName;
                    }
                }
            }
            if (strings > limits.MaxStringBytes)
            {
                return RigV1Status::LimitExceeded;
            }
            if (!MatrixValid(d.MeshTransform))
            {
                return RigV1Status::InvalidInput;
            }
            for (const auto& m : d.InverseBindMatrices)
            {
                if (!MatrixValid(m))
                {
                    return RigV1Status::InvalidInput;
                }
            }
            for (const auto& v : d.Vertices)
            {
                const float numeric[] = {v.Position.X, v.Position.Y, v.Position.Z, v.Normal.X,
                                         v.Normal.Y,   v.Normal.Z,   v.TexCoord.U, v.TexCoord.V};
                for (float f : numeric)
                {
                    if (!std::isfinite(f))
                    {
                        return RigV1Status::InvalidInput;
                    }
                }
                double weight = 0;
                for (size_t i = 0; i < 4; ++i)
                {
                    if (v.JointIndices[i] >= d.Topology.Joints.size() || !std::isfinite(v.JointWeights[i]) ||
                        v.JointWeights[i] < 0 || v.JointWeights[i] > 1)
                    {
                        return RigV1Status::InvalidInput;
                    }
                    weight += v.JointWeights[i];
                }
                if (std::abs(weight - 1) > 1e-5)
                {
                    return RigV1Status::InvalidInput;
                }
            }
            if (!ValidateSkeletalSubmeshData({d.SubMeshes.data(), d.SubMeshes.size()},
                                             {d.Indices.data(), d.Indices.size()}, d.Vertices.size(), d.Slots.size())
                     .Succeeded())
            {
                return RigV1Status::InvalidInput;
            }
            return RigV1Status::Success;
        }
        RigV1Status Encode(const SkinMeshV1Data& d, W::Bytes& out, const RigV1Limits& limits)
        {
            uint64_t stringBytes = 0;
            const auto status = Validate(d, limits, stringBytes);
            if (status != RigV1Status::Success)
            {
                return status;
            }
            const uint64_t sizes[] = {
                stringBytes,          d.Topology.Joints.size() * 24,     64, d.Vertices.size() * 64,
                d.Indices.size() * 4, d.InverseBindMatrices.size() * 64, 64, d.SubMeshes.size() * 64,
                d.Slots.size() * 32,  d.Materials.size() * 128};
            uint64_t total = 576;
            for (const auto size : sizes)
            {
                total = ((total + 15) & ~uint64_t{15}) + size;
            }
            if (total > limits.MaxWireBytes)
            {
                return RigV1Status::LimitExceeded;
            }
            W::OutputSection sections[10];
            for (size_t i = 0; i < 10; ++i)
            {
                sections[i].Code = Codes[i];
                sections[i].Record = Records[i];
                sections[i].Data.reserve(size_t(sizes[i]));
            }
            W::WriteTopology(d.Topology, sections[0].Data, sections[1].Data);
            auto& ref = sections[2].Data;
            ref.resize(64, 0);
            W::W64(ref, 0, W::AppendName(sections[0].Data, d.SkeletonPath));
            W::W32(ref, 8, uint32_t(d.SkeletonPath.size()));
            W::W32(ref, 12, 1);
            W::W64(ref, 16, d.Topology.SkeletonId);
            W::W64(ref, 24, d.SkeletonContentHash);
            W::W64(ref, 32, d.SkeletonRestHash);
            W::W64(ref, 40, d.SkeletonRootHash);
            auto& vertices = sections[3].Data;
            vertices.resize(d.Vertices.size() * 64, 0);
            for (size_t i = 0; i < d.Vertices.size(); ++i)
            {
                const auto& v = d.Vertices[i];
                const size_t o = i * 64;
                const float f[] = {v.Position.X, v.Position.Y, v.Position.Z, v.Normal.X,
                                   v.Normal.Y,   v.Normal.Z,   v.TexCoord.U, v.TexCoord.V};
                for (size_t j = 0; j < 8; ++j)
                {
                    W::WF(vertices, o + j * 4, f[j]);
                }
                for (size_t j = 0; j < 4; ++j)
                {
                    W::W32(vertices, o + 32 + j * 4, v.JointIndices[j]);
                    W::WF(vertices, o + 48 + j * 4, v.JointWeights[j]);
                }
            }
            sections[4].Data.resize(d.Indices.size() * 4, 0);
            for (size_t i = 0; i < d.Indices.size(); ++i)
            {
                W::W32(sections[4].Data, i * 4, d.Indices[i]);
            }
            sections[5].Data.resize(d.InverseBindMatrices.size() * 64, 0);
            for (size_t i = 0; i < d.InverseBindMatrices.size(); ++i)
            {
                for (size_t j = 0; j < 16; ++j)
                {
                    W::WF(sections[5].Data, i * 64 + j * 4, d.InverseBindMatrices[i][j]);
                }
            }
            sections[6].Data.resize(64, 0);
            for (size_t j = 0; j < 16; ++j)
            {
                W::WF(sections[6].Data, j * 4, d.MeshTransform[j]);
            }
            sections[7].Data.resize(d.SubMeshes.size() * 64, 0);
            for (size_t i = 0; i < d.SubMeshes.size(); ++i)
            {
                const auto& s = d.SubMeshes[i];
                auto& b = sections[7].Data;
                const size_t o = i * 64;
                W::W32(b, o, s.IndexStart);
                W::W32(b, o + 4, s.IndexCount);
                W::W32(b, o + 12, s.VertexCount);
                W::W32(b, o + 16, s.MaterialSlot);
                W::W32(b, o + 20, s.bNoShadow ? 1 : 0);
                for (size_t j = 0; j < 3; ++j)
                {
                    W::WF(b, o + 24 + j * 4, s.BoundsCenter[j]);
                }
                W::WF(b, o + 36, s.BoundsRadius);
            }
            sections[8].Data.resize(d.Slots.size() * 32, 0);
            for (size_t i = 0; i < d.Slots.size(); ++i)
            {
                const auto& s = d.Slots[i];
                auto& b = sections[8].Data;
                const size_t o = i * 32;
                W::W64(b, o, W::AppendName(sections[0].Data, s.Name));
                W::W32(b, o + 8, uint32_t(s.Name.size()));
                W::W32(b, o + 16, s.MaterialIndex);
            }
            sections[9].Data.resize(d.Materials.size() * 128, 0);
            for (size_t i = 0; i < d.Materials.size(); ++i)
            {
                auto record = d.Materials[i].Record;
                Asset::CookedMaterialStringRef* refs[] = {&record.Albedo, &record.Normal, &record.Arm,
                                                          &record.Emissive};
                for (size_t j = 0; j < 4; ++j)
                {
                    const auto& path = d.Materials[i].Textures[j];
                    *refs[j] = path.empty() ? Asset::CookedMaterialStringRef{}
                                            : Asset::CookedMaterialStringRef{W::AppendName(sections[0].Data, path),
                                                                             uint32_t(path.size())};
                }
                if (Asset::WriteCookedMaterialRecord(record, stringBytes, {sections[9].Data.data() + i * 128, 128}) !=
                    Asset::CookedMaterialStatus::Success)
                {
                    return RigV1Status::InvalidInput;
                }
            }
            return W::WriteEnvelope(2, d.Topology.SkeletonId, {sections, 10}, out, limits);
        }
    } // namespace
    bool BuildSkinMeshV1(const RigAuthoringCpu& source, const SkeletonV1& skeleton, const C::AnsiString& path,
                         C::Span<const SkinMaterialV1> materials, SkinMeshV1& out, RigV1Report& report,
                         const RigV1Limits& limits)
    {
        report = {};
        try
        {
            const auto* rig = source.GetData();
            const auto* sk = skeleton.GetData();
            if (!rig || !sk || !IsValidRigV1Limits(limits) || !IsSplitLogicalPath(path))
            {
                return false;
            }
            if (!SameRigTopology(rig->Topology, sk->Topology))
            {
                report.Status = RigV1Status::TopologyMismatch;
                return false;
            }
            const auto& g = rig->Geometry;
            const size_t slotCount = g.MaterialSlots.empty() ? 1 : g.MaterialSlots.size();
            if (g.Vertices.size() > limits.MaxVertices || g.Indices.size() > limits.MaxIndices ||
                g.Joints.size() > limits.MaxJoints || slotCount > MaximumMaterialSlotCount ||
                g.SubMeshes.size() > MaximumSubmeshCount)
            {
                report.Status = RigV1Status::LimitExceeded;
                return false;
            }
            if (materials.size() != slotCount || !materials.data())
            {
                return false;
            }
            // 呼出し側の所有値でも、名前や材質列をcopyする前に有限化する。
            uint64_t nameBytes = path.size();
            for (const auto& j : sk->Topology.Joints)
            {
                if (!W::ValidName(j.Name, limits))
                {
                    report.Status = RigV1Status::InvalidName;
                    return false;
                }
                nameBytes += j.Name.size();
            }
            for (const auto& material : materials)
            {
                for (const auto& texture : material.Textures)
                {
                    if (texture.size() > limits.MaxNameBytes)
                    {
                        report.Status = RigV1Status::LimitExceeded;
                        return false;
                    }
                    nameBytes += texture.size();
                }
            }
            for (const auto& slot : g.MaterialSlots)
            {
                const auto measured = Asset::MeasureSkeletalNameEncoding(
                    2, C::Span<const C::String::value_type>{slot.Name.data(), slot.Name.size()});
                if (!measured.Succeeded() || measured.ByteCount > limits.MaxNameBytes)
                {
                    report.Status = RigV1Status::InvalidName;
                    return false;
                }
                nameBytes += measured.ByteCount;
            }
            if (g.MaterialSlots.empty())
            {
                nameBytes += 10;
            }
            const uint64_t preflightSizes[] = {nameBytes,
                                               sk->Topology.Joints.size() * 24,
                                               64,
                                               g.Vertices.size() * 64,
                                               g.Indices.size() * 4,
                                               sk->Topology.Joints.size() * 64,
                                               64,
                                               std::max<size_t>(g.SubMeshes.size(), 1) * 64,
                                               slotCount * 32,
                                               slotCount * 128};
            uint64_t worstWire = 576;
            for (auto bytes : preflightSizes)
            {
                worstWire = ((worstWire + 15) & ~uint64_t{15}) + bytes;
            }
            if (nameBytes > limits.MaxStringBytes || worstWire > limits.MaxWireBytes)
            {
                report.Status = RigV1Status::LimitExceeded;
                return false;
            }
            // メッシュが作られたrestを現在Skeletonへ黙って差替えない。名前一致だけでは不十分。
            for (size_t i = 0; i < sk->CurrentRest.Rest.size(); ++i)
            {
                const auto& a = rig->LocalRest[rig->Topology.CanonicalToSource[i]];
                const auto& b = sk->CurrentRest.Rest[i];
                const float av[] = {a.Translation.X, a.Translation.Y, a.Translation.Z, a.Rotation.X, a.Rotation.Y,
                                    a.Rotation.Z,    a.Rotation.W,    a.Scale.X,       a.Scale.Y,    a.Scale.Z};
                const float bv[] = {b.Translation.X, b.Translation.Y, b.Translation.Z, b.Rotation.X, b.Rotation.Y,
                                    b.Rotation.Z,    b.Rotation.W,    b.Scale.X,       b.Scale.Y,    b.Scale.Z};
                bool bSame = true;
                for (size_t n = 0; n < 10; ++n)
                {
                    bSame = bSame && std::bit_cast<uint32_t>(av[n]) == std::bit_cast<uint32_t>(bv[n]);
                }
                if (!bSame)
                {
                    report.Status = RigV1Status::RestMismatch;
                    return false;
                }
            }
            SkinMeshV1Data data;
            data.Topology = sk->Topology;
            data.SkeletonPath = path;
            data.SkeletonContentHash = sk->ContentHash;
            data.SkeletonRestHash = sk->CurrentRest.RestHash;
            data.SkeletonRootHash = sk->RootHash;
            data.Vertices = g.Vertices;
            data.Indices = g.Indices;
            data.MeshTransform = g.MeshNodeGlobalTransform;
            data.SubMeshes = g.SubMeshes;
            for (auto index : rig->Topology.CanonicalToSource)
            {
                data.InverseBindMatrices.push_back(g.Joints[index].InverseBindMatrix);
            }
            for (auto& v : data.Vertices)
            {
                for (auto& joint : v.JointIndices)
                {
                    if (joint >= rig->Topology.SourceToCanonical.size())
                    {
                        return false;
                    }
                    joint = rig->Topology.SourceToCanonical[joint];
                }
            }
            if (g.MaterialSlots.empty())
            {
                if (!g.SubMeshes.empty())
                {
                    return false;
                }
                data.Slots.push_back({C::AnsiString("Material_0"), 0});
                SkeletalSubMesh sub;
                sub.IndexCount = uint32_t(g.Indices.size());
                data.SubMeshes.push_back(sub);
            }
            else
            {
                for (size_t i = 0; i < slotCount; ++i)
                {
                    SkinSlotV1 slot;
                    slot.MaterialIndex = uint32_t(i);
                    if (!Utf8(g.MaterialSlots[i].Name, slot.Name, limits))
                    {
                        report.Status = RigV1Status::InvalidName;
                        return false;
                    }
                    data.Slots.push_back(std::move(slot));
                }
            }
            for (const auto& material : materials)
            {
                data.Materials.push_back(material);
            }
            W::Bytes bytes;
            report.Status = Encode(data, bytes, limits);
            if (report.Status != RigV1Status::Success)
            {
                return false;
            }
            return ParseSkinMeshV1(W::ViewOf(bytes), out, report, limits);
        }
        catch (...)
        {
            report.Status = RigV1Status::Exception;
            return false;
        }
    }
    bool WriteSkinMeshV1(const SkinMeshV1& mesh, W::Bytes& out, RigV1Report& report, const RigV1Limits& limits)
    {
        report = {};
        try
        {
            const auto* data = mesh.GetData();
            if (!data)
            {
                return false;
            }
            report.Status = Encode(*data, out, limits);
            report.SkeletonId = data->Topology.SkeletonId;
            return report.Status == RigV1Status::Success;
        }
        catch (...)
        {
            report.Status = RigV1Status::Exception;
            return false;
        }
    }
    bool MatchSkinMeshSkeletonV1(const SkinMeshV1& mesh, const SkeletonV1& skeleton, RigV1Report& report)
    {
        report = {};
        const auto* m = mesh.GetData();
        const auto* s = skeleton.GetData();
        if (!m || !s)
        {
            return false;
        }
        report.SkeletonId = s->Topology.SkeletonId;
        if (!SameRigTopology(m->Topology, s->Topology))
        {
            report.Status = RigV1Status::TopologyMismatch;
            return false;
        }
        if (m->SkeletonContentHash != s->ContentHash || m->SkeletonRestHash != s->CurrentRest.RestHash ||
            m->SkeletonRootHash != s->RootHash)
        {
            report.Status = RigV1Status::HashMismatch;
            return false;
        }
        report.Status = RigV1Status::Success;
        return true;
    }
    bool ParseSkinMeshV1(W::View bytes, SkinMeshV1& out, RigV1Report& report, const RigV1Limits& limits)
    {
        report = {};
        try
        {
            const auto fail = [&](RigV1Status status)
            {
                report.Status = status;
                return false;
            };
            W::Section s[10];
            for (size_t i = 0; i < 10; ++i)
            {
                s[i].Code = Codes[i];
                s[i].Record = Records[i];
            }
            report.Status = W::ReadEnvelope(bytes, 2, {s, 10}, limits);
            if (report.Status != RigV1Status::Success)
            {
                return false;
            }
            if (!s[0].Count || s[0].Count > limits.MaxStringBytes || !s[1].Count || s[1].Count > limits.MaxJoints ||
                !s[3].Count || s[3].Count > limits.MaxVertices || !s[4].Count || s[4].Count > limits.MaxIndices ||
                !s[7].Count || s[7].Count > MaximumSubmeshCount || !s[8].Count ||
                s[8].Count > MaximumMaterialSlotCount || !s[9].Count || s[9].Count > MaximumMaterialSlotCount)
            {
                return fail(RigV1Status::LimitExceeded);
            }
            if (s[2].Count != 1 || s[5].Count != s[1].Count || s[6].Count != 1 || s[9].Count != s[8].Count)
            {
                return fail(RigV1Status::BadWire);
            }
            const W::View strings{bytes.data() + s[0].Offset, size_t(s[0].Size)};
            if (!Asset::MeasureSkeletalNameDecoding<char>(2, strings).Succeeded())
            {
                return fail(RigV1Status::InvalidName);
            }
            uint64_t stringRemaining = limits.MaxStringBytes;
            const auto check = [&](size_t o, bool bEmpty = false)
            {
                return W::PreflightName(strings, W::U64(bytes, o), W::U32(bytes, o + 8), limits, stringRemaining,
                                        bEmpty);
            };
            for (uint32_t i = 0; i < s[1].Count; ++i)
            {
                const auto status = check(size_t(s[1].Offset) + i * 24);
                if (status != RigV1Status::Success)
                {
                    return fail(status);
                }
            }
            const auto referenceStatus = check(size_t(s[2].Offset));
            if (referenceStatus != RigV1Status::Success)
            {
                return fail(referenceStatus);
            }
            for (uint32_t i = 0; i < s[8].Count; ++i)
            {
                const auto status = check(size_t(s[8].Offset) + i * 32);
                if (status != RigV1Status::Success)
                {
                    return fail(status);
                }
            }
            for (uint32_t i = 0; i < s[9].Count; ++i)
            {
                for (size_t j = 0; j < 4; ++j)
                {
                    const auto status = check(size_t(s[9].Offset) + i * 128 + j * 16, true);
                    if (status != RigV1Status::Success)
                    {
                        return fail(status);
                    }
                }
            }
            Detail::ObserveSplitAllocation("mesh_owned");
            auto d = C::MakeShared<SkinMeshV1Data>();
            report.Status = W::ReadTopology(bytes, strings, s[1], limits, d->Topology);
            if (report.Status != RigV1Status::Success)
            {
                return false;
            }
            const size_t ref = size_t(s[2].Offset);
            if (!W::ReadName(strings, W::U64(bytes, ref), W::U32(bytes, ref + 8), d->SkeletonPath, limits) ||
                W::U32(bytes, ref + 12) != 1 || W::U64(bytes, ref + 16) != d->Topology.SkeletonId ||
                !W::Zero(bytes, ref + 48, ref + 64))
            {
                return fail(RigV1Status::BadWire);
            }
            d->SkeletonContentHash = W::U64(bytes, ref + 24);
            d->SkeletonRestHash = W::U64(bytes, ref + 32);
            d->SkeletonRootHash = W::U64(bytes, ref + 40);
            d->Vertices.resize(s[3].Count);
            for (uint32_t i = 0; i < s[3].Count; ++i)
            {
                const size_t o = size_t(s[3].Offset) + i * 64;
                auto& v = d->Vertices[i];
                v.Position = {W::F32(bytes, o), W::F32(bytes, o + 4), W::F32(bytes, o + 8)};
                v.Normal = {W::F32(bytes, o + 12), W::F32(bytes, o + 16), W::F32(bytes, o + 20)};
                v.TexCoord = {W::F32(bytes, o + 24), W::F32(bytes, o + 28)};
                for (size_t j = 0; j < 4; ++j)
                {
                    v.JointIndices[j] = W::U32(bytes, o + 32 + j * 4);
                    v.JointWeights[j] = W::F32(bytes, o + 48 + j * 4);
                }
            }
            d->Indices.resize(s[4].Count);
            for (size_t i = 0; i < d->Indices.size(); ++i)
            {
                d->Indices[i] = W::U32(bytes, size_t(s[4].Offset) + i * 4);
            }
            d->InverseBindMatrices.resize(s[5].Count);
            for (size_t i = 0; i < d->InverseBindMatrices.size(); ++i)
            {
                for (size_t j = 0; j < 16; ++j)
                {
                    d->InverseBindMatrices[i][j] = W::F32(bytes, size_t(s[5].Offset) + i * 64 + j * 4);
                }
            }
            for (size_t j = 0; j < 16; ++j)
            {
                d->MeshTransform[j] = W::F32(bytes, size_t(s[6].Offset) + j * 4);
            }
            d->SubMeshes.resize(s[7].Count);
            for (size_t i = 0; i < d->SubMeshes.size(); ++i)
            {
                if (Asset::ReadCookedSkeletalV02Submesh({bytes.data() + s[7].Offset + i * 64, 64}, d->Vertices.size(),
                                                        d->SubMeshes[i]) != Asset::CookedSkeletalWireStatus::Success)
                {
                    return fail(RigV1Status::BadWire);
                }
            }
            d->Slots.resize(s[8].Count);
            for (size_t i = 0; i < d->Slots.size(); ++i)
            {
                const size_t o = size_t(s[8].Offset) + i * 32;
                if (!W::ReadName(strings, W::U64(bytes, o), W::U32(bytes, o + 8), d->Slots[i].Name, limits) ||
                    W::U32(bytes, o + 12) || !W::Zero(bytes, o + 20, o + 32))
                {
                    return fail(RigV1Status::BadWire);
                }
                d->Slots[i].MaterialIndex = W::U32(bytes, o + 16);
            }
            d->Materials.resize(s[9].Count);
            for (size_t i = 0; i < d->Materials.size(); ++i)
            {
                auto& m = d->Materials[i];
                if (Asset::ReadCookedMaterialRecord({bytes.data() + s[9].Offset + i * 128, 128}, strings.size(),
                                                    m.Record) != Asset::CookedMaterialStatus::Success)
                {
                    return fail(RigV1Status::BadWire);
                }
                const Asset::CookedMaterialStringRef refs[] = {m.Record.Albedo, m.Record.Normal, m.Record.Arm,
                                                               m.Record.Emissive};
                for (size_t j = 0; j < 4; ++j)
                {
                    if (!refs[j].Length && refs[j].Offset)
                    {
                        return fail(RigV1Status::BadWire);
                    }
                    if (refs[j].Length && !W::ReadName(strings, refs[j].Offset, refs[j].Length, m.Textures[j], limits))
                    {
                        return fail(RigV1Status::InvalidName);
                    }
                }
                // bytes内offsetの借用を残さず、以後は所有Textから再encodeする。
                m.Record.Albedo = {};
                m.Record.Normal = {};
                m.Record.Arm = {};
                m.Record.Emissive = {};
            }
            uint64_t stringBytes = 0;
            report.Status = Validate(*d, limits, stringBytes);
            if (report.Status != RigV1Status::Success)
            {
                return false;
            }
            d->ContentHash = RigBytesHash(bytes);
            report.SkeletonId = d->Topology.SkeletonId;
            SkinMeshV1 candidate;
            candidate.m_Data = std::move(d);
            out = std::move(candidate);
            report.Status = RigV1Status::Success;
            return true;
        }
        catch (...)
        {
            report.Status = RigV1Status::Exception;
            return false;
        }
    }
} // namespace NorvesLib::Core::Skeletal
