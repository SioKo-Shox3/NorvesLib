#pragma once

namespace NorvesLib::Core::Rendering
{
    const char *GetTextureCreateUploadProfileRoleForCurrentThread();
    const char *SetTextureCreateUploadProfileRoleForCurrentThread(const char *role);

    class ScopedTextureCreateUploadProfileRole final
    {
    public:
        explicit ScopedTextureCreateUploadProfileRole(const char *role);
        ~ScopedTextureCreateUploadProfileRole();

        ScopedTextureCreateUploadProfileRole(const ScopedTextureCreateUploadProfileRole &) = delete;
        ScopedTextureCreateUploadProfileRole &operator=(const ScopedTextureCreateUploadProfileRole &) = delete;

    private:
        const char *m_PreviousRole = "caller";
    };
}
