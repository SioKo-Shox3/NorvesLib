#include "Animal.h"
#include <iostream>

namespace NorvesLib::Test
{
    // Animalクラスのリフレクション実装 - 自動登録機能により手動登録が不要に
    IMPLEMENT_CLASS(Animal, Core::Object)

    // コンストラクタ
    Animal::Animal()
    {
        Name = "未設定";
        Age = 0;
        Weight = 0.0f;
        IsHealthy = true;
    }

    void Animal::Initialize()
    {
        Core::Object::Initialize(); // 親クラスの初期化も呼び出す
        std::cout << "Animal::Initialize - " << Name << std::endl;
    }

    void Animal::MakeSound()
    {
        std::cout << Name << "が鳴いています" << std::endl;
    }

    float Animal::GetBMI()
    {
        if (Weight <= 0.0f) return 0.0f;
        return Weight / 2.0f; // 単純な計算式（実際のBMIとは異なる）
    }

    // Dogクラスのリフレクション実装 - 自動登録機能により手動登録が不要に
    IMPLEMENT_CLASS(Dog, Animal)

    // 犬クラスの実装
    Dog::Dog()
    {
        Name = "ワンちゃん";
        Age = 3;
        Weight = 15.0f;
        Breed = "雑種";
        IsGoodBoy = true;
    }

    void Dog::Initialize()
    {
        Animal::Initialize(); // 親クラスの初期化を呼び出す
        std::cout << "Dog::Initialize - " << Name << "(" << Breed << ")" << std::endl;
    }

    void Dog::MakeSound()
    {
        std::cout << Name << "(" << Breed << ")：ワンワン！" << std::endl;
    }
}