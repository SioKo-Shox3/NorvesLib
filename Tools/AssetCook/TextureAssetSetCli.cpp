#include "TextureAssetSetCook.h"
#include "NativeCookPath.h"
#include "CookBatchWorkers.h"
#include <charconv>
#include "Text/UnicodeText.h"
#include <cstdio>
#include <cstring>
#include <exception>
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        bool Option(const char* argument, const char* name)
        {
            const auto size = std::strlen(name);
            return std::strncmp(argument, name, size) == 0 && (argument[size] == 0 || argument[size] == '=');
        }
        void CleanupWarning(const CookManagedBootstrapOutcome& out)
        {
            if (!out.bCleanupIncomplete)
            {
                return;
            }
            Core::Container::AnsiString location;
            if (!Detail::EncodeCookPathUtf8(out.RetiredDirectory, location))
            {
                location = "unrepresentable_locator";
            }
            std::fprintf(stderr, "AssetCook warning: committed_or_rolled_back_state_preserved; cleanup_incomplete=%s\n",
                         location.c_str());
        }
    } // 名前空間
    bool RunTextureAssetSetCommand(int argc, const char* const* argv, int& exitCode)
    {
        bool bFound = false;
        for (int i = 1; i < argc; ++i)
        {
            bFound |= Option(argv[i], "--asset-set") || Option(argv[i], "--recover");
        }
        if (!bFound)
        {
            return false;
        }
        const auto reject = [&](const char* reason)
        {
            std::fprintf(stderr, "AssetCook error: texture_asset_set: %s\n", reason);
            exitCode = 1;
            return true;
        };
        TextureAssetSetCookRequest request;
        CookBatchReport report;
        request.Report = &report;
        bool bSeen[4] = {};
        bool bRecover = false;
        bool bJobsSeen = false, bVerifySeen = false;
        const char* names[] = {"--asset-set", "--source-root", "--runtime-root", "--manifest"};
        std::filesystem::path* paths[] = {&request.SpecPath, &request.SourceRoot, &request.RuntimeRoot,
                                          &request.ManifestPath};
        try
        {
            for (int i = 1; i < argc; ++i)
            {
                const char* arg = argv[i];
                if (std::strcmp(arg, "--force") == 0 || std::strcmp(arg, "--prune") == 0 ||
                    std::strcmp(arg, "--verify") == 0)
                {
                    bool& seen = std::strcmp(arg, "--force") == 0
                                     ? request.bForce
                                     : (std::strcmp(arg, "--prune") == 0 ? request.bPrune : bVerifySeen);
                    if (seen)
                    {
                        return reject("duplicate flag");
                    }
                    seen = true;
                    // 現行の所有cacheは常にpayload hashも検証する。--verifyで弱い経路へ切り替えない。
                    continue;
                }
                if (Option(arg, "--jobs"))
                {
                    if (bJobsSeen)
                    {
                        return reject("duplicate --jobs");
                    }
                    bJobsSeen = true;
                    const char* equals = std::strchr(arg, '=');
                    const char* value = equals ? equals + 1 : (i + 1 < argc ? argv[++i] : nullptr);
                    if (!value || !*value)
                    {
                        return reject("missing --jobs value");
                    }
                    const char* end = value + std::strlen(value);
                    const auto parsed = std::from_chars(value, end, request.Jobs);
                    if (parsed.ec != std::errc{} || parsed.ptr != end || request.Jobs == 0 ||
                        request.Jobs > Detail::MaximumCookBatchJobs)
                    {
                        return reject("--jobs must be between 1 and 64");
                    }
                    continue;
                }
                if (std::strcmp(arg, "--warn-budget") == 0)
                {
                    if (request.bWarnBudget)
                    {
                        return reject("duplicate --warn-budget");
                    }
                    request.bWarnBudget = true;
                    continue;
                }
                if (Option(arg, "--recover"))
                {
                    if (bRecover || std::strcmp(arg, "--recover"))
                    {
                        return reject("duplicate or valued --recover");
                    }
                    bRecover = true;
                    continue;
                }
                const char* equals = std::strchr(arg, '=');
                const size_t length = equals ? static_cast<size_t>(equals - arg) : std::strlen(arg);
                size_t option = 4;
                for (size_t n = 0; n < 4; ++n)
                {
                    if (length == std::strlen(names[n]) && std::memcmp(arg, names[n], length) == 0)
                    {
                        option = n;
                    }
                }
                if (option == 4)
                {
                    return reject("unknown or mixed option");
                }
                if (bSeen[option])
                {
                    return reject("duplicate option");
                }
                bSeen[option] = true;
                const char* value = equals ? equals + 1 : (i + 1 < argc ? argv[++i] : nullptr);
                if (!value || !*value || std::strncmp(value, "--", 2) == 0)
                {
                    return reject("missing option value");
                }
                if (!Core::TextDetail::ForEachUnicodeScalar<char>({value, std::strlen(value)},
                                                                  [](uint32_t)
                                                                  {
                                                                  }))
                {
                    return reject("path argument is not UTF-8");
                }
                *paths[option] = std::filesystem::u8path(value, value + std::strlen(value));
            }
            Core::Container::AnsiString error;
            CookManagedBootstrapOutcome out;
            if (bRecover)
            {
                if (!bSeen[2] || bSeen[0] || bSeen[1] || bSeen[3] || bJobsSeen || request.bWarnBudget ||
                    request.bForce || request.bPrune || bVerifySeen)
                {
                    return reject(
                        "--recover requires only --runtime-root; its parent workspace pending may belong to a sibling root");
                }
                std::filesystem::path runtime;
                if (!Detail::ResolveTextureCliPath(request.RuntimeRoot, std::filesystem::current_path(), runtime,
                                                   error))
                {
                    return reject(error.c_str());
                }
                // 復旧は明示された親workspaceの固定pending。source/spec読込や新cookへ進まない。
                const auto recovered = RecoverCookManagedPending(runtime, out, error);
                const char* status = nullptr;
                switch (recovered)
                {
                case CookManagedRecoveryResult::NoPending:
                    status = "no_pending";
                    break;
                case CookManagedRecoveryResult::RolledBack:
                    status = "rolled_back";
                    break;
                case CookManagedRecoveryResult::Committed:
                    status = "committed";
                    break;
                case CookManagedRecoveryResult::Busy:
                    return reject("workspace_busy; pending preserved");
                default:
                    return reject(error.c_str());
                }
                std::fprintf(stderr, "AssetCook workspace_recovery %s\n", status);
                if (recovered != CookManagedRecoveryResult::NoPending)
                {
                    CleanupWarning(out);
                }
                exitCode = 0;
                return true;
            }
            if (!bSeen[0] || !bSeen[2])
            {
                return reject("--asset-set and --runtime-root are required");
            }
            const auto cooked = CookTextureAssetSetWithOutcome(request, out, error);
            if (!report.ReportDirectory.empty())
            {
                Core::Container::AnsiString location;
                if (Detail::EncodeCookPathUtf8(report.ReportDirectory, location))
                {
                    std::fprintf(stderr, "cook_report_directory=%s\n", location.c_str());
                }
            }
            const char* status = nullptr;
            switch (cooked)
            {
            case TextureAssetSetCookResult::Created:
                status = "published";
                break;
            case TextureAssetSetCookResult::Updated:
                status = "updated";
                break;
            case TextureAssetSetCookResult::NoChange:
                status = "unchanged";
                break;
            case TextureAssetSetCookResult::NeedsRecovery:
                std::fprintf(
                    stderr,
                    "AssetCook: pending preserved; explicitly run --recover --runtime-root <directory>; recovery covers its parent workspace, including sibling pending\n");
                return reject(error.c_str());
            case TextureAssetSetCookResult::StorePublishedButError:
                std::fprintf(stderr, "AssetCook: published store preserved; no automatic retry\n");
                return reject(error.c_str());
            case TextureAssetSetCookResult::CommittedButError:
                std::fprintf(stderr, "AssetCook: committed state preserved; no automatic recook\n");
                return reject(error.c_str());
            case TextureAssetSetCookResult::BudgetExceeded:
                std::fprintf(stderr, "AssetCook error: budget_exceeded; see adjacent .reports run directory\n");
                exitCode = 2;
                return true;
            case TextureAssetSetCookResult::Busy:
                return reject("workspace_busy");
            default:
                return reject(error.c_str());
            }
            std::fprintf(stderr, "AssetCook texture_asset_set %s\n", status);
            CleanupWarning(out);
            exitCode = 0;
            return true;
        }
        catch (const std::exception& exception)
        {
            return reject(exception.what());
        }
    }
} // 名前空間 NorvesLib::Tools::AssetCook
