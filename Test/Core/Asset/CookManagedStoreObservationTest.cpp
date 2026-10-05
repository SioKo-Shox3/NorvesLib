// 実filesystemに独立schema fixtureを置き、観測が変更も採用も行わないことを検証する。
#include "Tools/AssetCook/CookManagedStoreObservation.h"
#include "Tools/AssetCook/CookDestinationLock.h"
#include "Tools/AssetCook/NativeCookPath.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <source_location>
#if defined(_WIN32)
#include <Windows.h>
#endif
namespace StoreObservationTest
{
    struct Failure
    {
    };
    bool bAliasCleanupFailed = false;
} // namespace StoreObservationTest
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                       \
            throw StoreObservationTest::Failure{};                                                                     \
        }                                                                                                              \
    } while (false)
namespace StoreObservationTest
{
    using namespace NorvesLib::Tools::AssetCook;
    using Text = NorvesLib::Core::Container::AnsiString;
    template <class T> using Array = NorvesLib::Core::Container::VariableArray<T>;
    using Result = CookManagedStoreResult;
    constexpr char StoreId[] = "11111111111111111111111111111111";
    constexpr char OwnerId[] = "22222222222222222222222222222222";
    void Write(const std::filesystem::path& p, const Text& s)
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        CHECK(f);
        f.write(s.data(), static_cast<std::streamsize>(s.size()));
        f.close();
        CHECK(!f.fail());
    }
    Text Read(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        CHECK(f);
        Text s;
        char c;
        while (f.get(c))
        {
            s.push_back(c);
        }
        CHECK(f.eof());
        return s;
    }
#if defined(_WIN32)
    struct Fixture
    {
        std::filesystem::path Root, Spec;
        Fixture()
        {
            char name[96];
            std::snprintf(name, sizeof(name), "norves-store-observation-%lu-%llu", GetCurrentProcessId(),
                          static_cast<unsigned long long>(GetTickCount64()));
            Root = std::filesystem::temp_directory_path() / name;
            CHECK(std::filesystem::create_directory(Root));
            Spec = Root / "spec.json";
            Write(Spec, "{}");
        }
        ~Fixture()
        {
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }
        std::filesystem::path Workspace(const char* name)
        {
            auto p = Root / name;
            CHECK(std::filesystem::create_directory(p));
            return p;
        }
        CookOwnerResolveRequest Request(const std::filesystem::path& w, const char* leaf = "Runtime Long Name")
        {
            return {Spec, w / leaf, "manifest.json"};
        }
    };
    struct NativeIdentity
    {
        Text Id, Serial, Guid;
    };
    NativeIdentity Identity(const std::filesystem::path& p)
    {
        HANDLE h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE);
        FILE_ID_INFO id{};
        CHECK(GetFileInformationByHandleEx(h, FileIdInfo, &id, sizeof(id)));
        wchar_t full[32768]{}, canonical[50]{};
        auto n = GetFinalPathNameByHandleW(h, full, 32768, FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
        CHECK(CloseHandle(h));
        CHECK(n > 49 && n < 32768);
        full[49] = 0;
        CHECK(GetVolumeNameForVolumeMountPointW(full, canonical, 50));
        NativeIdentity out;
        char hex[33]{};
        for (size_t i = 0; i < 16; ++i)
        {
            std::snprintf(hex + i * 2, 3, "%02x", id.FileId.Identifier[i]);
        }
        out.Id = hex;
        std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(id.VolumeSerialNumber));
        out.Serial = hex;
        for (size_t i = 11; i < 47; ++i)
        {
            auto c = canonical[i];
            if (c >= L'A' && c <= L'F')
            {
                c += L'a' - L'A';
            }
            out.Guid.push_back(static_cast<char>(c));
        }
        return out;
    }
    Text Field(const char* key, const Text& value)
    {
        Text s = "\"";
        s.append(key);
        s.append("\":\"");
        s.append(value);
        s.append("\"");
        return s;
    }
    Text Header(const std::filesystem::path& w)
    {
        auto wi = Identity(w), si = Identity(w / ".norves-assetcook");
        Text s = "{\"producer\":\"NorvesLib.AssetCook\",\"schema\":1,";
        s.append(Field("store_id", StoreId));
        s.push_back(',');
        s.append(Field("volume_guid", wi.Guid));
        s.push_back(',');
        s.append(Field("volume_serial", wi.Serial));
        s.push_back(',');
        s.append(Field("workspace_id", wi.Id));
        s.push_back(',');
        s.append(Field("store_directory_id", si.Id));
        s.push_back('}');
        return s;
    }
    Text Claim(const char* leaf, const Text& id, const Text& owner, size_t n = 1)
    {
        char cid[33];
        std::snprintf(cid, sizeof(cid), "%032llx", static_cast<unsigned long long>(n));
        Text s = "{";
        s.append(Field("claim_id", cid));
        s.push_back(',');
        s.append(Field("leaf", leaf));
        s.push_back(',');
        s.append(Field("directory_id", id));
        s.push_back(',');
        s.append(Field("owner_id", owner));
        s.push_back('}');
        return s;
    }
    Text Index(const Text& rows = "")
    {
        Text s = "{\"schema\":1,";
        s.append(Field("store_id", StoreId));
        s.append(",\"generation\":\"0000000000000001\",\"roots\":[");
        s.append(rows);
        s.append("]}");
        return s;
    }
    void Init(const std::filesystem::path& w)
    {
        CHECK(std::filesystem::create_directory(w / ".norves-assetcook"));
        Write(w / ".norves-assetcook" / "header.json", Header(w));
        Write(w / ".norves-assetcook" / "roots.json", Index());
    }
    Text Bind(const CookOwnerResolveRequest& r)
    {
        CookResolvedOwnerBinding o;
        Text e;
        CHECK(ResolveCookOwnerBinding(r, o, e));
        return o.ExpectedBinding.OwnerId;
    }
    Text Own(const CookOwnerResolveRequest& r)
    {
        CHECK(std::filesystem::create_directory(r.FinalRuntimeRoot));
        Text leaf;
        const auto filename = r.FinalRuntimeRoot.filename();
        for (auto c : filename.native())
        {
            CHECK(c >= 32 && c < 127);
            leaf.push_back(static_cast<char>(c));
        }
        auto row = Claim(leaf.c_str(), Identity(r.FinalRuntimeRoot).Id, Bind(r));
        Write(r.FinalRuntimeRoot.parent_path() / ".norves-assetcook" / "roots.json", Index(row));
        return row;
    }
    struct SnapshotEntry
    {
        std::filesystem::path Path;
        int64_t Stamp = 0;
        uint64_t Hash = 0, Size = 0, VolumeSerial = 0;
        NorvesLib::Core::Container::FixedArray<uint8_t, 16> FileId;
        bool bFile = false;
    };
    Array<SnapshotEntry> Snapshot(const std::filesystem::path& p)
    {
        Array<SnapshotEntry> out;
        for (const auto& e : std::filesystem::recursive_directory_iterator(p))
        {
            SnapshotEntry a;
            a.Path = e.path();
            // 列挙cacheのdirectory時刻は子の作成直後に古い場合がある。全entryをhandleで再観測する。
            struct SnapshotHandle
            {
                HANDLE Value = INVALID_HANDLE_VALUE;
                ~SnapshotHandle()
                {
                    if (Value != INVALID_HANDLE_VALUE)
                    {
                        CloseHandle(Value);
                    }
                }
            } h;
            h.Value = CreateFileW(e.path().c_str(), FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            CHECK(h.Value != INVALID_HANDLE_VALUE);
            FILE_ATTRIBUTE_TAG_INFO tag{};
            CHECK(GetFileType(h.Value) == FILE_TYPE_DISK);
            CHECK(GetFileInformationByHandleEx(h.Value, FileAttributeTagInfo, &tag, sizeof(tag)));
            CHECK((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0);
            a.bFile = (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
            if (a.bFile)
            {
                auto bytes = Read(e.path());
                a.Size = bytes.size();
                a.Hash = 14695981039346656037ull;
                for (unsigned char c : bytes)
                {
                    a.Hash = (a.Hash ^ c) * 1099511628211ull;
                }
            }
            FILE_BASIC_INFO basic{};
            FILE_ID_INFO identity{};
            CHECK(GetFileInformationByHandleEx(h.Value, FileBasicInfo, &basic, sizeof(basic)));
            CHECK(GetFileInformationByHandleEx(h.Value, FileIdInfo, &identity, sizeof(identity)));
            a.Stamp = basic.LastWriteTime.QuadPart;
            a.VolumeSerial = identity.VolumeSerialNumber;
            std::memcpy(a.FileId.data(), identity.FileId.Identifier, 16);
            const auto handle = h.Value;
            h.Value = INVALID_HANDLE_VALUE;
            CHECK(CloseHandle(handle));
            out.push_back(std::move(a));
        }
        std::sort(out.begin(), out.end(),
                  [](const auto& a, const auto& b)
                  {
                      return a.Path.native() < b.Path.native();
                  });
        return out;
    }
    void Unchanged(const Array<SnapshotEntry>& a, const Array<SnapshotEntry>& b, uint_least32_t callerLine)
    {
        CHECK(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i)
        {
            const bool bSame = a[i].Path == b[i].Path && a[i].Stamp == b[i].Stamp && a[i].Hash == b[i].Hash &&
                               a[i].Size == b[i].Size && a[i].bFile == b[i].bFile &&
                               a[i].VolumeSerial == b[i].VolumeSerial &&
                               std::memcmp(a[i].FileId.data(), b[i].FileId.data(), 16) == 0;
            if (!bSame)
            {
                Text before, after;
                CHECK(NorvesLib::Tools::AssetCook::Detail::EncodeCookPathUtf8(a[i].Path, before, false));
                CHECK(NorvesLib::Tools::AssetCook::Detail::EncodeCookPathUtf8(b[i].Path, after, false));
                std::fprintf(
                    stderr,
                    "store_snapshot_difference caller_line=%u entry=%zu before=%s after=%s file=%d/%d size=%llu/%llu hash=%016llx/%016llx stamp=%lld/%lld identity_equal=%d\n",
                    static_cast<unsigned>(callerLine), i, before.c_str(), after.c_str(), a[i].bFile, b[i].bFile,
                    static_cast<unsigned long long>(a[i].Size), static_cast<unsigned long long>(b[i].Size),
                    static_cast<unsigned long long>(a[i].Hash), static_cast<unsigned long long>(b[i].Hash),
                    static_cast<long long>(a[i].Stamp), static_cast<long long>(b[i].Stamp),
                    a[i].VolumeSerial == b[i].VolumeSerial &&
                        std::memcmp(a[i].FileId.data(), b[i].FileId.data(), 16) == 0);
            }
            CHECK(bSame);
        }
    }
    CookManagedStoreObservation Observe(const CookOwnerResolveRequest& r, Result expected,
                                        const std::filesystem::path& snapshotRoot = {},
                                        std::source_location location = std::source_location::current())
    {
        auto before = snapshotRoot.empty() ? Array<SnapshotEntry>{} : Snapshot(snapshotRoot);
        CookManagedStoreObservation out;
        out.CurrentRootClaimId = "held";
        Text error;
        auto result = ObserveCookManagedStore(r, out, error);
        if (result != expected)
        {
            std::fprintf(stderr, "expected %u got %u: %s\n", unsigned(expected), unsigned(result), error.c_str());
            CHECK(false);
        }
        if (result == Result::Observed || result == Result::StoreMissing)
        {
            CHECK(error.empty());
        }
        else
        {
            CHECK(!error.empty());
            CHECK(out.CurrentRootClaimId == "held" && out.Stores.empty());
        }
        if (!snapshotRoot.empty())
        {
            Unchanged(before, Snapshot(snapshotRoot), location.line());
        }
        return out;
    }
    std::filesystem::path Short(const std::filesystem::path& path)
    {
        wchar_t buf[32768]{};
        const auto n = GetShortPathNameW(path.c_str(), buf, 32768);
        CHECK(n && n < 32768);
        return std::filesystem::path(buf);
    }
    bool Ascii(const std::filesystem::path& path)
    {
        for (auto c : path.native())
        {
            if (c < 32 || c >= 127)
            {
                return false;
            }
        }
        return true;
    }
    struct DriveAlias
    {
        wchar_t Drive[3] = {0, L':', 0};
        std::filesystem::path Target;
        bool bMapped = false;
        explicit DriveAlias(const std::filesystem::path& target) : Target(target)
        {
            const DWORD used = GetLogicalDrives();
            if (used == 0)
            {
                return;
            }
            for (wchar_t c = L'Z'; c >= L'P'; --c)
            {
                if ((used & (1u << (c - L'A'))) != 0)
                {
                    continue;
                }
                Drive[0] = c;
                wchar_t prior[32768]{};
                if (QueryDosDeviceW(Drive, prior, 32768) != 0 || GetLastError() != ERROR_FILE_NOT_FOUND)
                {
                    continue;
                }
                if (DefineDosDeviceW(DDD_NO_BROADCAST_SYSTEM, Drive, Target.c_str()))
                {
                    bMapped = true;
                    break;
                }
            }
        }
        ~DriveAlias()
        {
            if (bMapped &&
                !DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE | DDD_NO_BROADCAST_SYSTEM, Drive,
                                  Target.c_str()))
            {
                bAliasCleanupFailed = true;
                std::fprintf(stderr, "DOS mapping cleanup failed: %lu\n", GetLastError());
            }
        }
        std::filesystem::path Root() const
        {
            wchar_t path[] = {Drive[0], L':', L'\\', 0};
            return std::filesystem::path(path);
        }
    };
    CookManagedStoreObservation Run()
    {
        Fixture f;
        auto w = f.Workspace("Workspace Long Name");
        auto r = f.Request(w);
        auto missing = Observe(r, Result::StoreMissing, f.Root);
        CHECK(!missing.bRuntimeRootClaimed && missing.Stores.empty());
        Init(w);
        auto empty = Observe(r, Result::Observed, f.Root);
        CHECK(empty.Stores.size() == 1 && !empty.bRuntimeRootClaimed);
        auto row = Own(r);
        auto own = Observe(r, Result::Observed, f.Root);
        CHECK(own.bRuntimeRootClaimed && own.Stores[0].Roots.size() == 1);
        CHECK(own.CurrentRootClaimId == "00000000000000000000000000000001");
        auto sibling = f.Request(w, "Sibling");
        Observe(sibling, Result::Observed, f.Root);
        const auto metadata = w / ".norves-assetcook";
        const auto header = Read(metadata / "header.json");
        const auto index = Read(metadata / "roots.json");
        auto changed = r;
        changed.ManifestName = "other.json";
        Observe(changed, Result::Conflict, f.Root);
        changed = r;
        changed.SpecPath = f.Root / "other.json";
        Write(changed.SpecPath, "other");
        Observe(changed, Result::Conflict, f.Root);
        auto nested = f.Request(r.FinalRuntimeRoot, "Nested");
        Observe(nested, Result::Conflict, f.Root);
        auto deeper = r.FinalRuntimeRoot / "sub";
        CHECK(std::filesystem::create_directory(deeper));
        Observe(f.Request(deeper), Result::Conflict, f.Root);
        // 同内容のspecや新ownerでも固定pendingを回避できない。index欠落中も回復必須。
        Write(metadata / "pending", "incomplete");
        std::filesystem::remove(metadata / "roots.json");
        Observe(r, Result::NeedsRecovery, f.Root);
        Observe(changed, Result::NeedsRecovery, f.Root);
        Observe(nested, Result::NeedsRecovery, f.Root);
        auto disjoint = f.Workspace("Disjoint");
        Observe(f.Request(disjoint), Result::StoreMissing, f.Root);
        CHECK(std::filesystem::remove(metadata / "pending"));
        Write(metadata / "roots.json", index);
        // 既存unknown rootを後からancestorとして採用しない。
        auto reverse = f.Workspace("Reverse");
        auto child = reverse / "Already Existing";
        CHECK(std::filesystem::create_directory(child));
        Init(child);
        Observe(f.Request(reverse, "Already Existing"), Result::Conflict, f.Root);
        // unrelated claimの欠落/置換もstore全体を停止する。
        const auto held = w / "Old Root Held";
        std::filesystem::rename(r.FinalRuntimeRoot, held);
        Observe(sibling, Result::Conflict, f.Root);
        CHECK(std::filesystem::create_directory(r.FinalRuntimeRoot));
        CHECK(Identity(r.FinalRuntimeRoot).Id != Identity(held).Id);
        Observe(sibling, Result::Conflict, f.Root);
        CHECK(std::filesystem::remove(r.FinalRuntimeRoot));
        std::filesystem::rename(held, r.FinalRuntimeRoot);
        auto bad = row;
        bad.append(",");
        bad.append(row);
        Write(metadata / "roots.json", Index(bad));
        Observe(r, Result::Conflict, f.Root);
        Write(metadata / "roots.json", Index(Claim("../outside", Identity(r.FinalRuntimeRoot).Id, Bind(r))));
        Observe(r, Result::Conflict, f.Root);
        Write(metadata / "roots.json", Index(Claim("C:/outside", Identity(r.FinalRuntimeRoot).Id, Bind(r))));
        Observe(r, Result::Conflict, f.Root);
        Write(metadata / "roots.json", index);
        // 保存locatorをschema外fieldとして拒否し、その先に作ったtrapは触らない。
        Text trap = header;
        trap.pop_back();
        trap.append(",\"workspace_path\":\"C:/never-open\"}");
        Write(metadata / "header.json", trap);
        Observe(r, Result::Conflict, f.Root);
        Write(metadata / "header.json", header);
        // 空/未知store、case違い固定名、型違いを採用しない。
        auto unknown = f.Workspace("Unknown");
        CHECK(std::filesystem::create_directory(unknown / ".norves-assetcook"));
        Observe(f.Request(unknown), Result::Conflict, f.Root);
        auto wrong = f.Workspace("Wrong Type");
        Write(wrong / ".norves-assetcook", "file");
        Observe(f.Request(wrong), Result::Conflict, f.Root);
        auto upper = f.Workspace("Uppercase");
        CHECK(std::filesystem::create_directory(upper / ".NORVES-ASSETCOOK"));
        Observe(f.Request(upper), Result::Conflict, f.Root);
        std::filesystem::rename(metadata / "header.json", metadata / "HEADER.JSON");
        Observe(r, Result::Conflict, f.Root);
        std::filesystem::rename(metadata / "HEADER.JSON", metadata / "header.json");
        Write(metadata / "PENDING", "x");
        Observe(r, Result::Conflict, f.Root);
        CHECK(std::filesystem::remove(metadata / "PENDING"));
        // 他storeのheaderをcopyしてもworkspace/store IDが違うため拒否。
        auto copy = f.Workspace("Copied");
        Init(copy);
        Write(copy / ".norves-assetcook" / "header.json", header);
        Observe(f.Request(copy), Result::Conflict, f.Root);
        // control領域の別名や子孫をruntimeとして使わない。
        Observe(f.Request(w, ".NORVES-ASSETCOOK"), Result::Conflict, f.Root);
        CHECK(std::filesystem::create_directory(metadata / "sub"));
        Observe(f.Request(metadata / "sub"), Result::Conflict, f.Root);
        auto alias = r;
        alias.FinalRuntimeRoot = Short(r.FinalRuntimeRoot);
        const bool bShort = alias.FinalRuntimeRoot != r.FinalRuntimeRoot;
        CHECK(Observe(alias, Result::Observed, f.Root).Owner.ExpectedBinding.OwnerId == Bind(r));
        alias.FinalRuntimeRoot = Short(metadata);
        Observe(alias, Result::Conflict, f.Root);
        std::printf("store_short_alias_distinct=%d\n", bShort);
        // SUBSTのdrive rootでも物理workspaceが通常directoryなら同じstoreへ届く。
        bool bSubst = false;
        {
            DriveAlias mapping(w);
            if (mapping.bMapped)
            {
                alias = r;
                alias.FinalRuntimeRoot = mapping.Root() / r.FinalRuntimeRoot.filename();
                auto o = Observe(alias, Result::Observed, f.Root);
                CHECK(o.Owner.ExpectedBinding.OwnerId == Bind(r));
                alias.FinalRuntimeRoot = mapping.Root() / r.FinalRuntimeRoot.filename() / "sub" / "nested";
                Observe(alias, Result::Conflict, f.Root);
                bSubst = true;
            }
        }
        CHECK(!bAliasCleanupFailed);
        std::printf("store_subst_checked=%d\n", bSubst);
        auto unicode = w / std::filesystem::path(u8"資料\U0001f43a");
        CHECK(std::filesystem::create_directory(unicode));
        Init(unicode);
        bool bUnicode = false;
        auto shortUnicode = Short(unicode);
        if (Ascii(shortUnicode))
        {
            Observe(f.Request(shortUnicode), Result::Observed, f.Root);
            bUnicode = true;
        }
        std::printf("store_unicode_ascii_alias=%d\n", bUnicode);
        // 実volume直下workspaceは管理領域を暗黙作成しない。
        alias = r;
        alias.FinalRuntimeRoot = w.root_path() / "NorvesObservationNeverCreate";
        Observe(alias, Result::Conflict);
        // control file共有拒否。open済みhandleは例外経路でも閉じる。
        HANDLE locked =
            CreateFileW((metadata / "header.json").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(locked != INVALID_HANDLE_VALUE);
        try
        {
            Observe(r, Result::Error);
        }
        catch (...)
        {
            CloseHandle(locked);
            throw;
        }
        CHECK(CloseHandle(locked));
        Write(metadata / "header.json", Text(16385, ' '));
        Observe(r, Result::Error, f.Root);
        Write(metadata / "header.json", header);
        CHECK(CreateHardLinkW((metadata / "header-alias.json").c_str(), (metadata / "header.json").c_str(), nullptr));
        Observe(r, Result::Error, f.Root);
        CHECK(std::filesystem::remove(metadata / "header-alias.json"));
        Write(metadata / "roots.json", Text(1024 * 1024 + 1, ' '));
        Observe(r, Result::Error, f.Root);
        Write(metadata / "roots.json", index);
        // case-sensitive directoryでは2つの実長名を検出する。
        auto caseDir = f.Workspace("Case Sensitive");
        HANDLE ch = CreateFileW(caseDir.c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        CHECK(ch != INVALID_HANDLE_VALUE);
        FILE_CASE_SENSITIVE_INFO info{};
        info.Flags = FILE_CS_FLAG_CASE_SENSITIVE_DIR;
        const bool bCase = SetFileInformationByHandle(ch, FileCaseSensitiveInfo, &info, sizeof(info)) != 0;
        CHECK(CloseHandle(ch));
        if (bCase)
        {
            Init(caseDir);
            CHECK(std::filesystem::create_directory(caseDir / ".NORVES-ASSETCOOK"));
            Observe(f.Request(caseDir), Result::Conflict, f.Root);
        }
        std::printf("store_case_sensitive_checked=%d\n", bCase);
        // reparseはlink先を採用せず拒否。fixtureの専用linkだけを削除する。
        auto linked = f.Workspace("Linked");
        const auto link = linked / ".norves-assetcook";
        const bool bLink =
            CreateSymbolicLinkW(link.c_str(), metadata.c_str(),
                                SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
        if (bLink)
        {
            Observe(f.Request(linked), Result::Conflict);
            CHECK(std::filesystem::remove(link));
        }
        std::printf("store_reparse_checked=%d\n", bLink);
        // 4096実directoryのclaimを照合し、4097件を型/重複検査より前に拒否する。
        auto large = f.Workspace("Large");
        Init(large);
        Text rows;
        for (size_t i = 0; i < MaximumCookStoreRoots; ++i)
        {
            char name[32];
            std::snprintf(name, sizeof(name), "Root%04zu", i);
            auto p = large / name;
            CHECK(std::filesystem::create_directory(p));
            if (i)
            {
                rows.push_back(',');
            }
            rows.append(Claim(name, Identity(p).Id, OwnerId, i + 1));
        }
        Write(large / ".norves-assetcook" / "roots.json", Index(rows));
        CHECK(Observe(f.Request(large), Result::Observed).Stores[0].Roots.size() == MaximumCookStoreRoots);
        rows.push_back(',');
        rows.append(Claim("Extra", OwnerId, OwnerId, MaximumCookStoreRoots + 1));
        Write(large / ".norves-assetcook" / "roots.json", Index(rows));
        Observe(f.Request(large), Result::Conflict);
        // callback内からの再入を拒否し、保持したoutは変更しない。
        Text e;
        const auto nestedCall = [](const CookDestinationLockContext&, void* context, Text& error)
        {
            auto* request = static_cast<CookOwnerResolveRequest*>(context);
            CookManagedStoreObservation out;
            out.CurrentRootClaimId = "held";
            CHECK(ObserveCookManagedStore(*request, out, error) == Result::Error);
            CHECK(out.CurrentRootClaimId == "held");
            error.clear();
            return true;
        };
        CHECK(WithCookDestinationLock({r.FinalRuntimeRoot}, nestedCall, &r, e) == CookDestinationLockResult::Executed);
        Observe(r, Result::Observed, f.Root);
        // 観測値はfixture破棄後にも残る独立所有。
        auto retained = Observe(r, Result::Observed);
        CHECK(retained.Stores[0].Roots[0].RootLeaf == "Runtime Long Name");
        std::puts(
            "COOK_MANAGED_STORE result=pass physical_ancestry_fixed_control_pending_claims_read_only_no_adoption");
        return retained;
    }
#endif
} // namespace StoreObservationTest
int main()
{
#if !defined(_WIN32)
    return 125;
#else
    try
    {
        const auto retained = StoreObservationTest::Run();
        CHECK(retained.bRuntimeRootClaimed && retained.Stores[0].Roots[0].RootLeaf == "Runtime Long Name");
        return StoreObservationTest::bAliasCleanupFailed ? 1 : 0;
    }
    catch (...)
    {
        return 1;
    }
#endif
}
