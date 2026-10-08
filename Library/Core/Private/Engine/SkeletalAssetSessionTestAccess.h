#pragma once
// Begin準備の境界注入。設定変更はlifecycleとの排他を試験側で保つ。
#include "Engine/SkeletalAssetSession.h"
namespace NorvesLib::Core::Engine::Detail
{
    struct SkeletalSessionTestAccess
    {
        static void SetPrepareHook(SkeletalAssetSession& session, void (*hook)(void*), void* context)
        {
            session.m_PrepareHook = hook;
            session.m_pPrepareHookContext = context;
        }
    };
} // namespace NorvesLib::Core::Engine::Detail
