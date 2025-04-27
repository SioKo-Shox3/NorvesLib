#pragma once

#include "Object/IUnknown.h"
#include "Object/Object.h"
#include <string>

namespace NorvesLib::Test
{
    // Animal基本クラス
    class Animal : public Core::Object
    {
        REFLECTION_CLASS(Animal, Core::Object)

    public:
        Animal();
        virtual ~Animal() = default;

        // プロパティ定義 (自動登録機能付き)
        PROPERTY(std::string, Name)
        PROPERTY(int, Age)
        PROPERTY(float, Weight)
        PROPERTY(bool, IsHealthy)

        // 関数定義 (自動登録機能付き)
        FUNCTION(void, MakeSound)
        FUNCTION(float, GetBMI)

        void Initialize() override;
    };

    // 犬のサブクラス
    class Dog : public Animal
    {
        REFLECTION_CLASS(Dog, Animal)  // REFLECT_CLASS_PARENTからREFLECTION_CLASSに変更
        using __ThisClass = Dog;  // __ThisClassの定義を追加（リフレクションに必要）

    public:
        Dog();
        virtual ~Dog() = default;

        // 追加のプロパティ (自動登録機能付き)
        PROPERTY(std::string, Breed)
        PROPERTY(bool, IsGoodBoy)

        // オーバーライド関数 (自動登録機能付き)
        FUNCTION(void, MakeSound)

        void Initialize() override;
    };
}