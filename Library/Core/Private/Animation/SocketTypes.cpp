#include "Animation/SocketTypes.h"
#include "Animation/SkeletalBindRowMath.h"
#include "Animation/SkeletonResource.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
namespace NorvesLib::Core::Animation
{
    namespace
    {
        bool Fail(SocketReport& report, SocketError error, const TCHAR* detail)
        {
            report.Error = error;
            report.Detail = detail;
            return false;
        }
        bool Finite(const Math::Transform& t)
        {
            const float values[] = {t.position.x, t.position.y, t.position.z, t.rotation.x, t.rotation.y,
                                    t.rotation.z, t.rotation.w, t.scale.x,    t.scale.y,    t.scale.z};
            for (float value : values)
                if (!std::isfinite(value))
                    return false;
            const double length = double(t.rotation.x) * t.rotation.x + double(t.rotation.y) * t.rotation.y +
                                  double(t.rotation.z) * t.rotation.z + double(t.rotation.w) * t.rotation.w;
            return std::fabs(length - 1) < 1e-4 && t.scale.x > 0 && t.scale.y > 0 && t.scale.z > 0;
        }
        bool Rigid(const Math::Transform& t)
        {
            return Finite(t) && t.scale.x == 1 && t.scale.y == 1 && t.scale.z == 1;
        }
        bool BoundedJson(const Container::String& text)
        {
            unsigned depth = 0;
            bool quoted = false, escaped = false;
            for (auto c : text)
            {
                if (quoted)
                {
                    if (escaped)
                        escaped = false;
                    else if (c == '\\')
                        escaped = true;
                    else if (c == '"')
                        quoted = false;
                    continue;
                }
                if (c == '"')
                {
                    quoted = true;
                    continue;
                }
                if (c == '{' || c == '[')
                {
                    if (++depth > 32)
                        return false;
                }
                else if (c == '}' || c == ']')
                {
                    if (!depth)
                        return false;
                    --depth;
                }
            }
            return depth == 0 && !quoted;
        }
        bool Unique(JsonValue object)
        {
            if (!object.IsObject() || object.GetObjectSize() > 64)
                return false;
            for (size_t i = 0; i < object.GetObjectSize(); ++i)
                for (size_t j = 0; j < i; ++j)
                    if (object.GetMemberName(i) == object.GetMemberName(j))
                        return false;
            return true;
        }
        bool Array(JsonValue value, float* out, size_t count)
        {
            if (!value.IsArray() || value.GetArraySize() != count)
                return false;
            for (size_t i = 0; i < count; ++i)
            {
                auto element = value.GetArrayElement(i);
                const double n = element.AsNumber();
                if (!element.IsNumber() || !std::isfinite(n) || std::fabs(n) > 3.402823466e38)
                    return false;
                out[i] = float(n);
            }
            return true;
        }
    } // namespace

    bool ParseSocketSettingsDocument(const Container::String& json, JsonDocument& doc, SocketReport& report)
    {
        report = {};
        if (json.size() > 1048576)
            return Fail(report, SocketError::LimitExceeded, _T("socket_json_size"));
        if (!BoundedJson(json) || !JsonDocument::TryParse(json, doc) || !Unique(doc.GetRoot()))
            return Fail(report, SocketError::InvalidJson, _T("socket_json"));
        const auto root = doc.GetRoot();
        if (root.HasMember("version") &&
            (!root.FindMember("version").IsIntegerLiteral() || root.FindMember("version").AsInt64() != 1))
            return Fail(report, SocketError::InvalidJson, _T("socket_version"));
        return true;
    }
    bool ReadSocketSettingsFile(const Container::String& path, Container::String& out, SocketReport& report)
    {
        report = {};
        auto fail = [&]() { return Fail(report, SocketError::FileReadFailed, _T("socket_file")); };
        try
        {
            const std::filesystem::path filePath(path.c_str());
            std::error_code error;
            if (!std::filesystem::is_regular_file(filePath, error) || error)
                return fail();
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file)
                return fail();
            const auto size = file.tellg();
            if (size <= 0 || size > 1048576)
                return fail();
            Container::VariableArray<uint8_t> bytes(size_t(size), 0);
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())) ||
                file.peek() != std::char_traits<char>::eof())
                return fail();
            const size_t skip = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
            const Container::Span<const uint8_t> text{bytes.data() + skip, bytes.size() - skip};
            using Char = Container::String::value_type;
            const auto measured = Asset::MeasureSkeletalNameDecoding<Char>(2, text);
            if (!measured.Succeeded())
                return fail();
            Container::VariableArray<Char> units(measured.CodeUnitCount);
            if (!Asset::DecodeSkeletalWireName(2, text, Container::Span<Char>{units.data(), units.size()}).Succeeded())
                return fail();
            out = Container::String(Container::StringView(units.data(), units.size()));
            return true;
        }
        catch (...)
        {
            return fail();
        }
    }
    bool ValidateSockets(Container::Span<const SocketDefinition> sockets, size_t joints, SocketReport& report)
    {
        report = {};
        if (sockets.size() > 256)
            return Fail(report, SocketError::LimitExceeded, _T("socket_count"));
        for (size_t i = 0; i < sockets.size(); ++i)
        {
            const auto& socket = sockets[i];
            const auto name = socket.Name.GetView();
            if (!socket.Name.IsValid() || name.empty() || name.size() > 1024 ||
                std::find(name.begin(), name.end(), TCHAR{}) != name.end())
                return Fail(report, SocketError::InvalidName, _T("socket_name"));
            for (size_t j = 0; j < i; ++j)
                if (sockets[j].Name == socket.Name)
                    return Fail(report, SocketError::DuplicateName, _T("socket_name"));
            if (socket.ParentJoint >= joints)
                return Fail(report, SocketError::UnknownJoint, _T("socket_parent"));
            if (!Rigid(socket.Offset))
                return Fail(report, SocketError::InvalidTransform, _T("socket_offset"));
        }
        return true;
    }
    bool ParseSockets(const Container::String& json, const SkeletonResource& skeleton,
                      Container::VariableArray<SocketDefinition>& out, SocketReport& report)
    {
        JsonDocument doc;
        if (!ParseSocketSettingsDocument(json, doc, report))
            return false;
        const auto root = doc.GetRoot();
        auto entries = root.FindMember("sockets");
        if (!entries.IsArray())
            return Fail(report, SocketError::InvalidJson, _T("sockets"));
        if (entries.GetArraySize() > 256)
            return Fail(report, SocketError::LimitExceeded, _T("socket_count"));
        Container::VariableArray<SocketDefinition> candidate;
        for (size_t i = 0; i < entries.GetArraySize(); ++i)
        {
            const auto object = entries.GetArrayElement(i), name = object.FindMember("name"),
                       parent = object.FindMember("parent");
            if (!Unique(object) || !name.IsString() || name.AsString().empty() || name.AsString().size() > 1024)
                return Fail(report, SocketError::InvalidName, _T("socket_name"));
            if (!parent.IsString() || parent.AsString().empty() || parent.AsString().size() > 1024)
                return Fail(report, SocketError::UnknownJoint, _T("socket_parent"));
            const int32_t joint = skeleton.FindJointIndex(Identity(parent.AsString()));
            if (joint < 0)
                return Fail(report, SocketError::UnknownJoint, _T("socket_parent"));
            SocketDefinition socket;
            socket.Name = Identity(name.AsString());
            socket.ParentJoint = uint32_t(joint);
            if (object.HasMember("position"))
            {
                float v[3];
                if (!Array(object.FindMember("position"), v, 3))
                    return Fail(report, SocketError::InvalidTransform, _T("socket_position"));
                socket.Offset.position = {v[0], v[1], v[2]};
            }
            if (object.HasMember("rotation"))
            {
                float v[4];
                if (!Array(object.FindMember("rotation"), v, 4))
                    return Fail(report, SocketError::InvalidTransform, _T("socket_rotation"));
                socket.Offset.rotation = {v[0], v[1], v[2], v[3]};
            }
            if (object.HasMember("scale"))
            {
                float v[3];
                if (!Array(object.FindMember("scale"), v, 3))
                    return Fail(report, SocketError::InvalidTransform, _T("socket_scale"));
                socket.Offset.scale = {v[0], v[1], v[2]};
            }
            candidate.push_back(socket);
        }
        if (!ValidateSockets(candidate, skeleton.GetPoseRuntime().Parents.size(), report))
            return false;
        out = std::move(candidate);
        return true;
    }

    bool ParseAttachProfiles(const Container::String& json, Container::VariableArray<AttachProfile>& out,
                             Math::Transform& grip, SocketReport& report)
    {
        JsonDocument doc;
        if (!ParseSocketSettingsDocument(json, doc, report))
            return false;
        const auto root = doc.GetRoot();
        const auto transform = [&](JsonValue object, Math::Transform& t) {
            if (!Unique(object))
                return false;
            if (object.HasMember("position"))
            {
                float v[3];
                if (!Array(object.FindMember("position"), v, 3))
                    return false;
                t.position = {v[0], v[1], v[2]};
            }
            if (object.HasMember("rotation"))
            {
                float v[4];
                if (!Array(object.FindMember("rotation"), v, 4))
                    return false;
                t.rotation = {v[0], v[1], v[2], v[3]};
            }
            if (object.HasMember("scale"))
            {
                float v[3];
                if (!Array(object.FindMember("scale"), v, 3))
                    return false;
                t.scale = {v[0], v[1], v[2]};
            }
            return Rigid(t);
        };
        auto profiles = root.FindMember("attachProfiles");
        if (!profiles.IsArray() || profiles.GetArraySize() > 64)
            return Fail(report, SocketError::LimitExceeded, _T("attachment_profiles"));
        Container::VariableArray<AttachProfile> candidate;
        Math::Transform candidateGrip;
        for (size_t i = 0; i < profiles.GetArraySize(); ++i)
        {
            const auto entry = profiles.GetArrayElement(i), name = entry.FindMember("name");
            if (!name.IsString() || name.AsString().empty() || name.AsString().size() > 1024)
                return Fail(report, SocketError::InvalidName, _T("profile_name"));
            AttachProfile profile;
            profile.Name = Identity(name.AsString());
            for (const auto& previous : candidate)
                if (previous.Name == profile.Name)
                    return Fail(report, SocketError::DuplicateName, _T("profile_name"));
            if (!transform(entry, profile.Offset))
                return Fail(report, SocketError::InvalidTransform, _T("profile_offset"));
            SocketDefinition check;
            check.Name = profile.Name;
            check.ParentJoint = 0;
            check.Offset = profile.Offset;
            if (!ValidateSockets({&check, 1}, 1, report))
                return false;
            candidate.push_back(profile);
        }
        if (root.HasMember("grip") && !transform(root.FindMember("grip"), candidateGrip))
            return Fail(report, SocketError::InvalidTransform, _T("item_grip"));
        out = std::move(candidate);
        grip = candidateGrip;
        return true;
    }
    bool ComposeSocketAttachment(const Math::Transform& socket, const Math::Transform& profile,
                                 const Math::Transform& grip, Math::Transform& out)
    {
        if (!Finite(socket) || !Rigid(profile) || !Rigid(grip))
            return false;
        const auto matrix = [](const Math::Transform& t) {
            return Math::MatrixUtils::CreateWorldRowVector(t.position, t.rotation, t.scale);
        };
        Math::Matrix4x4 inverse;
        if (!Detail::TryInverseMatrix(matrix(grip), inverse))
            return false;
        const auto world = inverse * matrix(profile) * matrix(socket);
        auto rotation = world;
        const float scale[] = {socket.scale.x, socket.scale.y, socket.scale.z};
        for (size_t c = 0; c < 3; ++c)
            for (size_t r = 0; r < 3; ++r)
                rotation.m[r][c] /= scale[c];
        rotation.m30 = rotation.m31 = rotation.m32 = 0;
        auto q = Math::QuaternionUtils::Normalize(Math::QuaternionUtils::FromRotationMatrix(rotation));
        const Math::Transform result(world.GetTranslationRow(), q, socket.scale);
        if (!Finite(result))
            return false;
        out = result;
        return true;
    }
    bool ExtractSocketRigidMatrix(const Math::Matrix4x4& matrix, Math::Matrix4x4& out)
    {
        for (float value : matrix.values)
            if (!std::isfinite(value))
                return false;
        if (std::fabs(matrix.m03) > 1e-6f || std::fabs(matrix.m13) > 1e-6f || std::fabs(matrix.m23) > 1e-6f ||
            std::fabs(matrix.m33 - 1) > 1e-6f)
            return false;
        double x[3] = {matrix.m00, matrix.m01, matrix.m02}, y[3] = {matrix.m10, matrix.m11, matrix.m12};
        const auto normalize = [](double* v) {
            const double length = std::hypot(v[0], v[1], v[2]);
            if (!std::isfinite(length) || length < 1e-12)
                return false;
            for (size_t i = 0; i < 3; ++i)
                v[i] /= length;
            return true;
        };
        if (!normalize(x))
            return false;
        const double dot = x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
        for (size_t i = 0; i < 3; ++i)
            y[i] -= dot * x[i];
        if (!normalize(y))
            return false;
        const double z[3] = {x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0]};
        if (z[0] * matrix.m20 + z[1] * matrix.m21 + z[2] * matrix.m22 <= 1e-12)
            return false;
        auto result = Math::Matrix4x4::Identity;
        for (size_t i = 0; i < 3; ++i)
        {
            result.m[0][i] = float(x[i]);
            result.m[1][i] = float(y[i]);
            result.m[2][i] = float(z[i]);
        }
        result.m30 = matrix.m30;
        result.m31 = matrix.m31;
        result.m32 = matrix.m32;
        out = result;
        return true;
    }
    bool BuildSocketWorldTransform(const Math::Matrix4x4& model, const Math::Transform& offset,
                                   const Math::Transform& owner, Math::Transform& out)
    {
        if (!Rigid(offset) || !Finite(owner))
            return false;
        Math::Matrix4x4 joint;
        if (!ExtractSocketRigidMatrix(model, joint))
            return false;
        const auto local =
            Math::MatrixUtils::CreateWorldRowVector(offset.position, offset.rotation, Math::Vector3::One) * joint;
        const auto world = local * Math::MatrixUtils::CreateWorldRowVector(owner.position, owner.rotation, owner.scale);
        auto rotation = world;
        const float scales[] = {owner.scale.x, owner.scale.y, owner.scale.z};
        for (size_t c = 0; c < 3; ++c)
            for (size_t r = 0; r < 3; ++r)
                rotation.m[r][c] /= scales[c];
        rotation.m30 = rotation.m31 = rotation.m32 = 0;
        const auto q = Math::QuaternionUtils::FromRotationMatrix(rotation);
        const double length = std::hypot(std::hypot(double(q.x), q.y), std::hypot(double(q.z), q.w));
        if (!std::isfinite(length) || length < 1e-12)
            return false;
        const Math::Transform result(
            world.GetTranslationRow(),
            Math::Quaternion(float(q.x / length), float(q.y / length), float(q.z / length), float(q.w / length)),
            owner.scale);
        if (!Finite(result))
            return false;
        out = result;
        return true;
    }
} // namespace NorvesLib::Core::Animation
