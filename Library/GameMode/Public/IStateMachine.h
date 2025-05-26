#pragma once

#include "Core/Object/IUnknown.h"
#include "Core/Public/Container/PointerTypes.h"

namespace NorvesLib::GameMode
{
    // 前方宣言
    class IFactory;

    // ステートマシンインターフェース
    class IStateMachine : public Core::IUnknown
    {
    public:
        // 仮想デストラクタ
        virtual ~IStateMachine() = default;

        // 次のステートを予約
        template<typename T>
        void ReserveState(Core::Container::TUniquePtr<T> nextState);

        // ファクトリを取得
        virtual void* GetFactoryImpl() const = 0;
        
        // 最新のデルタタイムを取得
        virtual float GetDeltaTime() const = 0;
        
        // ファクトリを型安全に取得するためのテンプレートメソッド
        template<typename FactoryType>
        FactoryType& GetFactory() const
        {
            return *static_cast<FactoryType*>(GetFactoryImpl());
        }
    };

} // namespace NorvesLib::GameMode