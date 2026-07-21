#pragma once

#include "Scripting/ScriptRuntime.h"

#include <angelscript.h>

namespace NorvesLib::Core::Scripting
{
    using ScriptEngineFactory = asIScriptEngine* (*)();
    using ScriptEngineValidator = bool (*)(asIScriptEngine*);

    asIScriptEngine* CreateDefaultScriptEngine();
    asIScriptEngine* GetActiveAngelScriptEngine();

    class AngelScriptEngineOwner final
    {
    public:
        AngelScriptEngineOwner();
        ~AngelScriptEngineOwner();
        AngelScriptEngineOwner(const AngelScriptEngineOwner&) = delete;
        AngelScriptEngineOwner& operator=(const AngelScriptEngineOwner&) = delete;

        bool Initialize(
            ScriptEngineFactory factory = &CreateDefaultScriptEngine,
            ScriptEngineValidator validator = nullptr);
        bool Shutdown();
        bool IsInitialized() const;
        bool OwnsGlobalAllocator() const;
        asIScriptEngine* GetEngine() const;
        const ScriptRuntimeDiagnostics& GetDiagnostics() const;
        void SetLastResult(EScriptRuntimeResult result);

    private:
        asIScriptEngine* m_Engine = nullptr;
        ScriptRuntimeDiagnostics m_Diagnostics;
        bool m_bOwnsAllocator = false;
        bool m_bShutdownPending = false;
    };
} // namespace NorvesLib::Core::Scripting
