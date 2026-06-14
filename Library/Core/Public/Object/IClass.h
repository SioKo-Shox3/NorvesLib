#pragma once

#include <algorithm>
#include <cstring>
#include <memory>
#include <ostream>
#include <type_traits>
#include <utility>
#include "IUnknown.h"
#include "RuntimeSchema.h"
#include "Object/PropertyBag.h"
#include "Container/Containers.h" // Core::Containerのすべてのコンテナ
#include "Text/IdentityPool.h"

namespace NorvesLib::Core
{
    // 前方宣言
    class IClass;
    class FieldInitializer;

    /**
     * @brief クラスのプロパティを表現する基本クラス
     */
    class ClassProperty
    {
    public:
        ClassProperty(const Identity &name, const IClass *type, size_t offset, size_t size, uint32_t flags = 0)
            : m_Name(name), m_Type(type), m_Offset(offset), m_Size(size), m_Flags(flags)
        {
        }

        virtual ~ClassProperty() = default;

        /**
         * @brief 変数名を取得します
         * @return 変数名
         */
        const Identity &GetName() const { return m_Name; }

        /**
         * @brief 変数の型を取得します
         * @return 型情報
         */
        const IClass *GetType() const { return m_Type; }

        /**
         * @brief 変数へのオフセットを取得します
         * @return オブジェクトメンバ内のオフセット
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
         * @brief Runtime schemaで使う型IDを取得します。
         * 旧ClassPropertyでは型情報を持たない場合があるため、既定値は無効IDです。
         */
        virtual TypeId GetRuntimeTypeId() const { return InvalidSchemaId; }

        /**
         * @brief Runtime schema値をプロパティへ適用します。
         */
        virtual bool ApplyValue(IUnknown *instance, const PropertyValue &value) const
        {
            (void)instance;
            (void)value;
            return false;
        }

        /**
         * @brief プロパティの初期値を適用します
         * @param instance オブジェクトインスタンス
         * @param initializer 初期値を提供する初期化子
         * @return 初期値が適用された場合はtrue
         */
        virtual bool ApplyInitialValue(IUnknown *instance, const FieldInitializer *initializer) const = 0;

        /**
         * @brief デフォルト値を適用します
         * @param instance オブジェクトインスタンス
         */
        virtual void ApplyDefaultValue(IUnknown *instance) const = 0;

        /**
         * @brief プロパティデータの読み取り専用ポインタを取得します
         * @param instance オブジェクトインスタンス
         * @return プロパティデータへのポインタ
         */
        virtual const void *GetValuePtr(const IUnknown *instance) const
        {
            (void)instance;
            return nullptr;
        }

        /**
         * @brief プロパティデータの書き込み可能ポインタを取得します
         * @param instance オブジェクトインスタンス
         * @return プロパティデータへのポインタ
         */
        virtual void *GetValuePtr(IUnknown *instance) const
        {
            (void)instance;
            return nullptr;
        }

        /**
         * @brief プロパティ値を別のインスタンスからコピーします
         * @param destInstance コピー先インスタンス
         * @param srcInstance コピー元インスタンス
         * @return コピーが成功した場合はtrue
         */
        virtual bool CopyValueFrom(IUnknown *destInstance, const IUnknown *srcInstance) const
        {
            if (!destInstance || !srcInstance)
                return false;

            const void *src = GetValuePtr(srcInstance);
            void *dest = GetValuePtr(destInstance);

            if (!src || !dest)
                return false;

            std::memcpy(dest, src, m_Size);
            return true;
        }

        virtual void CaptureDefaultValue(const IUnknown *instance)
        {
            (void)instance;
        }

    protected:
        Identity m_Name;      // 変数名
        const IClass *m_Type; // 型情報
        size_t m_Offset;      // オブジェクトメンバ内のオフセット
        size_t m_Size;        // サイズ（バイト単位）
        uint32_t m_Flags;     // フラグ
    };

    /**
     * @brief Object生成後に適用するProperty override set
     *
     * 名前キーはC++側の簡易生成API向け、StablePropertyIdキーはschema/editor連携向けです。
     * 値のcopy/destroy/serialize/AddReferencesはPropertyValue/PropertyBagが担います。
     */
    class FieldInitializer
    {
    public:
        FieldInitializer() = default;
        virtual ~FieldInitializer() = default;

        /**
         * @brief プロパティの初期値を設定します
         * @param propertyName プロパティ名
         * @param value 初期値
         */
        template <typename T>
        void SetInitialValue(const Identity &propertyName, const T &value)
        {
            SetValue(propertyName, PropertyValue::Create<T>(value));
        }

        template <typename T>
        void SetInitialValue(StablePropertyId propertyId, const T &value)
        {
            SetValue(propertyId, PropertyValue::Create<T>(value));
        }

        void SetValue(const Identity &propertyName, PropertyValue value)
        {
            if (propertyName.IsValid())
            {
                m_ValuesByName[propertyName] = std::move(value);
            }
        }

        void SetValue(StablePropertyId propertyId, PropertyValue value)
        {
            m_ValuesByStableId.SetValue(propertyId, std::move(value));
        }

        /**
         * @brief 指定されたプロパティに初期値が設定されているか確認します
         * @param propertyName プロパティ名
         * @return 初期値が設定されている場合はtrue
         */
        bool HasInitialValue(const Identity &propertyName) const
        {
            return m_ValuesByName.find(propertyName) != m_ValuesByName.end();
        }

        bool HasInitialValue(StablePropertyId propertyId) const
        {
            return m_ValuesByStableId.Has(propertyId);
        }

        /**
         * @brief プロパティの初期値を取得します
         * @param propertyName プロパティ名
         * @return 初期値のポインタ、見つからない場合はnullptr
         */
        template <typename T>
        const T *GetInitialValue(const Identity &propertyName) const
        {
            const PropertyValue *value = FindValue(propertyName);
            return value ? value->Get<T>() : nullptr;
        }

        template <typename T>
        const T *GetInitialValue(StablePropertyId propertyId) const
        {
            const PropertyValue *value = FindValue(propertyId);
            return value ? value->Get<T>() : nullptr;
        }

        PropertyValue *FindValue(const Identity &propertyName)
        {
            auto it = m_ValuesByName.find(propertyName);
            return it != m_ValuesByName.end() ? &it->second : nullptr;
        }

        const PropertyValue *FindValue(const Identity &propertyName) const
        {
            auto it = m_ValuesByName.find(propertyName);
            return it != m_ValuesByName.end() ? &it->second : nullptr;
        }

        PropertyValue *FindValue(StablePropertyId propertyId)
        {
            return m_ValuesByStableId.Find(propertyId);
        }

        const PropertyValue *FindValue(StablePropertyId propertyId) const
        {
            return m_ValuesByStableId.Find(propertyId);
        }

        PropertyBag &GetPropertyBag()
        {
            return m_ValuesByStableId;
        }

        const PropertyBag &GetPropertyBag() const
        {
            return m_ValuesByStableId;
        }

        size_t GetValueCount() const
        {
            return m_ValuesByName.size() + m_ValuesByStableId.GetValueCount();
        }

        void Clear()
        {
            m_ValuesByName.clear();
            m_ValuesByStableId.Clear();
        }

    private:
        Container::UnorderedMap<Identity, PropertyValue, Identity::Hasher> m_ValuesByName;
        PropertyBag m_ValuesByStableId;
    };

    /**
     * @brief PROPERTYマクロで宣言される値ラッパー
     *
     * 通常のメンバアクセスと反射アクセスが同じ実値を参照するよう、
     * ClassPropertyはこのラッパー内の値をメンバオフセットから解決します。
     */
    template <typename T>
    class TPropertyValue
    {
    public:
        TPropertyValue() : m_Value{} {}
        TPropertyValue(const T &value) : m_Value(value) {}
        TPropertyValue(T &&value) : m_Value(std::move(value)) {}

        operator T &() { return m_Value; }
        operator const T &() const { return m_Value; }

        TPropertyValue &operator=(const T &value)
        {
            m_Value = value;
            return *this;
        }

        TPropertyValue &operator=(T &&value)
        {
            m_Value = std::move(value);
            return *this;
        }

        T *operator->() { return &m_Value; }
        const T *operator->() const { return &m_Value; }

        TPropertyValue &operator++()
        {
            ++m_Value;
            return *this;
        }

        T operator++(int)
        {
            T old = m_Value;
            ++m_Value;
            return old;
        }

        TPropertyValue &operator--()
        {
            --m_Value;
            return *this;
        }

        T operator--(int)
        {
            T old = m_Value;
            --m_Value;
            return old;
        }

        T &Get() { return m_Value; }
        const T &Get() const { return m_Value; }

    private:
        T m_Value;
    };

    template <typename CharT, typename Traits, typename T>
    std::basic_ostream<CharT, Traits> &operator<<(std::basic_ostream<CharT, Traits> &os, const TPropertyValue<T> &value)
    {
        return os << value.Get();
    }

    /**
     * @brief 型付きのプロパティクラス
     * @tparam T プロパティの型
     */
    template <typename T>
    class TClassProperty : public ClassProperty
    {
    public:
        using MutableGetter = T &(*)(IUnknown *);
        using ConstGetter = const T &(*)(const IUnknown *);

        TClassProperty(const Identity &name, const IClass *type, size_t offset, size_t size, uint32_t flags = 0,
                       MutableGetter mutableGetter = nullptr, ConstGetter constGetter = nullptr)
            : ClassProperty(name, type, offset, size, flags), m_DefaultValue(),
              m_MutableGetter(mutableGetter), m_ConstGetter(constGetter)
        {
        }

        /**
         * @brief デフォルト値を設定します
         * @param defaultValue デフォルト値
         */
        void SetDefaultValue(const T &defaultValue)
        {
            m_DefaultValue = defaultValue;
        }

        /**
         * @brief プロパティ値への参照を取得
         * @param instance オブジェクトインスタンス
         * @return プロパティ値への参照
         */
        T &GetRef(IUnknown *instance) const
        {
            if (m_MutableGetter)
            {
                return m_MutableGetter(instance);
            }

            auto *base = reinterpret_cast<uint8_t *>(instance);
            auto *property = reinterpret_cast<TPropertyValue<T> *>(base + m_Offset);
            return property->Get();
        }

        /**
         * @brief プロパティ値へのconst参照を取得
         * @param instance オブジェクトインスタンス
         * @return プロパティ値へのconst参照
         */
        const T &GetRef(const IUnknown *instance) const
        {
            if (m_ConstGetter)
            {
                return m_ConstGetter(instance);
            }

            const auto *base = reinterpret_cast<const uint8_t *>(instance);
            const auto *property = reinterpret_cast<const TPropertyValue<T> *>(base + m_Offset);
            return property->Get();
        }

        /**
         * @brief プロパティ値を設定
         * @param instance オブジェクトインスタンス
         * @param value 設定する値
         */
        void SetValue(IUnknown *instance, const T &value) const
        {
            T &ref = GetRef(instance);
            ref = value;
        }

        virtual const void *GetValuePtr(const IUnknown *instance) const override
        {
            if (!instance)
            {
                return nullptr;
            }
            return &GetRef(instance);
        }

        virtual void *GetValuePtr(IUnknown *instance) const override
        {
            if (!instance)
            {
                return nullptr;
            }
            return &GetRef(instance);
        }

        /**
         * @brief プロパティの初期値を適用します
         * @param instance オブジェクトインスタンス
         * @param initializer 初期値を提供する初期化子
         * @return 初期値が適用された場合はtrue
         */
        bool ApplyValue(IUnknown *instance, const PropertyValue &value) const override
        {
            if (!instance)
            {
                return false;
            }

            const T *typedValue = value.Get<T>();
            if (!typedValue)
            {
                return false;
            }

            SetValue(instance, *typedValue);
            return true;
        }

        bool ApplyInitialValue(IUnknown *instance, const FieldInitializer *initializer) const override
        {
            if (initializer)
            {
                const PropertyValue *value = initializer->FindValue(m_Name);
                if (value)
                {
                    return ApplyValue(instance, *value);
                }
            }
            return false;
        }

        /**
         * @brief デフォルト値を適用します
         * @param instance オブジェクトインスタンス
         */
        void ApplyDefaultValue(IUnknown *instance) const override
        {
            SetValue(instance, m_DefaultValue);
        }

        bool CopyValueFrom(IUnknown *destInstance, const IUnknown *srcInstance) const override
        {
            if (!destInstance || !srcInstance)
            {
                return false;
            }

            SetValue(destInstance, GetRef(srcInstance));
            return true;
        }

        void CaptureDefaultValue(const IUnknown *instance) override
        {
            if (instance)
            {
                m_DefaultValue = GetRef(instance);
            }
        }

        TypeId GetRuntimeTypeId() const override
        {
            return TypeRegistry::Get().GetTypeId<T>();
        }

    private:
        T m_DefaultValue; // プロパティのデフォルト値
        MutableGetter m_MutableGetter;
        ConstGetter m_ConstGetter;
    };

    /**
     * @brief クラスのプロパティ参照クラス
     * ClassPropertyへのアクセスを簡略化する
     * @tparam T プロパティの型
     */
    template <typename T>
    class PropertyRef
    {
    public:
        PropertyRef(IUnknown *instance, const TClassProperty<T> *property)
            : m_Instance(instance), m_Property(property)
        {
        }

        // 暗黙的キャスト演算子でプロパティ値を直接取得可能に
        operator T() const
        {
            return m_Property->GetRef(m_Instance);
        }

        // 代入演算子でプロパティ値を設定可能に
        PropertyRef &operator=(const T &value)
        {
            m_Property->SetValue(m_Instance, value);
            return *this;
        }

        // 加算代入演算子
        PropertyRef &operator+=(const T &value)
        {
            T current = m_Property->GetRef(m_Instance);
            current += value;
            m_Property->SetValue(m_Instance, current);
            return *this;
        }

        // 減算代入演子
        PropertyRef &operator-=(const T &value)
        {
            T current = m_Property->GetRef(m_Instance);
            current -= value;
            m_Property->SetValue(m_Instance, current);
            return *this;
        }

        // 直接参照を取得するポインタ演算子
        T *operator->()
        {
            return &(m_Property->GetRef(m_Instance));
        }

        // 直接参照を取得する間接参照演算子
        T &operator*()
        {
            return m_Property->GetRef(m_Instance);
        }

    private:
        IUnknown *m_Instance;
        const TClassProperty<T> *m_Property;
    };

    /**
     * @brief クラスのプロパティ参照クラス（constバージョン）
     * @tparam T プロパティの型
     */
    template <typename T>
    class ConstPropertyRef
    {
    public:
        ConstPropertyRef(const IUnknown *instance, const TClassProperty<T> *property)
            : m_Instance(instance), m_Property(property)
        {
        }

        // 暗黙的キャスト演算子でプロパティ値を直接取得可能に
        operator T() const
        {
            return m_Property->GetRef(m_Instance);
        }

        // 直接参照を取得するポインタ演算子
        const T *operator->() const
        {
            return &(m_Property->GetRef(m_Instance));
        }

        // 直接参照を取得する間接参照演算子
        const T &operator*() const
        {
            return m_Property->GetRef(m_Instance);
        }

    private:
        const IUnknown *m_Instance;
        const TClassProperty<T> *m_Property;
    };

    /**
     * @brief クラスの関数を表現するクラス
     */
    class ClassFunction
    {
    public:
        ClassFunction(const Identity &name, const IClass *returnType, uint32_t flags = 0)
            : m_Name(name), m_ReturnType(returnType), m_Flags(flags)
        {
        }

        virtual ~ClassFunction() = default;

        /**
         * @brief 関数名を取得します
         * @return 関数名
         */
        const Identity &GetName() const { return m_Name; }

        /**
         * @brief 返り値の型を取得します
         * @return 型情報
         */
        const IClass *GetReturnType() const { return m_ReturnType; }

        /**
         * @brief パラメータを追加します
         * @param type パラメータの型
         * @param name パラメータの名前
         */
        void AddParameter(const IClass *type, const Identity &name)
        {
            m_ParameterTypes.push_back(type);
            m_ParameterNames.push_back(name);
        }

        /**
         * @brief 引数の型を取得します
         * @return 引数の型情報配列
         */
        const Container::VariableArray<const IClass *> &GetParameterTypes() const { return m_ParameterTypes; }

        /**
         * @brief 引数の名前を取得します
         * @return 引数の名前配列
         */
        const Container::VariableArray<Identity> &GetParameterNames() const { return m_ParameterNames; }

        /**
         * @brief フラグを取得します
         * @return フラグ値
         */
        uint32_t GetFlags() const { return m_Flags; }

        /**
         * @brief Runtime schemaで使う戻り値TypeIdを取得します。
         */
        virtual TypeId GetRuntimeReturnTypeId() const { return InvalidSchemaId; }

        /**
         * @brief 関数を呼び出します
         * @param instance オブジェクトインスタンス
         * @param parameters 関数パラメータの配列
         * @param result 結果を格納するためのバッファ
         * @return 呼び出しが成功した場合はtrue
         */
        virtual bool Invoke(IUnknown *instance, const void *const *parameters, void *result) const = 0;

    private:
        Identity m_Name;                                           // 関数名
        const IClass *m_ReturnType;                                // 戻り値の型
        Container::VariableArray<const IClass *> m_ParameterTypes; // パラメータの型リスト
        Container::VariableArray<Identity> m_ParameterNames;       // パラメータの名前リスト
        uint32_t m_Flags;                                          // フラグ
    };

    template <typename ClassType, typename ReturnType>
    class TClassFunction : public ClassFunction
    {
    public:
        using FunctionPointer = ReturnType (ClassType::*)();

        TClassFunction(const Identity &name, FunctionPointer function, const IClass *returnType, uint32_t flags = 0)
            : ClassFunction(name, returnType, flags), m_Function(function)
        {
        }

        bool Invoke(IUnknown *instance, const void *const *parameters, void *result) const override
        {
            (void)parameters;

            ClassType *typedInstance = dynamic_cast<ClassType *>(instance);
            if (!typedInstance || !m_Function)
            {
                return false;
            }

            if constexpr (std::is_void_v<ReturnType>)
            {
                (typedInstance->*m_Function)();
                return true;
            }
            else
            {
                ReturnType value = (typedInstance->*m_Function)();
                if (result)
                {
                    *static_cast<ReturnType *>(result) = value;
                }
                return true;
            }
        }

        TypeId GetRuntimeReturnTypeId() const override
        {
            if constexpr (std::is_void_v<ReturnType>)
            {
                return InvalidSchemaId;
            }
            else
            {
                return TypeRegistry::Get().GetTypeId<ReturnType>();
            }
        }

    private:
        FunctionPointer m_Function;
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
            if (property)
            {
                m_Properties[property->GetName()] = std::move(property);
            }
        }

        /**
         * @brief プロパティを取得します
         * @param name プロパティ名
         * @return プロパティ、見つからない場合はnullptr
         */
        const ClassProperty *GetProperty(const Identity &name) const
        {
            auto it = m_Properties.find(name);
            if (it != m_Properties.end())
            {
                return it->second.get();
            }

            auto inheritedIt = m_InheritedProperties.find(name);
            if (inheritedIt != m_InheritedProperties.end())
            {
                return inheritedIt->second;
            }

            return nullptr;
        }

        /**
         * @brief すべてのプロパティを取得します
         * @return プロパティのベクター
         */
        Container::VariableArray<const ClassProperty *> GetAllProperties() const
        {
            Container::VariableArray<const ClassProperty *> result;
            result.reserve(m_InheritedProperties.size() + m_Properties.size());
            for (const auto &pair : m_InheritedProperties)
            {
                result.push_back(pair.second);
            }
            for (const auto &pair : m_Properties)
            {
                result.push_back(pair.second.get());
            }
            return result;
        }

        /**
         * @brief 継承されたプロパティを追加します（親クラスからの参照）
         * @param property 親クラスのプロパティへのポインタ
         */
        void AddInheritedProperty(const ClassProperty *property)
        {
            if (property)
            {
                m_InheritedProperties[property->GetName()] = property;
            }
        }

        /**
         * @brief プロパティの総サイズを取得します
         * @return 合計バイトサイズ
         */
        size_t GetTotalSize() const
        {
            size_t totalSize = 0;
            for (const auto &pair : m_Properties)
            {
                const auto &prop = pair.second;
                totalSize = std::max(totalSize, prop->GetOffset() + prop->GetSize());
            }
            for (const auto &pair : m_InheritedProperties)
            {
                const auto *prop = pair.second;
                totalSize = std::max(totalSize, prop->GetOffset() + prop->GetSize());
            }
            return totalSize;
        }

    private:
        // プロパティ名からプロパティへのマップ（このクラスで定義されたもの）
        Container::UnorderedMap<Identity, std::shared_ptr<ClassProperty>, Identity::Hasher> m_Properties;
        // 継承されたプロパティへのポインタマップ（親クラスから）
        Container::UnorderedMap<Identity, const ClassProperty *, Identity::Hasher> m_InheritedProperties;
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
            if (function)
            {
                m_Functions[function->GetName()] = std::move(function);
            }
        }

        /**
         * @brief 関数を取得します
         * @param name 関数名
         * @return 関数、見つからない場合はnullptr
         */
        const ClassFunction *GetFunction(const Identity &name) const
        {
            auto it = m_Functions.find(name);
            if (it != m_Functions.end())
            {
                return it->second.get();
            }

            auto inheritedIt = m_InheritedFunctions.find(name);
            if (inheritedIt != m_InheritedFunctions.end())
            {
                return inheritedIt->second;
            }

            return nullptr;
        }

        /**
         * @brief すべての関数を取得します
         * @return 関数のベクター
         */
        Container::VariableArray<const ClassFunction *> GetAllFunctions() const
        {
            Container::VariableArray<const ClassFunction *> result;
            result.reserve(m_InheritedFunctions.size() + m_Functions.size());
            for (const auto &pair : m_InheritedFunctions)
            {
                result.push_back(pair.second);
            }
            for (const auto &pair : m_Functions)
            {
                result.push_back(pair.second.get());
            }
            return result;
        }

        /**
         * @brief 継承された関数を追加します（親クラスからの参照）
         * @param function 親クラスの関数へのポインタ
         */
        void AddInheritedFunction(const ClassFunction *function)
        {
            if (function)
            {
                m_InheritedFunctions[function->GetName()] = function;
            }
        }

    private:
        // 関数名から関数へのマップ（このクラスで定義されたもの）
        Container::UnorderedMap<Identity, std::shared_ptr<ClassFunction>, Identity::Hasher> m_Functions;
        // 継承された関数へのポインタマップ（親クラスから）
        Container::UnorderedMap<Identity, const ClassFunction *, Identity::Hasher> m_InheritedFunctions;
    };

    /**
     * @brief クラス情報を定義するインターフェース
     * クラスの継承関係、メンバ変数、メンバ関数の情報を管理します
     */
    class IClass
    {
    public:
        virtual ~IClass() = default;

        /**
         * @brief クラス名を取得します
         * @return クラス名
         */
        virtual const Identity &GetClassName() const = 0;

        /**
         * @brief 親クラス情報を取得します
         * @return 親クラス情報へのポインタ、親がない場合はnullptr
         */
        virtual const IClass *GetParentClass() const = 0;

        /**
         * @brief このクラスが指定されたクラスを継承しているか確認します
         * @param cls 確認するクラス
         * @return 継承している場合はtrue
         */
        virtual bool IsChildOf(const IClass *cls) const = 0;

        /**
         * @brief このクラスの新しいインスタンスを作成します
         * @param outer 親オブジェクト（オプション）
         * @return 作成されたインスタンス、失敗した場合はnullptr
         */
        virtual IUnknown *NewInstance(IUnknown *outer = nullptr) const = 0;

        /**
         * @brief プロパティフィールドを取得します
         * @return プロパティフィールドへのポインタ
         */
        virtual const PropertyField *GetPropertyField() const = 0;

        /**
         * @brief 関数フィールドを取得します
         * @return 関数フィールドへのポインタ
         */
        virtual const FunctionField *GetFunctionField() const = 0;

        /**
         * @brief プロパティを取得します
         * @param name プロパティ名
         * @return プロパティへのポインタ、見つからない場合はnullptr
         */
        virtual const ClassProperty *GetProperty(const Identity &name) const = 0;

        /**
         * @brief すべてのプロパティを取得します
         * @return プロパティの配列
         */
        virtual Container::VariableArray<const ClassProperty *> GetAllProperties() const = 0;

        /**
         * @brief 関数を取得します
         * @param name 関数名
         * @return 関数へのポインタ、見つからない場合はnullptr
         */
        virtual const ClassFunction *GetFunction(const Identity &name) const = 0;

        /**
         * @brief すべての関数を取得します
         * @return 関数の配列
         */
        virtual Container::VariableArray<const ClassFunction *> GetAllFunctions() const = 0;

        /**
         * @brief クラスIDを取得します
         * @return クラス固有のID
         */
        virtual uint64_t GetClassId() const = 0;

        /**
         * @brief 継承階層における深さを取得します
         *
         * ルートクラス（Object）を0とし、派生するごとに1ずつ増えます。
         * IsChildOfが祖先テーブル（Cohenの定数時間判定）を用いて
         * O(1)で継承判定を行うために使用します。
         * @return 継承の深さ（ルート=0）
         */
        virtual uint32_t GetDepth() const = 0;

    };

    inline ClassInfo BuildClassInfoSnapshot(const IClass &cls, const char *moduleName = "NorvesLib")
    {
        ClassInfo info;
        info.Id = cls.GetClassId();
        info.StableId = MakeStableSchemaId(moduleName, "Class", cls.GetClassName().GetView());
        info.Name = cls.GetClassName().ToString();
        info.ParentId = cls.GetParentClass() ? cls.GetParentClass()->GetClassId() : InvalidSchemaId;

        Container::VariableArray<const ClassProperty *> properties = cls.GetAllProperties();
        info.Properties.reserve(properties.size());
        for (const ClassProperty *property : properties)
        {
            if (!property)
            {
                continue;
            }

            PropertyDesc desc;
            desc.Id = property->GetName().GetHash();
            desc.StableId = MakeStableSchemaId(
                moduleName,
                "Property",
                cls.GetClassName().GetView(),
                property->GetName().GetView());
            desc.Name = property->GetName().ToString();
            desc.Type = property->GetRuntimeTypeId();
            desc.Flags = static_cast<PropertyFlags>(property->GetFlags());
            desc.Storage = StorageKind::Member;
            info.Properties.push_back(std::move(desc));
        }

        Container::VariableArray<const ClassFunction *> functions = cls.GetAllFunctions();
        info.Functions.reserve(functions.size());
        for (const ClassFunction *function : functions)
        {
            if (!function)
            {
                continue;
            }

            FunctionDesc desc;
            desc.Id = function->GetName().GetHash();
            desc.StableId = MakeStableSchemaId(
                moduleName,
                "Function",
                cls.GetClassName().GetView(),
                function->GetName().GetView());
            desc.Name = function->GetName().ToString();
            desc.ReturnType = function->GetRuntimeReturnTypeId();
            desc.Flags = static_cast<FunctionFlags>(function->GetFlags());
            desc.Thread = ThreadPolicy::GameThreadOnly;
            info.Functions.push_back(std::move(desc));
        }

        return info;
    }

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
        static ClassRegistry &Get();

        /**
         * @brief 新しいクラスIDを発行します
         * @return 一意なクラスID
         */
        uint64_t AllocateClassId();

        /**
         * @brief クラス情報を登録します
         * @param cls 登録するクラス情報
         */
        void RegisterClass(const IClass *cls);

        /**
         * @brief クラス情報を名前で検索します
         * @param className クラス名
         * @return クラス情報へのポインタ、見つからない場合はnullptr
         */
        const IClass *FindClass(const Identity &className) const;

        /**
         * @brief クラス情報をIDで検索します
         * @param classId クラスID
         * @return クラス情報へのポインタ、見つからない場合はnullptr
         */
        const IClass *FindClass(uint64_t classId) const;

        /**
         * @brief 登録されているすべてのクラスを取得します
         * @return クラス情報の配列
         */
        Container::VariableArray<const IClass *> GetAllClasses() const;

    private:
        ClassRegistry() = default;
        ~ClassRegistry() = default;

        // クラス名からクラス情報へのマップ
        Container::UnorderedMap<Identity, const IClass *, Identity::Hasher> m_ClassesByName;

        // クラスIDからクラス情報へのマップ
        Container::UnorderedMap<uint64_t, const IClass *> m_ClassesById;

        // 次に割り当てるクラスID。Object基本クラスは0を予約する。
        uint64_t m_NextClassId = 1;
    };

} // namespace NorvesLib::Core
