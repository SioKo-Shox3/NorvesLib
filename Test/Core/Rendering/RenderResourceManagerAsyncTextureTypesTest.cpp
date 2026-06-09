#include "Rendering/TextureAsyncLoadQueue.h"
#include "Rendering/TextureAsyncTypes.h"
#include "Rendering/TextureUploadProfile.h"

#include <cassert>
#include <cstdint>
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

static_assert(std::is_same_v<decltype(TextureAsyncRequest::RequestId), uint32_t>);
static_assert(std::is_same_v<decltype(TextureAsyncResult::Source), TextureLoadSource>);
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
