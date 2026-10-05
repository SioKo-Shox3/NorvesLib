// destination volumeの協調排他。永続fileや権限設定を作らず、公開処理は接続しない。
#include "CookDestinationLock.h"
#include "CookDestinationLockTestAccess.h"
#include "CookPathIdentity.h"
#include "CookOutputPaths.h"
#include "CookOwnerBinding.h"
#include "NativeCookPath.h"
#include "Container/FixedArray.h"
#include "Container/VariableArray.h"
#include <exception>
#include <cstring>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#endif
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using LockText = Core::Container::AnsiString;
        using Fault = Detail::CookLockFault;
        using Result = CookDestinationLockResult;
        thread_local bool bInsideLock = false;
        bool Fail(LockText& error, const char* reason)
        {
            error = "cook_destination_lock: ";
            error.append(reason);
            return false;
        }
        struct Invocation
        {
            Invocation()
            {
                bInsideLock = true;
            }
            ~Invocation()
            {
                bInsideLock = false;
            }
        };
#if defined(_WIN32)
        constexpr size_t VolumeRootUnits = 49;
        bool Guid(const wchar_t* text, size_t size, LockText& out)
        {
            constexpr wchar_t prefix[] = L"\\\\?\\Volume{";
            constexpr size_t begin = sizeof(prefix) / sizeof(wchar_t) - 1;
            if (size != VolumeRootUnits || std::memcmp(text, prefix, begin * sizeof(wchar_t)) != 0 ||
                text[47] != L'}' || text[48] != L'\\')
            {
                return false;
            }
            LockText candidate;
            for (size_t i = 0; i < 36; ++i)
            {
                wchar_t c = text[begin + i];
                if (i == 8 || i == 13 || i == 18 || i == 23)
                {
                    if (c != L'-')
                    {
                        return false;
                    }
                }
                else
                {
                    if (c >= L'A' && c <= L'F')
                    {
                        c += L'a' - L'A';
                    }
                    if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f')))
                    {
                        return false;
                    }
                }
                candidate.push_back(static_cast<char>(c));
            }
            out = std::move(candidate);
            return true;
        }
        struct Observation
        {
            CookDestinationLockContext Context;
            LockText Name;
        };
        bool Observe(const CookDestinationLockRequest& request, Observation& out, LockText& error)
        {
            Observation candidate;
            if (!Detail::NormalizeCookGuardLocator(request.FinalRuntimeRoot, candidate.Context.FinalRuntimeRootLocator,
                                                   error))
            {
                return false;
            }
            LockText ascii;
            if (!Detail::CookOutputPaths::AsciiPath(candidate.Context.FinalRuntimeRootLocator, ascii))
            {
                return Fail(error, "ascii_runtime_locator_required");
            }
            Detail::CookPathIdentity identity;
            if (!Detail::ObserveCookDirectoryIdentity(candidate.Context.FinalRuntimeRootLocator, identity, error))
            {
                return false;
            }
            const auto& physical = identity.Canonical.native();
            LockText checkedGuid;
            if (physical.size() < VolumeRootUnits || !Guid(physical.data(), VolumeRootUnits, checkedGuid))
            {
                return Fail(error, "invalid_observed_volume_guid");
            }
            // volume control identityだけを解決する。canonical rootをasset/state I/Oへ戻さない。
            Core::Container::FixedArray<wchar_t, 50> volumeRoot, canonical;
            for (size_t i = 0; i < VolumeRootUnits; ++i)
            {
                volumeRoot[i] = physical[i];
            }
            volumeRoot[VolumeRootUnits] = 0;
            if (!GetVolumeNameForVolumeMountPointW(volumeRoot.data(), canonical.data(),
                                                   static_cast<DWORD>(canonical.size())))
            {
                return Fail(error, "canonical_volume_failed");
            }
            size_t length = 0;
            while (length < canonical.size() && canonical[length])
            {
                ++length;
            }
            if (!Guid(canonical.data(), length, candidate.Context.CanonicalVolumeGuid))
            {
                return Fail(error, "invalid_canonical_volume_guid");
            }
            if (!Detail::EncodeCookPathUtf8(identity.Canonical, candidate.Context.CanonicalFinalRuntimeRootIdentity,
                                            false) ||
                candidate.Context.CanonicalFinalRuntimeRootIdentity.size() > MaximumCookOwnerIdentityBytes)
            {
                return Fail(error, "canonical_root_limit_or_utf8");
            }
            candidate.Context.bFinalRuntimeRootPresent = identity.bPresent;
            candidate.Name = "Global\\NorvesLib.AssetCook.DestinationVolumeLock.v1.";
            candidate.Name.append(candidate.Context.CanonicalVolumeGuid);
            out = std::move(candidate);
            return true;
        }
        struct Mutex
        {
            HANDLE Handle = nullptr;
            bool bOwned = false;
            bool Cleanup(Fault fault) noexcept
            {
                bool bGood = true;
                if (bOwned)
                {
                    bOwned = false;
                    if (!ReleaseMutex(Handle))
                    {
                        bGood = false;
                    }
                    if (fault == Fault::Release)
                    {
                        bGood = false;
                    }
                }
                if (Handle)
                {
                    HANDLE h = Handle;
                    Handle = nullptr;
                    if (!CloseHandle(h))
                    {
                        bGood = false;
                    }
                    if (fault == Fault::Close)
                    {
                        bGood = false;
                    }
                }
                return bGood;
            }
            ~Mutex()
            {
                (void)Cleanup(Fault::None);
            }
        };
#endif
    } // namespace
    bool Detail::InspectCookDestinationMutexNameForTest(const CookDestinationLockRequest& request, LockText& out,
                                                        LockText& error)
    {
        if (&out == &error)
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
            Observation observation;
            if (!Observe(request, observation, error))
            {
                return false;
            }
            out = std::move(observation.Name);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "observation_exception");
        }
#endif
    }
    CookDestinationLockResult Detail::WithCookDestinationLockForTest(const CookDestinationLockRequest& request,
                                                                     CookDestinationLockedWork work, void* userData,
                                                                     LockText& error, Fault fault)
    {
        error.clear();
        if (!work || bInsideLock)
        {
            Fail(error, !work ? "null_callback" : "nested_invocation");
            return Result::Error;
        }
#if !defined(_WIN32)
        (void)request;
        (void)userData;
        (void)fault;
        Fail(error, "windows_required");
        return Result::Error;
#else
        Invocation invocation;
        Mutex mutex;
        Result result = Result::Error;
        try
        {
            Observation before;
            if (Observe(request, before, error))
            {
                Core::Container::VariableArray<wchar_t> name;
                for (const unsigned char c : before.Name)
                {
                    name.push_back(static_cast<wchar_t>(c));
                }
                name.push_back(0);
                if (fault != Fault::Create)
                {
                    mutex.Handle = CreateMutexExW(nullptr, name.data(), 0, SYNCHRONIZE | MUTEX_MODIFY_STATE);
                }
                if (!mutex.Handle)
                {
                    Fail(error, "mutex_create_failed");
                }
                else
                {
                    const DWORD wait = fault == Fault::Wait ? WAIT_FAILED : WaitForSingleObject(mutex.Handle, 0);
                    if (wait == WAIT_TIMEOUT)
                    {
                        result = Result::Busy;
                    }
                    else if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
                    {
                        Fail(error, "mutex_wait_failed");
                    }
                    else
                    {
                        mutex.bOwned = true;
                        Observation current;
                        if (fault == Fault::Reobserve)
                        {
                            Fail(error, "reobserve_injected");
                        }
                        else if (Observe(request, current, error))
                        {
                            if (fault == Fault::VolumeChanged ||
                                current.Context.CanonicalVolumeGuid != before.Context.CanonicalVolumeGuid)
                            {
                                Fail(error, "volume_changed");
                            }
                            else
                            {
                                current.Context.bAbandoned = wait == WAIT_ABANDONED;
                                if (work(current.Context, userData, error))
                                {
                                    result = Result::Executed;
                                    error.clear();
                                }
                                else if (error.empty())
                                {
                                    Fail(error, "callback_failed");
                                }
                            }
                        }
                    }
                }
            }
        }
        catch (const std::exception&)
        {
            Fail(error, "callback_or_observation_exception");
        }
        catch (...)
        {
            Fail(error, "nonstandard_callback_exception");
        }
        if (!mutex.Cleanup(fault))
        {
            if (error.empty())
            {
                Fail(error, "mutex_cleanup_failed");
            }
            else
            {
                error.append("; mutex_cleanup_failed");
            }
            return Result::Error;
        }
        return result;
#endif
    }
    CookDestinationLockResult WithCookDestinationLock(const CookDestinationLockRequest& request,
                                                      CookDestinationLockedWork work, void* userData, LockText& error)
    {
        return Detail::WithCookDestinationLockForTest(request, work, userData, error, Fault::None);
    }
} // namespace NorvesLib::Tools::AssetCook
