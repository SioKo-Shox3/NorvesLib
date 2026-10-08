// 実Windows path観測とstateの独立bindingを検証する。作成/清掃はtest所有fixtureだけ。
#include "Tools/AssetCook/CookOwnerResolver.h"
#include "Tools/AssetCook/CookPathIdentity.h"
#include "Tools/AssetCook/CookStateFile.h"
#include "Tools/AssetCook/CookDependencySnapshot.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <exception>
#if defined(_WIN32)
#include <Windows.h>
#endif
namespace OwnerResolveTest
{
    struct Failure
    {
    };
    bool bAliasCleanupFailed = false;
} // namespace OwnerResolveTest
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                       \
            throw OwnerResolveTest::Failure{};                                                                         \
        }                                                                                                              \
    } while (false)
namespace OwnerResolveTest
{
    using namespace NorvesLib::Tools::AssetCook;
    namespace RAPI = NorvesLib::Tools::AssetCook::Detail;
    using ResolveText = NorvesLib::Core::Container::AnsiString;
    void Write(const std::filesystem::path& p, const char* value)
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        CHECK(f);
        f.write(value, static_cast<std::streamsize>(std::strlen(value)));
        f.close();
        CHECK(!f.fail());
    }
    ResolveText Read(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        CHECK(f);
        ResolveText value;
        char c;
        while (f.get(c))
        {
            value.push_back(c);
        }
        CHECK(f.eof());
        return value;
    }
    CookResolvedOwnerBinding Bind(const CookOwnerResolveRequest& r)
    {
        CookResolvedOwnerBinding out;
        ResolveText error;
        if (!ResolveCookOwnerBinding(r, out, error))
        {
            std::fprintf(stderr, "resolve: %s\n", error.c_str());
            CHECK(false);
        }
        CHECK(error.empty());
        return out;
    }
    bool Same(const CookResolvedOwnerBinding& a, const CookResolvedOwnerBinding& b)
    {
        return a.SpecLocator == b.SpecLocator && a.FinalRuntimeRootLocator == b.FinalRuntimeRootLocator &&
               a.Identity.CanonicalSpecLocator == b.Identity.CanonicalSpecLocator &&
               a.Identity.CanonicalFinalRuntimeRootIdentity == b.Identity.CanonicalFinalRuntimeRootIdentity &&
               a.Identity.ManifestName == b.Identity.ManifestName &&
               a.ExpectedBinding.OwnerId == b.ExpectedBinding.OwnerId &&
               a.ExpectedBinding.RuntimeRootIdentity == b.ExpectedBinding.RuntimeRootIdentity &&
               a.ExpectedBinding.ManifestName == b.ExpectedBinding.ManifestName &&
               a.bFinalRuntimeRootPresent == b.bFinalRuntimeRootPresent;
    }
    void Bad(const CookOwnerResolveRequest& request, const CookResolvedOwnerBinding& held)
    {
        auto out = held;
        ResolveText error;
        CHECK(!ResolveCookOwnerBinding(request, out, error));
        CHECK(!error.empty() && Same(out, held));
    }
    size_t Count(const std::filesystem::path& root)
    {
        size_t n = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        {
            (void)entry;
            ++n;
        }
        return n;
    }
#if defined(_WIN32)
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
#endif
} // namespace OwnerResolveTest
int main()
{
#if !defined(_WIN32)
    return 125;
#else
    try
    {
        using namespace OwnerResolveTest;
        char name[100];
        std::snprintf(name, sizeof(name), "norves-owner-resolver-%lu-%llu", GetCurrentProcessId(),
                      static_cast<unsigned long long>(GetTickCount64()));
        const auto root = std::filesystem::temp_directory_path() / name;
        CHECK(std::filesystem::create_directory(root));
        const auto spec = root / "Long Asset Spec.json";
        Write(spec, "spec-one");
        CookOwnerResolveRequest request{spec, root / "Long Runtime Directory", "manifest.json"};
        const auto count = Count(root);
        const auto stamp = std::filesystem::last_write_time(spec);
        const auto absent = Bind(request);
        CHECK(!absent.bFinalRuntimeRootPresent && Count(root) == count &&
              !std::filesystem::exists(request.FinalRuntimeRoot));
        CHECK(std::filesystem::last_write_time(spec) == stamp && Read(spec) == "spec-one");
        CHECK(std::filesystem::create_directory(request.FinalRuntimeRoot));
        const auto existing = Bind(request);
        CHECK(existing.bFinalRuntimeRootPresent && existing.ExpectedBinding.OwnerId == absent.ExpectedBinding.OwnerId);
        CHECK(existing.Identity.CanonicalFinalRuntimeRootIdentity == absent.Identity.CanonicalFinalRuntimeRootIdentity);
        CHECK(existing.ExpectedBinding.RuntimeRootIdentity == absent.ExpectedBinding.RuntimeRootIdentity &&
              Same(Bind(request), existing));
        auto changed = request;
        changed.ManifestName = "other.json";
        CHECK(Bind(changed).ExpectedBinding.OwnerId != existing.ExpectedBinding.OwnerId);
        changed = request;
        changed.FinalRuntimeRoot = root / "other";
        CHECK(Bind(changed).ExpectedBinding.OwnerId != existing.ExpectedBinding.OwnerId);
        changed = request;
        changed.SpecPath = root / "copy.json";
        Write(changed.SpecPath, "spec-one");
        CHECK(Bind(changed).ExpectedBinding.OwnerId != existing.ExpectedBinding.OwnerId);
        // 同じ名前へのatomic replacementはfile IDが変わってもownerを維持する。
        RAPI::CookPathIdentity beforeId, afterId;
        ResolveText error;
        CHECK(RAPI::ObserveCookPathIdentity(spec, beforeId, error));
        Write(root / "replace.json", "spec-two");
        CHECK(MoveFileExW((root / "replace.json").c_str(), spec.c_str(), MOVEFILE_REPLACE_EXISTING));
        CHECK(RAPI::ObserveCookPathIdentity(spec, afterId, error));
        CHECK(RAPI::CompareCookFileIdentity(beforeId, afterId) != 0);
        CHECK(Bind(request).ExpectedBinding.OwnerId == existing.ExpectedBinding.OwnerId);
        changed = request;
        changed.SpecPath = root / "hard-spec.json";
        std::filesystem::create_hard_link(spec, changed.SpecPath);
        CHECK(Bind(changed).ExpectedBinding.OwnerId != existing.ExpectedBinding.OwnerId);
        // 日本語/非BMP specをnativeのまま読む。
        changed = request;
        changed.SpecPath = root / std::filesystem::path(u8"犬\U0001f43a.json");
        Write(changed.SpecPath, "unicode");
        const auto unicode = Bind(changed);
        CHECK(unicode.ExpectedBinding.OwnerId != existing.ExpectedBinding.OwnerId);
        auto unicodeRoot = request;
        unicodeRoot.FinalRuntimeRoot = root / std::filesystem::path(u8"犬\U0001f43a");
        Bad(unicodeRoot, existing);
        // 独立再導出したbindingで新規stateを保存/読み戻す。保存ownerを期待値へ採用しない。
        CookOwnedState state;
        state.Binding = existing.ExpectedBinding;
        CookStateFileRequest io{root / "state.json", existing.FinalRuntimeRootLocator, existing.ExpectedBinding};
        CHECK(WriteNewCookOwnedState(io, state, error));
        const auto fresh = Bind(request);
        io.ExpectedBinding = fresh.ExpectedBinding;
        CookOwnedState loaded;
        CHECK(LoadCookOwnedState(io, loaded, error) == CookStateLoadResult::Loaded);
        CHECK(loaded.Binding.OwnerId == existing.ExpectedBinding.OwnerId);
        changed = request;
        changed.SpecPath = Short(spec);
        changed.FinalRuntimeRoot = Short(request.FinalRuntimeRoot);
        const auto aliases = Bind(changed);
        CHECK(aliases.ExpectedBinding.OwnerId == existing.ExpectedBinding.OwnerId);
        const bool bShortDistinct = aliases.FinalRuntimeRootLocator != existing.FinalRuntimeRootLocator;
        if (bShortDistinct)
        {
            io.RuntimeRoot = aliases.FinalRuntimeRootLocator;
            io.ExpectedBinding = aliases.ExpectedBinding;
            CHECK(LoadCookOwnedState(io, loaded, error) == CookStateLoadResult::Error);
        }
        std::printf("owner_short_alias_distinct=%d\n", bShortDistinct);
        changed = request;
        changed.FinalRuntimeRoot = Short(root) / "New Alias Root";
        const auto aliasAbsent = Bind(changed);
        CHECK(!aliasAbsent.bFinalRuntimeRootPresent);
        CHECK(std::filesystem::create_directory(changed.FinalRuntimeRoot));
        CHECK(Bind(changed).ExpectedBinding.OwnerId == aliasAbsent.ExpectedBinding.OwnerId);
        // ASCII caller aliasでUnicode物理親を解決する場合。
        const auto unicodeParent = root / std::filesystem::path(u8"資料\U0001f43a");
        CHECK(std::filesystem::create_directory(unicodeParent));
        const auto shortUnicode = Short(unicodeParent);
        bool bUnicodeAlias = false;
        if (Ascii(shortUnicode))
        {
            changed = request;
            changed.FinalRuntimeRoot = shortUnicode / "child";
            const auto observed = Bind(changed);
            CHECK(std::filesystem::create_directory(changed.FinalRuntimeRoot));
            CHECK(Bind(changed).ExpectedBinding.OwnerId == observed.ExpectedBinding.OwnerId);
            bUnicodeAlias = true;
        }
        std::printf("owner_unicode_ascii_alias=%d\n", bUnicodeAlias);
        bool bSubst = false;
        {
            DriveAlias mapping(root);
            if (mapping.bMapped)
            {
                changed = request;
                changed.SpecPath = mapping.Root() / spec.filename();
                changed.FinalRuntimeRoot = mapping.Root() / request.FinalRuntimeRoot.filename();
                CookResolvedOwnerBinding mapped;
                if (ResolveCookOwnerBinding(changed, mapped, error))
                {
                    CHECK(mapped.ExpectedBinding.OwnerId == existing.ExpectedBinding.OwnerId);
                    io.RuntimeRoot = mapped.FinalRuntimeRootLocator;
                    io.ExpectedBinding = mapped.ExpectedBinding;
                    CHECK(LoadCookOwnedState(io, loaded, error) == CookStateLoadResult::Error);
                    changed.FinalRuntimeRoot = mapping.Root() / "New Subst Root";
                    const auto provisional = Bind(changed);
                    CHECK(!provisional.bFinalRuntimeRootPresent);
                    CHECK(std::filesystem::create_directory(changed.FinalRuntimeRoot));
                    CHECK(Bind(changed).ExpectedBinding.OwnerId == provisional.ExpectedBinding.OwnerId);
                    bSubst = true;
                }
                else
                {
                    CHECK(std::strstr(error.c_str(), "final_name_failed"));
                }
            }
        }
        CHECK(!bAliasCleanupFailed);
        bool bUnwound = false;
        wchar_t unwoundDrive[3]{};
        try
        {
            DriveAlias probe(root);
            if (probe.bMapped)
            {
                for (size_t i = 0; i < 3; ++i)
                {
                    unwoundDrive[i] = probe.Drive[i];
                }
                throw Failure{};
            }
        }
        catch (const Failure&)
        {
            bUnwound = true;
        }
        CHECK(!bAliasCleanupFailed);
        if (bUnwound)
        {
            wchar_t target[32768]{};
            CHECK(QueryDosDeviceW(unwoundDrive, target, 32768) == 0 && GetLastError() == ERROR_FILE_NOT_FOUND);
        }
        std::printf("owner_subst_unwind_checked=%d\n", bUnwound);
        std::printf("owner_subst_checked=%d\n", bSubst);
        bool bCaseSensitive = false;
        const auto caseDir = root / "case-sensitive";
        CHECK(std::filesystem::create_directory(caseDir));
        HANDLE directory = CreateFileW(caseDir.c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        CHECK(directory != INVALID_HANDLE_VALUE);
        FILE_CASE_SENSITIVE_INFO info{};
        info.Flags = FILE_CS_FLAG_CASE_SENSITIVE_DIR;
        const bool bEnabled = SetFileInformationByHandle(directory, FileCaseSensitiveInfo, &info, sizeof(info)) != 0;
        CHECK(CloseHandle(directory));
        if (bEnabled)
        {
            Write(caseDir / "Spec.json", "upper");
            Write(caseDir / "spec.json", "lower");
            CHECK(std::filesystem::create_directory(caseDir / "Root"));
            CHECK(std::filesystem::create_directory(caseDir / "root"));
            changed = request;
            changed.SpecPath = caseDir / "Spec.json";
            changed.FinalRuntimeRoot = caseDir / "Root";
            const auto upper = Bind(changed);
            changed.SpecPath = caseDir / "spec.json";
            CHECK(Bind(changed).ExpectedBinding.OwnerId != upper.ExpectedBinding.OwnerId);
            changed.SpecPath = caseDir / "Spec.json";
            changed.FinalRuntimeRoot = caseDir / "root";
            CHECK(Bind(changed).ExpectedBinding.OwnerId != upper.ExpectedBinding.OwnerId);
            changed.FinalRuntimeRoot = caseDir / "Absent";
            const auto upperAbsent = Bind(changed);
            changed.FinalRuntimeRoot = caseDir / "absent";
            CHECK(Bind(changed).ExpectedBinding.OwnerId != upperAbsent.ExpectedBinding.OwnerId);
            bCaseSensitive = true;
        }
        std::printf("owner_case_sensitive_checked=%d\n", bCaseSensitive);
        // input baseの変更はownerの入力ではなく、既存依存authorityの要求意味に含まれる。
        Write(root / "source-a.bin", "same");
        Write(root / "source-b.bin", "same");
        SingleAssetCookRequest cook;
        cook.InputPath = root / "source-a.bin";
        cook.Kind = "raw";
        cook.LogicalPath = "Raw/test";
        cook.EntryName = "__raw__";
        cook.EntryTypeText = "Raw";
        cook.Format = "raw.v0";
        cook.Variant = "default";
        CookDependencySnapshot sourceA, sourceB;
        CHECK(CaptureCookDependencySnapshot(cook, 1, sourceA, error));
        cook.InputPath = root / "source-b.bin";
        CHECK(CaptureCookDependencySnapshot(cook, 1, sourceB, error));
        CHECK(sourceA.Fingerprint != sourceB.Fingerprint &&
              Bind(request).ExpectedBinding.OwnerId == existing.ExpectedBinding.OwnerId);
        changed = request;
        changed.SpecPath = root / "missing.json";
        Bad(changed, existing);
        changed.SpecPath = root;
        Bad(changed, existing);
        changed = request;
        changed.FinalRuntimeRoot = spec;
        Bad(changed, existing);
        changed.FinalRuntimeRoot = root / "missing/child";
        Bad(changed, existing);
        changed.FinalRuntimeRoot = root.root_path();
        Bad(changed, existing);
        changed.FinalRuntimeRoot = "relative";
        Bad(changed, existing);
        for (const char* leaf : {"a.", "a ", "a:stream", "CON.json", "a/../b", "a/./b", "a//b"})
        {
            changed = request;
            changed.FinalRuntimeRoot = std::filesystem::path(root.native() + std::filesystem::path("/").native() +
                                                             std::filesystem::path(leaf).native());
            Bad(changed, existing);
        }
        changed = request;
        changed.ManifestName = "../manifest.json";
        Bad(changed, existing);
        changed = request;
        changed.ManifestName = ResolveText(MaximumCookOwnerIdentityBytes + 1, 'x');
        Bad(changed, existing);
        NorvesLib::Core::Container::VariableArray<wchar_t> tooLong(32768, L'x');
        tooLong[0] = L'C';
        tooLong[1] = L':';
        tooLong[2] = L'\\';
        changed = request;
        changed.SpecPath = std::filesystem::path(tooLong.data(), tooLong.data() + tooLong.size());
        Bad(changed, existing);
        const wchar_t nul[] = {L'C', L':', L'\\', L'x', 0, L'y'};
        changed = request;
        changed.SpecPath = std::filesystem::path(nul, nul + 6);
        Bad(changed, existing);
        const wchar_t invalid[] = {L'C', L':', L'\\', static_cast<wchar_t>(0xd800)};
        changed.SpecPath = std::filesystem::path(invalid, invalid + 4);
        Bad(changed, existing);
        HANDLE locked =
            CreateFileW(spec.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(locked != INVALID_HANDLE_VALUE);
        // FILE_READ_ATTRIBUTESはshare flagで必ず遮断されるとは限らない。成功時も公開権限は与えない。
        auto lockedOut = existing;
        const bool bLockedObserved = ResolveCookOwnerBinding(request, lockedOut, error);
        if (bLockedObserved)
        {
            CHECK(Same(lockedOut, existing));
        }
        else
        {
            CHECK(Same(lockedOut, existing) && !error.empty());
        }
        std::printf("owner_exclusive_spec_identity_observed=%d\n", bLockedObserved);
        CHECK(CloseHandle(locked));
        const auto junction = root / "junction";
        char command[4096];
        std::snprintf(command, sizeof(command), "cmd /c mklink /J \"%s\" \"%s\" >nul", junction.string().c_str(),
                      request.FinalRuntimeRoot.string().c_str());
        CHECK(std::system(command) == 0);
        changed = request;
        changed.FinalRuntimeRoot = junction;
        Bad(changed, existing);
        CHECK(std::filesystem::remove(junction));
        auto held = existing;
        CHECK(!ResolveCookOwnerBinding(request, held, held.Identity.ManifestName));
        CHECK(Same(held, existing));
        changed = request;
        CHECK(!ResolveCookOwnerBinding(changed, held, changed.ManifestName));
        CHECK(changed.ManifestName == request.ManifestName && Same(held, existing));
        CHECK(Read(spec) == "spec-two" && std::filesystem::is_directory(request.FinalRuntimeRoot));
        std::filesystem::remove_all(root);
        std::puts(
            "COOK_OWNER_RESOLVER result=pass physical_identity_lexical_binding_read_only_provisional_recheck_aliases_hold");
        return 0;
    }
    catch (const OwnerResolveTest::Failure&)
    {
        return 1;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "fixture exception: %s\n", e.what());
        return 1;
    }
#endif
}
