// 保存stateとは独立に、実file/rootの比較identityを読み取り専用で導出する。
#include "CookOwnerResolver.h"
#include "CookPathIdentity.h"
#include "CookOutputPaths.h"
#include "NativeCookPath.h"
#include <exception>
#include <utility>
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using ResolveText = Core::Container::AnsiString;
        bool Fail(ResolveText& error, const char* code)
        {
            error = "cook_owner_resolver: ";
            error.append(code);
            return false;
        }
        bool ErrorAlias(const CookOwnerResolveRequest& request, const CookResolvedOwnerBinding& out,
                        const ResolveText& error)
        {
            return &error == &request.ManifestName || &error == &out.Identity.CanonicalSpecLocator ||
                   &error == &out.Identity.CanonicalFinalRuntimeRootIdentity || &error == &out.Identity.ManifestName ||
                   &error == &out.ExpectedBinding.OwnerId || &error == &out.ExpectedBinding.RuntimeRootIdentity ||
                   &error == &out.ExpectedBinding.ManifestName;
        }
    } // namespace
    bool ResolveCookOwnerBinding(const CookOwnerResolveRequest& request, CookResolvedOwnerBinding& out,
                                 ResolveText& error)
    {
        if (ErrorAlias(request, out, error))
        {
            return false;
        }
        error.clear();
#if !defined(_WIN32)
        (void)request;
        (void)out;
        return Fail(error, "windows_required");
#else
        try
        {
            CookResolvedOwnerBinding candidate;
            if (!Detail::NormalizeCookGuardLocator(request.SpecPath, candidate.SpecLocator, error) ||
                !Detail::NormalizeCookGuardLocator(request.FinalRuntimeRoot, candidate.FinalRuntimeRootLocator, error))
            {
                return false;
            }
            if (!Detail::CookOutputPaths::AsciiPath(candidate.FinalRuntimeRootLocator,
                                                    candidate.ExpectedBinding.RuntimeRootIdentity))
            {
                return Fail(error, "ascii_runtime_locator_required");
            }
            Detail::CookPathIdentity spec, root;
            if (!Detail::ObserveCookPathIdentity(candidate.SpecLocator, spec, error))
            {
                return false;
            }
            if (!spec.bPresent)
            {
                return Fail(error, "existing_spec_required");
            }
            if (!Detail::ObserveCookDirectoryIdentity(candidate.FinalRuntimeRootLocator, root, error))
            {
                return false;
            }
            // Native encoderを共有する。GUID付きcanonical pathを実際のI/Oへ逆流させない。
            if (!Detail::EncodeCookPathUtf8(spec.Canonical, candidate.Identity.CanonicalSpecLocator, false) ||
                !Detail::EncodeCookPathUtf8(root.Canonical, candidate.Identity.CanonicalFinalRuntimeRootIdentity,
                                            false))
            {
                return Fail(error, "canonical_utf8_failed");
            }
            candidate.Identity.ManifestName = request.ManifestName;
            candidate.ExpectedBinding.ManifestName = request.ManifestName;
            candidate.bFinalRuntimeRootPresent = root.bPresent;
            if (!ComputeCookOwnerId(candidate.Identity, candidate.ExpectedBinding.OwnerId, error))
            {
                return false;
            }
            if (!IsValidCookStateBinding(candidate.ExpectedBinding))
            {
                return Fail(error, "state_binding_profile_mismatch");
            }
            out = std::move(candidate);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "resolution_exception");
        }
#endif
    }
} // namespace NorvesLib::Tools::AssetCook
