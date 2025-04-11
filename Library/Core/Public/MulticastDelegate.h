#pragma once

#include "Delegate.h"
#include <vector>
#include <algorithm>

namespace NorvesLib::Core
{

/**
 * @brief 複数の関数を登録して一括呼び出しするためのマルチキャストデリゲートクラス
 * @tparam RetType 戻り値の型（戻り値は無視されるためvoidのみサポート）
 * @tparam Args 引数の型...
 */
template<typename... Args>
class MulticastDelegate
{
private:
    // マルチキャストデリゲートでは戻り値を扱わないため、内部的にはvoidを返すデリゲートを使用
    using DelegateType = Delegate<void, Args...>;
    
    // 登録された関数のリスト
    std::vector<DelegateType> m_Delegates;
    
public:
    /**
     * @brief デフォルトコンストラクタ
     */
    MulticastDelegate() = default;
    
    /**
     * @brief デリゲートにバインドされている関数の数を取得
     * @return 登録されている関数の数
     */
    size_t GetSize() const
    {
        return m_Delegates.size();
    }
    
    /**
     * @brief デリゲートが空かどうか確認
     * @return 関数が一つも登録されていない場合true
     */
    bool IsEmpty() const
    {
        return m_Delegates.empty();
    }
    
    /**
     * @brief すべての登録された関数をクリア
     */
    void Clear()
    {
        m_Delegates.clear();
    }
    
    /**
     * @brief 関数をデリゲートに追加
     * @param function 追加する関数ポインタ
     * @return 自身への参照
     */
    MulticastDelegate& Add(void(*function)(Args...))
    {
        m_Delegates.emplace_back(function);
        return *this;
    }
    
    /**
     * @brief デリゲートをマルチキャストデリゲートに追加
     * @param delegate 追加するデリゲート
     * @return 自身への参照
     */
    MulticastDelegate& Add(const DelegateType& delegate)
    {
        if (delegate.IsBound())
        {
            m_Delegates.push_back(delegate);
        }
        return *this;
    }
    
    /**
     * @brief 関数オブジェクトをデリゲートに追加
     * @tparam F 関数オブジェクトの型
     * @param functor 追加する関数オブジェクト
     * @return 自身への参照
     */
    template<typename F>
    MulticastDelegate& Add(F&& functor)
    {
        m_Delegates.emplace_back(std::forward<F>(functor));
        return *this;
    }
    
    /**
     * @brief メソッドとオブジェクトをデリゲートに追加
     * @tparam T オブジェクトの型
     * @tparam Method メソッドの型
     * @param instance オブジェクトのポインタ
     * @param method メソッドのポインタ
     * @return 自身への参照
     */
    template<typename T, typename Method>
    MulticastDelegate& Add(T* instance, Method method)
    {
        m_Delegates.emplace_back(instance, method);
        return *this;
    }
    
    /**
     * @brief 関数をデリゲートから削除
     * @param function 削除する関数ポインタ
     * @return 自身への参照
     */
    MulticastDelegate& Remove(void(*function)(Args...))
    {
        DelegateType toRemove(function);
        RemoveDelegate(toRemove);
        return *this;
    }
    
    /**
     * @brief デリゲートをマルチキャストデリゲートから削除
     * @param delegate 削除するデリゲート
     * @return 自身への参照
     */
    MulticastDelegate& Remove(const DelegateType& delegate)
    {
        RemoveDelegate(delegate);
        return *this;
    }
    
    /**
     * @brief メソッドとオブジェクトをデリゲートから削除
     * @tparam T オブジェクトの型
     * @tparam Method メソッドの型
     * @param instance オブジェクトのポインタ
     * @param method メソッドのポインタ
     * @return 自身への参照
     */
    template<typename T, typename Method>
    MulticastDelegate& Remove(T* instance, Method method)
    {
        DelegateType toRemove(instance, method);
        RemoveDelegate(toRemove);
        return *this;
    }
    
    /**
     * @brief マルチキャストデリゲートに登録されたすべての関数を実行
     * @param args 関数に渡す引数
     */
    void Broadcast(Args... args) const
    {
        for (const auto& delegate : m_Delegates)
        {
            delegate.InvokeIfBound(std::forward<Args>(args)...);
        }
    }
    
    /**
     * @brief マルチキャストデリゲートを関数呼び出し演算子で実行
     * @param args 関数に渡す引数
     */
    void operator()(Args... args) const
    {
        Broadcast(std::forward<Args>(args)...);
    }
    
    /**
     * @brief デリゲートを追加する演算子オーバーロード
     * @param rhs 追加するデリゲート
     * @return 自身への参照
     */
    MulticastDelegate& operator+=(const DelegateType& rhs)
    {
        return Add(rhs);
    }
    
    /**
     * @brief 関数ポインタを追加する演算子オーバーロード
     * @param rhs 追加する関数ポインタ
     * @return 自身への参照
     */
    MulticastDelegate& operator+=(void(*rhs)(Args...))
    {
        return Add(rhs);
    }
    
    /**
     * @brief デリゲートを削除する演算子オーバーロード
     * @param rhs 削除するデリゲート
     * @return 自身への参照
     */
    MulticastDelegate& operator-=(const DelegateType& rhs)
    {
        return Remove(rhs);
    }
    
    /**
     * @brief 関数ポインタを削除する演算子オーバーロード
     * @param rhs 削除する関数ポインタ
     * @return 自身への参照
     */
    MulticastDelegate& operator-=(void(*rhs)(Args...))
    {
        return Remove(rhs);
    }
    
private:
    /**
     * @brief デリゲートをリストから削除する内部関数
     * @param delegate 削除するデリゲート
     */
    void RemoveDelegate(const DelegateType& delegate)
    {
        m_Delegates.erase(
            std::remove_if(
                m_Delegates.begin(), 
                m_Delegates.end(), 
                [&delegate](const DelegateType& item) {
                    return item == delegate;
                }
            ), 
            m_Delegates.end()
        );
    }
};

// マルチキャストデリゲート作成のヘルパー関数
template<typename... Args>
MulticastDelegate<Args...> MakeMulticastDelegate()
{
    return MulticastDelegate<Args...>();
}

} // namespace NorvesLib::Core