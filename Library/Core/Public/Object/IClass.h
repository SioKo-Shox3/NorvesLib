#pragma once

#include <memory>
#include "IUnknown.h"
#include "Container/Containers.h" // Core::Containerのすべてのコンテナ
#include "Text/IdentityPool.h"

namespace NorvesLib::Core
{
    // 前方宣言
    class IValue;
    class IClass;

    /**
     * @brief クラスのプロパティを表現する基本クラス
     */
    class ClassProperty
    {
    public:
        ClassProperty(const Identity& name, const IClass* type, size_t offset, size_t size, uint32_t flags = 0)
            : m_Name(name)
            , m_Type(type)
            , m_Offset(offset)
            , m_Size(size)
            , m_Flags(flags)
        {
        }

        virtual ~ClassProperty() = default;

        /**
         * @brief 変数名を取得します
         * @return 変数名
         */
        const Identity& GetName() const { return m_Name; }

        /**
         * @brief 変数の型を取得します
         * @return 型情報
         */
        const IClass* GetType() const { return m_Type; }

        /**
         * @brief 変数へのオフセットを取得します
         * @return VariableContainer内のオフセット
         */
        size_t GetOffset() const { return m_Offset; }

        /**
         * @brief メンバのサイズを取得します
         * @return メンバのバイトサイズ
         */
        size_t GetSize() const { return m_Size; }

        /**
         * @brief フラグを取得します
         * @return フラグ値
         */
        uint32_t GetFlags() const { return m_Flags; }

        /**
         * @brief 変数の値を取得します
         * @param instance オブジェクトインスタンス
         * @return 変数の値
         */
        std::unique_ptr<IValue> GetValue(const IUnknown* instance) const;

        /**
         * @brief 変数の値を設定します
         * @param instance オブジェクトインスタンス
         * @param value 設定する値
         * @return 成功した場合はtrue
         */
        bool SetValue(IUnknown* instance, const IValue* value) const;

    protected:
        Identity m_Name;       // 変数名
        const IClass* m_Type;     // 型情報
        size_t m_Offset;          // VariableContainer内のオフセット
        size_t m_Size;            // サイズ（バイト単位）
        uint32_t m_Flags;         // フラグ
    };

    /**
     * @brief 型付きのプロパティクラス
     * @tparam T プロパティの型
     */
    template<typename T>
    class TClassProperty : public ClassProperty
    {
    public:
        TClassProperty(const Identity& name, const IClass* type, size_t offset, size_t size, uint32_t flags = 0)
            : ClassProperty(name, type, offset, size, flags)
        {
        }

        /**
         * @brief プロパティ値への参照を取得
         * @param instance オブジェクトインスタンス
         * @return プロパティ値への参照
         */
        T& GetRef(IUnknown* instance) const
        {
            void* data = instance->GetVariableContainer()->GetAt(m_Offset);
            return *static_cast<T*>(data);
        }

        /**
         * @brief プロパティ値へのconst参照を取得
         * @param instance オブジェクトインスタンス
         * @return プロパティ値へのconst参照
         */
        const T& GetRef(const IUnknown* instance) const
        {
            const void* data = instance->GetVariableContainer()->GetAt(m_Offset);
            return *static_cast<const T*>(data);
        }

        /**
         * @brief プロパティ値を設定
         * @param instance オブジェクトインスタンス
         * @param value 設定する値
         */
        void SetValue(IUnknown* instance, const T& value) const
        {
            T& ref = GetRef(instance);
            ref = value;
        }
    };

    /**
     * @brief クラスのプロパティ参照クラス
     * ClassPropertyへのアクセスを簡略化する
     * @tparam T プロパティの型
     */
    template<typename T>
    class PropertyRef
    {
    public:
        PropertyRef(IUnknown* instance, const TClassProperty<T>* property)
            : m_Instance(instance)
            , m_Property(property)
        {
        }

        // 暗黙的キャスト演算子でプロパティ値を直接取得可能に
        operator T() const
        {
            return m_Property->GetRef(m_Instance);
        }

        // 代入演算子でプロパティ値を設定可能に
        PropertyRef& operator=(const T& value)
        {
            m_Property->SetValue(m_Instance, value);
            return *this;
        }

        // 加算代入演算子
        PropertyRef& operator+=(const T& value)
        {
            T current = m_Property->GetRef(m_Instance);
            current += value;
            m_Property->SetValue(m_Instance, current);
            return *this;
        }

        // 減算代入演子
        PropertyRef& operator-=(const T& value)
        {
            T current = m_Property->GetRef(m_Instance);
            current -= value;
            m_Property->SetValue(m_Instance, current);
            return *this;
        }

        // 直接参照を取得するポインタ演算子
        T* operator->()
        {
            return &(m_Property->GetRef(m_Instance));
        }

        // 直接参照を取得する間接参照演算子
        T& operator*()
        {
            return m_Property->GetRef(m_Instance);
        }

    private:
        IUnknown* m_Instance;
        const TClassProperty<T>* m_Property;
    };

    /**
     * @brief クラスのプロパティ参照クラス（constバージョン）
     * @tparam T プロパティの型
     */
    template<typename T>
    class ConstPropertyRef
    {
    public:
        ConstPropertyRef(const IUnknown* instance, const TClassProperty<T>* property)
            : m_Instance(instance)
            , m_Property(property)
        {
        }

        // 暗黙的キャスト演算子でプロパティ値を直接取得可能に
        operator T() const
        {
            return m_Property->GetRef(m_Instance);
        }

        // 直接参照を取得するポインタ演算子
        const T* operator->() const
        {
            return &(m_Property->GetRef(m_Instance));
        }

        // 直接参照を取得する間接参照演算子
        const T& operator*() const
        {
            return m_Property->GetRef(m_Instance);
        }

    private:
        const IUnknown* m_Instance;
        const TClassProperty<T>* m_Property;
    };

    /**
     * @brief クラスの関数を表現するクラス
     */
    class ClassFunction
    {
    public:
        ClassFunction(const Identity& name, const IClass* returnType, uint32_t flags = 0)
            : m_Name(name)
            , m_ReturnType(returnType)
            , m_Flags(flags)
        {
        }

        virtual ~ClassFunction() = default;

        /**
         * @brief 関数名を取得します
         * @return 関数名
         */
        const Identity& GetName() const { return m_Name; }

        /**
         * @brief 返り値の型を取得します
         * @return 型情報
         */
        const IClass* GetReturnType() const { return m_ReturnType; }

        /**
         * @brief パラメータを追加します
         * @param type パラメータの型
         * @param name パラメータの名前
         */
        void AddParameter(const IClass* type, const Identity& name)
        {
            m_ParameterTypes.push_back(type);
            m_ParameterNames.push_back(name);
        }

        /**
         * @brief 引数の型を取得します
         * @return 引数の型情報配列
         */
        const Container::VariableArray<const IClass*>& GetParameterTypes() const { return m_ParameterTypes; }

        /**
         * @brief 引数の名前を取得します
         * @return 引数の名前配列
         */
        const Container::VariableArray<Identity>& GetParameterNames() const { return m_ParameterNames; }

        /**
         * @brief フラグを取得します
         * @return フラグ値
         */
        uint32_t GetFlags() const { return m_Flags; }

        /**
         * @brief 関数を呼び出します
         * @param instance オブジェクトインスタンス
         * @param parameters 関数パラメータ
         * @return 戻り値、void型の場合はnullptr
         */
        virtual std::unique_ptr<IValue> Invoke(IUnknown* instance, const Container::VariableArray<IValue*>& parameters) const = 0;

    private:
        Identity m_Name;                         // 関数名
        const IClass* m_ReturnType;                 // 戻り値の型
        Container::VariableArray<const IClass*> m_ParameterTypes;  // パラメータの型リスト
        Container::VariableArray<Identity> m_ParameterNames;    // パラメータの名前リスト
        uint32_t m_Flags;                           // フラグ
    };

    /**
     * @brief フィールドクラス（基底）
     * クラスメンバー情報を格納するコンテナの基底クラス
     */
    class Field
    {
    public:
        Field() = default;
        virtual ~Field() = default;
    };

    /**
     * @brief プロパティフィールド
     * クラスのすべてのメンバ変数情報を格納するコンテナ
     */
    class PropertyField : public Field
    {
    public:
        PropertyField() = default;
        virtual ~PropertyField() = default;

        /**
         * @brief プロパティを追加します
         * @param property 追加するプロパティ
         */
        void AddProperty(std::shared_ptr<ClassProperty> property)
        {
            if (property) {
                m_Properties[property->GetName()] = std::move(property);
            }
        }

        /**
         * @brief プロパティを取得します
         * @param name プロパティ名
         * @return プロパティ、見つからない場合はnullptr
         */
        const ClassProperty* GetProperty(const Identity& name) const
        {
            auto it = m_Properties.find(name);
            if (it != m_Properties.end()) {
                return it->second.get();
            }
            return nullptr;
        }

        /**
         * @brief すべてのプロパティを取得します
         * @return プロパティのベクター
         */
        Container::VariableArray<const ClassProperty*> GetAllProperties() const
        {
            Container::VariableArray<const ClassProperty*> result;
            result.reserve(m_Properties.size());
            for (const auto& pair : m_Properties) {
                result.push_back(pair.second.get());
            }
            return result;
        }

        /**
         * @brief プロパティの総サイズを取得します
         * @return 合計バイトサイズ
         */
        size_t GetTotalSize() const
        {
            size_t totalSize = 0;
            for (const auto& pair : m_Properties) {
                const auto& prop = pair.second;
                totalSize = std::max(totalSize, prop->GetOffset() + prop->GetSize());
            }
            return totalSize;
        }

    private:
        // プロパティ名からプロパティへのマップ
        Container::UnorderedMap<Identity, std::shared_ptr<ClassProperty>, Identity::Hasher> m_Properties;
    };

    /**
     * @brief 関数フィールド
     * クラスのすべてのメンバ関数情報を格納するコンテナ
     */
    class FunctionField : public Field
    {
    public:
        FunctionField() = default;
        virtual ~FunctionField() = default;

        /**
         * @brief 関数を追加します
         * @param function 追加する関数
         */
        void AddFunction(std::shared_ptr<ClassFunction> function)
        {
            if (function) {
                m_Functions[function->GetName()] = std::move(function);
            }
        }

        /**
         * @brief 関数を取得します
         * @param name 関数名
         * @return 関数、見つからない場合はnullptr
         */
        const ClassFunction* GetFunction(const Identity& name) const
        {
            auto it = m_Functions.find(name);
            if (it != m_Functions.end()) {
                return it->second.get();
            }
            return nullptr;
        }

        /**
         * @brief すべての関数を取得します
         * @return 関数のベクター
         */
        Container::VariableArray<const ClassFunction*> GetAllFunctions() const
        {
            Container::VariableArray<const ClassFunction*> result;
            result.reserve(m_Functions.size());
            for (const auto& pair : m_Functions) {
                result.push_back(pair.second.get());
            }
            return result;
        }

    private:
        // 関数名から関数へのマップ
        Container::UnorderedMap<Identity, std::shared_ptr<ClassFunction>, Identity::Hasher> m_Functions;
    };

    /**
     * @brief クラス情報を定義するインターフェース
     * クラスの継承関係、メンバ変数、メンバ関数の情報およびデフォルトインスタンスを管理します
     */
    class IClass
    {
    public:
        virtual ~IClass() = default;

        /**
         * @brief クラス名を取得します
         * @return クラス名
         */
        virtual const Identity& GetClassName() const = 0;

        /**
         * @brief 親クラス情報を取得します
         * @return 親クラス情報へのポインタ、親がない場合はnullptr
         */
        virtual const IClass* GetParentClass() const = 0;

        /**
         * @brief このクラスが指定されたクラスを継承しているか確認します
         * @param cls 確認するクラス
         * @return 継承している場合はtrue
         */
        virtual bool IsChildOf(const IClass* cls) const = 0;

        /**
         * @brief デフォルトオブジェクトを取得します
         * @return デフォルトオブジェクトへのポインタ
         */
        virtual const IUnknown* GetDefaultObject() const = 0;

        /**
         * @brief 新しいインスタンスを作成します
         * @return 新しいインスタンスへのポインタ
         */
        virtual IUnknown* CreateInstance() const = 0;

        /**
         * @brief プロパティフィールドを取得します
         * @return プロパティフィールドへのポインタ
         */
        virtual const PropertyField* GetPropertyField() const = 0;

        /**
         * @brief 関数フィールドを取得します
         * @return 関数フィールドへのポインタ
         */
        virtual const FunctionField* GetFunctionField() const = 0;

        /**
         * @brief プロパティを取得します
         * @param name プロパティ名
         * @return プロパティへのポインタ、見つからない場合はnullptr
         */
        virtual const ClassProperty* GetProperty(const Identity& name) const = 0;

        /**
         * @brief すべてのプロパティを取得します
         * @return プロパティの配列
         */
        virtual Container::VariableArray<const ClassProperty*> GetAllProperties() const = 0;

        /**
         * @brief 関数を取得します
         * @param name 関数名
         * @return 関数へのポインタ、見つからない場合はnullptr
         */
        virtual const ClassFunction* GetFunction(const Identity& name) const = 0;

        /**
         * @brief すべての関数を取得します
         * @return 関数の配列
         */
        virtual Container::VariableArray<const ClassFunction*> GetAllFunctions() const = 0;

        /**
         * @brief クラスIDを取得します
         * @return クラス固有のID
         */
        virtual uint64_t GetClassId() const = 0;

        /**
         * @brief VariableContainerのサイズを取得します
         * @return バイト単位のサイズ
         */
        virtual size_t GetVariableContainerSize() const = 0;

        /**
         * @brief VariableContainerの初期化を行います
         * @param container 初期化するメモリ領域
         */
        virtual void InitializeVariableContainer(void* container) const = 0;
    };

    /**
     * @brief クラスレジストリ
     * システム内のすべてのクラス情報を管理します
     */
    class ClassRegistry
    {
    public:
        /**
         * @brief インスタンスを取得します
         * @return クラスレジストリのシングルトンインスタンス
         */
        static ClassRegistry& Get();

        /**
         * @brief クラス情報を登録します
         * @param cls 登録するクラス情報
         */
        void RegisterClass(const IClass* cls);

        /**
         * @brief クラス情報を名前で検索します
         * @param className クラス名
         * @return クラス情報へのポインタ、見つからない場合はnullptr
         */
        const IClass* FindClass(const Identity& className) const;

        /**
         * @brief クラス情報をIDで検索します
         * @param classId クラスID
         * @return クラス情報へのポインタ、見つからない場合はnullptr
         */
        const IClass* FindClass(uint64_t classId) const;

        /**
         * @brief 登録されているすべてのクラスを取得します
         * @return クラス情報の配列
         */
        Container::VariableArray<const IClass*> GetAllClasses() const;

    private:
        ClassRegistry() = default;
        ~ClassRegistry() = default;

        // クラス名からクラス情報へのマップ
        Container::UnorderedMap<Identity, const IClass*, Identity::Hasher> m_ClassesByName;
        
        // クラスIDからクラス情報へのマップ
        Container::UnorderedMap<uint64_t, const IClass*> m_ClassesById;
    };

} // namespace NorvesLib::Core