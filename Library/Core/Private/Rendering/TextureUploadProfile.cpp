#include "Rendering/TextureUploadProfile.h"

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        const char *NormalizeTextureCreateUploadProfileRole(const char *role)
        {
            return (role != nullptr && role[0] != '\0') ? role : "caller";
        }

        thread_local const char *g_TextureCreateUploadProfileRole = "caller";
    }

    const char *GetTextureCreateUploadProfileRoleForCurrentThread()
    {
        return g_TextureCreateUploadProfileRole;
    }

    const char *SetTextureCreateUploadProfileRoleForCurrentThread(const char *role)
    {
        const char *previousRole = g_TextureCreateUploadProfileRole;
        g_TextureCreateUploadProfileRole = NormalizeTextureCreateUploadProfileRole(role);
        return previousRole;
    }

    ScopedTextureCreateUploadProfileRole::ScopedTextureCreateUploadProfileRole(const char *role)
        : m_PreviousRole(SetTextureCreateUploadProfileRoleForCurrentThread(role))
    {
    }

    ScopedTextureCreateUploadProfileRole::~ScopedTextureCreateUploadProfileRole()
    {
        SetTextureCreateUploadProfileRoleForCurrentThread(m_PreviousRole);
    }
}
