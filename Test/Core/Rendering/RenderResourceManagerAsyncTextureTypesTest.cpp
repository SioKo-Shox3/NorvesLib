#include "Rendering/TextureAsyncLoadQueue.h"
#include "Rendering/RenderResourceRegistry.h"
#include "Rendering/RenderResourceManager.h"
#include "Rendering/TextureUploadProfile.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core::Rendering;

static_assert(std::is_same_v<RenderResourceManager, RenderResourceRegistry>);
static_assert(std::is_same_v<RenderResourceRegistry::AsyncTextureResult, TextureAsyncResult>);
static_assert(std::is_same_v<RenderResourceRegistry::AsyncTextureRequest, TextureAsyncRequest>);
static_assert(std::is_same_v<RenderResourceManager::AsyncTextureResult, TextureAsyncResult>);
static_assert(std::is_same_v<RenderResourceManager::AsyncTextureRequest, TextureAsyncRequest>);
static_assert(std::is_same_v<TextureAsyncLoadQueue::RequestPtr,
                             NorvesLib::Core::Container::TSharedPtr<TextureAsyncRequest>>);

namespace
{
    bool EqualsRole(const char *role, const char *expected)
    {
        return std::string(role != nullptr ? role : "") == expected;
    }

    void TestTextureCreateUploadProfileRole()
    {
        SetTextureCreateUploadProfileRoleForCurrentThread(nullptr);
        assert(EqualsRole(GetTextureCreateUploadProfileRoleForCurrentThread(), "caller"));

        const char *previousRole = SetTextureCreateUploadProfileRoleForCurrentThread("worker");
        assert(EqualsRole(previousRole, "caller"));
        assert(EqualsRole(GetTextureCreateUploadProfileRoleForCurrentThread(), "worker"));

        {
            ScopedTextureCreateUploadProfileRole scopedRole("main_render");
            assert(EqualsRole(GetTextureCreateUploadProfileRoleForCurrentThread(), "main_render"));
        }
        assert(EqualsRole(GetTextureCreateUploadProfileRoleForCurrentThread(), "worker"));

        previousRole = SetTextureCreateUploadProfileRoleForCurrentThread("");
        assert(EqualsRole(previousRole, "worker"));
        assert(EqualsRole(GetTextureCreateUploadProfileRoleForCurrentThread(), "caller"));

        {
            ScopedTextureCreateUploadProfileRole scopedRole(nullptr);
            assert(EqualsRole(GetTextureCreateUploadProfileRoleForCurrentThread(), "caller"));
        }
        assert(EqualsRole(GetTextureCreateUploadProfileRoleForCurrentThread(), "caller"));
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "RenderResourceManagerAsyncTextureTypesTest start\n";

    TestTextureCreateUploadProfileRole();

    std::cout << "RenderResourceManagerAsyncTextureTypesTest passed\n";
    return 0;
}
