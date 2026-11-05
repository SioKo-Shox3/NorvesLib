#pragma once

#include "GameMode/GameModeRegistry.h"

namespace NorvesLib::Modules::Audio
{
    /**
     * @brief GameThread 専用の、GameMode が鳴らす音の所有キーを返す。
     * @details 最上位ビットを 1、下位 63 ビットをモード ID のハッシュにする。
     * 最上位ビットが 0 の AudioComponent のキーとは重ならない。
     * モード内の再生では、この値を SoundPlayParams::OwnerKey に指定する。
     */
    [[nodiscard]] uint64_t GetGameModeOwnerKey(Core::GameMode::GameModeId id);

    /**
     * @brief GameThread 専用の、退場後にモード所有の音を停止する包み。
     * @details 渡すモードは有効であること。生成関数の包みは空の生成結果をそのまま返す。
     */
    class AudioGameModeOwner final : public Core::GameMode::IGameMode
    {
    public:
        AudioGameModeOwner(Core::GameMode::GameModeId id,
                           Core::Container::TUniquePtr<Core::GameMode::IGameMode> mode);

        Core::GameMode::GameModeEnterResult Enter(Core::GameMode::GameModeContext& ctx) override;
        void Tick(Core::GameMode::GameModeContext& ctx, float deltaTime) override;
        void Leave(Core::GameMode::GameModeContext& ctx, Core::GameMode::GameModeExitReason reason) override;
        void Suspend(Core::GameMode::GameModeContext& ctx) override;
        void Resume(Core::GameMode::GameModeContext& ctx) override;
        const char* GetDebugName() const override;

    private:
        Core::GameMode::GameModeId m_Id;
        Core::Container::TUniquePtr<Core::GameMode::IGameMode> m_Mode;
    };

    /** @brief GameThread 専用の、生成されたモードに音の所有権を付ける生成関数を返す。 */
    [[nodiscard]] Core::GameMode::GameModeRegistry::Creator WrapGameModeCreatorWithAudioOwner(
        Core::GameMode::GameModeId id, Core::GameMode::GameModeRegistry::Creator creator);
} // namespace NorvesLib::Modules::Audio
