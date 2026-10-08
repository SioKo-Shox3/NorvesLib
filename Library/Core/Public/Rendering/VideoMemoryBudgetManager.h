#pragma once

// VRAM の予算をプールへ割り振る計算。
// 上限 = min(ヒープの予算, --vram-budget-mb)（どちらも無ければ DeviceLocal のヒープの大きさ）から、プール以外の使用量を引いた残りを、プールごとの取り分で分ける。
// プール以外の使用量は、ヒープの使用量が取れるときは「ヒープの使用量 − プールの確保分」で決める。
// 取れないときは台帳で数えきれない確保（解放待ち・パスが直接作るテクスチャ）があるので、上限の一定割合を見込む近似にする。
// 時刻もデバイスも持たない計算だけの型なので、予算値を制御したテストから境界を確かめられる。
// 呼ぶ間隔（約1秒）は呼び出し側（RenderResources::PollVideoMemoryBudget）が決める。

#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    /** @brief 予算を受け取るプールの種類（ジオメトリ・VSM は後の段で使う枠） */
    enum class VideoMemoryPool : uint32_t
    {
        VirtualTexture = 0,
        Geometry,
        ShadowMap,
        Count
    };

    constexpr uint32_t VideoMemoryPoolCount = static_cast<uint32_t>(VideoMemoryPool::Count);

    /** @brief 予算の計算に渡す、ある時点の値 */
    struct VideoMemoryBudgetInput
    {
        /** @brief ヒープの予算・使用量を取得できたか（取得手段が無いバックエンドでは false） */
        bool bHeapValid = false;
        /** @brief ヒープの予算（バイト）。bHeapValid でも 0 のときは異常値として上限に使わない */
        uint64_t HeapBudgetBytes = 0;
        /** @brief このプロセスが使っているヒープの量（バイト。プールの確保分を含む） */
        uint64_t HeapUsageBytes = 0;
        /** @brief --vram-budget-mb の上限（バイト。0 は上限なし） */
        uint64_t CapBytes = 0;
        /** @brief DeviceLocal のヒープの大きさの合計（バイト。0 は不明）。ヒープの予算も CapBytes も無いときだけ上限に使う */
        uint64_t DeviceLocalHeapBytes = 0;
        /** @brief プールごとに確保済みの物理メモリの量（バイト）。ヒープの使用量から引いてプール以外を出す */
        uint64_t PoolCapacityBytes[VideoMemoryPoolCount] = {};
    };

    /** @brief 予算の計算結果 */
    struct VideoMemoryBudgetResult
    {
        /** @brief 上限がある（ヒープの予算か --vram-budget-mb のどちらかが使える）か。false の間は目標を持たない */
        bool bLimited = false;
        /** @brief 上限（バイト）。bLimited が false のとき 0 */
        uint64_t CeilingBytes = 0;
        /** @brief プール以外の使用量（バイト） */
        uint64_t NonPoolBytes = 0;
        /** @brief プール以外の使用量が見込みか（ヒープの使用量が取れず、上限の一定割合で代えた） */
        bool bNonPoolEstimated = false;
        /** @brief 取り分を持たない（重み 0 の）プールの確保量の合計（バイト）。固定の取り置きとして割り振れる量から引く */
        uint64_t FixedPoolBytes = 0;
        /** @brief 取り分のあるプールへ割り振れる量（バイト）。上限 − プール以外の使用量 − 固定の取り置きで、負にはならず 0 で止まる */
        uint64_t AvailableBytes = 0;
        /** @brief プールごとの目標の大きさ（バイト）。bLimited が false のとき全て 0 */
        uint64_t PoolTargetBytes[VideoMemoryPoolCount] = {};

        uint64_t GetTargetBytes(VideoMemoryPool pool) const
        {
            return PoolTargetBytes[static_cast<uint32_t>(pool)];
        }
    };

    class VideoMemoryBudgetManager
    {
    public:
        /** @brief 取り分の重みの上限（掛け算が溢れないように丸める） */
        static constexpr uint32_t MaxShareWeight = 65535;

        /** @brief 前回ログした値からこの割合（%）以上変わったら再びログを出す */
        static constexpr uint64_t ChangePercentThreshold = 1;

        /** @brief ヒープの使用量が取れないとき、プール以外へ見込む上限の割合（%） */
        static constexpr uint64_t EstimatedNonPoolPercent = 30;

        /** @brief 既定では VT のプールがすべてを受け取り、ジオメトリ・VSM は取り分を持たない */
        VideoMemoryBudgetManager()
        {
            m_ShareWeights[static_cast<uint32_t>(VideoMemoryPool::VirtualTexture)] = 1;
        }

        /** @brief プールの取り分の重みを決める（0 は取り分なし）。目標は重みの比で分かれる */
        void SetPoolShare(VideoMemoryPool pool, uint32_t weight)
        {
            m_ShareWeights[static_cast<uint32_t>(pool)] = weight > MaxShareWeight ? MaxShareWeight : weight;
        }

        uint32_t GetPoolShare(VideoMemoryPool pool) const
        {
            return m_ShareWeights[static_cast<uint32_t>(pool)];
        }

        /** @brief プールへ割り振る量を計算する */
        VideoMemoryBudgetResult Compute(const VideoMemoryBudgetInput &input) const
        {
            VideoMemoryBudgetResult result;

            // 取り分を持たないプール（VSM の物理ページのように大きさが決まっているもの）の確保量は、目標を割り振らずに
            // 固定の取り置きとして割り振れる量から引く（引かないと、その分を取り分のあるプールへ二重に割り振る）
            uint64_t poolSum = 0;
            for (uint32_t i = 0; i < VideoMemoryPoolCount; ++i)
            {
                poolSum += input.PoolCapacityBytes[i];
                if (m_ShareWeights[i] == 0)
                {
                    result.FixedPoolBytes += input.PoolCapacityBytes[i];
                }
            }

            // プール以外の使用量: ヒープの使用量が取れるときはそこからプールの確保分を引く
            if (input.bHeapValid)
            {
                result.NonPoolBytes = input.HeapUsageBytes > poolSum ? input.HeapUsageBytes - poolSum : 0;
            }

            const bool bHeapCeiling = input.bHeapValid && input.HeapBudgetBytes > 0;
            const bool bCapCeiling = input.CapBytes > 0;
            const bool bSizeCeiling = !bHeapCeiling && !bCapCeiling && input.DeviceLocalHeapBytes > 0;
            if (!bHeapCeiling && !bCapCeiling && !bSizeCeiling)
            {
                return result;
            }

            result.bLimited = true;
            if (bHeapCeiling && bCapCeiling)
            {
                result.CeilingBytes = input.HeapBudgetBytes < input.CapBytes ? input.HeapBudgetBytes : input.CapBytes;
            }
            else if (bHeapCeiling)
            {
                result.CeilingBytes = input.HeapBudgetBytes;
            }
            else
            {
                result.CeilingBytes = bCapCeiling ? input.CapBytes : input.DeviceLocalHeapBytes;
            }
            if (!input.bHeapValid)
            {
                // 取れないときは上限の一定割合を見込む（商と余りに分けて 64bit を溢れさせない）
                result.bNonPoolEstimated = true;
                result.NonPoolBytes = result.CeilingBytes / 100 * EstimatedNonPoolPercent +
                                      result.CeilingBytes % 100 * EstimatedNonPoolPercent / 100;
            }
            const uint64_t reservedBytes = result.NonPoolBytes + result.FixedPoolBytes;
            result.AvailableBytes = result.CeilingBytes > reservedBytes ? result.CeilingBytes - reservedBytes : 0;

            uint64_t totalWeight = 0;
            for (uint32_t i = 0; i < VideoMemoryPoolCount; ++i)
            {
                totalWeight += m_ShareWeights[i];
            }
            if (totalWeight == 0)
            {
                return result;
            }

            // 取り分 = available * weight / total。先に商と余りに分けて、64bit を溢れさせない
            const uint64_t quotient = result.AvailableBytes / totalWeight;
            const uint64_t remainder = result.AvailableBytes % totalWeight;
            for (uint32_t i = 0; i < VideoMemoryPoolCount; ++i)
            {
                const uint64_t weight = m_ShareWeights[i];
                result.PoolTargetBytes[i] = quotient * weight + remainder * weight / totalWeight;
            }
            return result;
        }

        /**
         * @brief 計算結果を VRAM_POOLS に出すべきか判定し、出すなら前回値として記録する
         * @return 初回、上限の有無・見込みか否かが変わったとき、または上限・プール以外の使用量・VT の目標のどれかが
         *         前回ログした値から 1% 以上変わったとき true
         */
        bool CommitLogIfChanged(const VideoMemoryBudgetResult &result)
        {
            const uint64_t vtTarget = result.GetTargetBytes(VideoMemoryPool::VirtualTexture);
            if (m_bLogged &&
                m_bLoggedLimited == result.bLimited &&
                m_bLoggedEstimated == result.bNonPoolEstimated &&
                !HasChanged(m_LoggedCeilingBytes, result.CeilingBytes) &&
                !HasChanged(m_LoggedNonPoolBytes, result.NonPoolBytes) &&
                !HasChanged(m_LoggedVtTargetBytes, vtTarget))
            {
                return false;
            }

            m_bLogged = true;
            m_bLoggedLimited = result.bLimited;
            m_bLoggedEstimated = result.bNonPoolEstimated;
            m_LoggedCeilingBytes = result.CeilingBytes;
            m_LoggedNonPoolBytes = result.NonPoolBytes;
            m_LoggedVtTargetBytes = vtTarget;
            return true;
        }

    private:
        static bool HasChanged(uint64_t previous, uint64_t current)
        {
            const uint64_t difference = previous > current ? previous - current : current - previous;
            return difference * 100 >= (previous > 0 ? previous : 1) * ChangePercentThreshold;
        }

        uint32_t m_ShareWeights[VideoMemoryPoolCount] = {};
        bool m_bLogged = false;
        bool m_bLoggedLimited = false;
        bool m_bLoggedEstimated = false;
        uint64_t m_LoggedCeilingBytes = 0;
        uint64_t m_LoggedNonPoolBytes = 0;
        uint64_t m_LoggedVtTargetBytes = 0;
    };

} // namespace NorvesLib::Core::Rendering
