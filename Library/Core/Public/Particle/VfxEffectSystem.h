#pragma once

#include "Math/Curves.h"
#include "Math/Matrix4x4.h"
#include "Particle/VfxEffectTypes.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/Wind.h"

#include <limits>

namespace NorvesLib::Core::Rendering
{
    struct BoardProxy;
    struct GpuParticleScene;
    struct GpuEmitterParams;
}

namespace NorvesLib::Core::Particle
{
    /// 衝突の高さ場。Heights は行優先 [z * Width + x]、XZ の WorldMin から WorldSize の範囲を覆う。
    struct VfxHeightFieldView
    {
        const float* Heights = nullptr;
        uint32_t Width = 0u;
        uint32_t Height = 0u;
        Math::Vector2 WorldMin = Math::Vector2(0.0f, 0.0f);
        Math::Vector2 WorldSize = Math::Vector2(0.0f, 0.0f);

        bool IsBound() const;
    };

    /// 1 刻みに外から渡す値。
    struct VfxTickContext
    {
        /// 無倍率の刻み幅。負または非有限なら deltaTime と同じ。
        float UnscaledDeltaTime = -1.0f;
        /// カメラを中心にした生成領域と折り返しに使う位置。
        Math::Vector3 CameraPosition = Math::Vector3(0.0f, 0.0f, 0.0f);
        /// 共通の風の設定。既定では無効。
        Rendering::WindParameters Wind;
        /// Rendering::WindClockSeconds で作った風の時刻。
        float WindTimeSeconds = 0.0f;
        /// 未結合なら CollisionHeightField は無効。
        VfxHeightFieldView HeightField;
    };

    struct VfxEffectHandle
    {
        uint32_t Index = std::numeric_limits<uint32_t>::max();
        uint32_t Generation = 0u;

        bool IsValid() const
        {
            return Index != std::numeric_limits<uint32_t>::max() && Generation != 0u;
        }

        bool operator==(const VfxEffectHandle& other) const = default;
    };

    struct VfxInstanceDesc
    {
        Math::Vector3 Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        uint32_t Seed = 0xC001D00Du;
        Rendering::TextureHandle Texture = Rendering::TextureHandle::Invalid();
        bool bEnabled = true;
        Rendering::MeshDataHandle Mesh = Rendering::MeshDataHandle::Invalid();
        Rendering::MaterialHandle Material = Rendering::MaterialHandle::Invalid();
    };

    struct VfxMeshInstance
    {
        uint64_t Id = 0u;
        Math::Matrix4x4 World;
        Math::Vector4 Color;
        float DissolveThreshold = 0.0f;
        float EdgeWidth = 0.0f;
        Math::Vector4 EdgeColor;
        Rendering::MeshDataHandle Mesh = Rendering::MeshDataHandle::Invalid();
        Rendering::MaterialHandle Material = Rendering::MaterialHandle::Invalid();
        uint32_t InstanceIndex = 0u;
        uint32_t EmitterIndex = 0u;
        uint32_t ParticleIndex = 0u;
    };

    struct VfxParticleState
    {
        Math::Vector3 Position;
        Math::Vector3 Velocity;
        Math::Vector4 Color;
        Math::Vector2 Size;
        /// 寿命の曲線を適用する前の生成時の色。
        Math::Vector4 BaseColor;
        /// 寿命の曲線を適用する前の生成時の大きさ。
        Math::Vector2 BaseSize;
        Math::Vector4 UVRect = Math::Vector4(0.0f, 0.0f, 1.0f, 1.0f);
        float Age = 0.0f;
        float Lifetime = 1.0f;
        double BirthTime = 0.0;
        uint32_t Frame = 0u;
        /// エミッタ内で生成要求ごとに割り当てる粒の通し番号。
        uint32_t Serial = 0u;
        /// 寿命の曲線で指定する伸び。
        float Stretch = 1.0f;
        /// 描画時の不透明度に掛ける有効の重み。
        float Activation = 1.0f;
        /// 衝突面に留まり、年齢だけを進める粒か。
        bool bStuck = false;
    };

    class VfxEffectSystem
    {
    public:
        VfxEffectSystem();
        ~VfxEffectSystem();
        VfxEffectSystem(VfxEffectSystem&&) noexcept;
        VfxEffectSystem& operator=(VfxEffectSystem&&) noexcept;
        VfxEffectSystem(const VfxEffectSystem&) = delete;
        VfxEffectSystem& operator=(const VfxEffectSystem&) = delete;

        VfxEffectHandle CreateInstance(const VfxEffectAsset& asset, const VfxInstanceDesc& desc, Container::String* outError = nullptr);
        bool DestroyInstance(VfxEffectHandle handle);
        void Clear();
        bool IsLiveInstance(VfxEffectHandle handle) const;
        bool SetInstancePosition(VfxEffectHandle handle, const Math::Vector3& position);
        bool SetInstanceEnabled(VfxEffectHandle handle, bool bEnabled);
        bool SetModuleParameter(VfxEffectHandle handle, uint32_t emitterIndex, uint32_t moduleIndex, const char* parameterName, const VfxValue& value, Container::String* outError = nullptr);
        bool SetEmitterProperty(VfxEffectHandle handle, uint32_t emitterIndex, const char* propertyName, const VfxValue& value, Container::String* outError = nullptr);
        bool RaiseEvent(VfxEffectHandle handle, const char* eventName, const Math::Vector3& position, double timeSeconds);
        /**
         * @brief 法線をローカルの +Z とするイベントを予約する。
         * @return イベントを受け付けた場合は true、入力が無効な場合は false。
         */
        bool RaiseOrientedEvent(VfxEffectHandle handle, const char* eventName, const Math::Vector3& position,
            const Math::Vector3& normal, double timeSeconds);
        bool RaiseGpuBurst(VfxEffectHandle handle, uint32_t emitterIndex, const Math::Vector3& position,
            const Math::Vector3& normal, const Math::Vector3& velocity, uint32_t count);
        void Tick(float deltaTime);
        void Tick(float deltaTime, const VfxTickContext& context);
        // CPU 側の時間入力を保ちながら GPU 側だけに無倍率時間を渡す。
        void Tick(float deltaTime, const VfxTickContext& context, float gpuUnscaledDeltaTime);
        void BuildGpuParticleFrame(Rendering::GpuParticleScene& out);
        double GetInstanceTime(VfxEffectHandle handle) const;
        uint32_t GetEmitterCount(VfxEffectHandle handle) const;
        bool CopyParticles(VfxEffectHandle handle, uint32_t emitterIndex, VariableArray<VfxParticleState>& outParticles) const;
        void AppendBoardProxies(VariableArray<Rendering::BoardProxy>& outProxies) const;
        void AppendMeshInstances(VariableArray<VfxMeshInstance>& outInstances) const;
        static float ComputeDissolveThreshold(float age, float lifetime, float dissolveStart, float dissolveDuration);
        static uint32_t ComputeFlipbookFrame(const VfxFlipbookImportSettings& flipbook, float age, float lifetime);
        static Math::Vector4 ComputeFlipbookUVRect(const VfxFlipbookImportSettings& flipbook, uint32_t frame);

    private:
        struct GpuExecutionState;

        struct ModuleBinding
        {
            uint32_t Arguments[16] = {};
        };

        struct WindInfluenceState
        {
            float Coupling = 0.0f;
            float Scale = 0.0f;
        };

        struct TurbulenceState
        {
            float Strength = 0.0f;
            float Frequency = 0.0f;
        };

        struct LifeCurveState
        {
            Math::CurveKeyframe Keys[kVfxMaxCurveKeys] = {};
            Math::Vector4 Color;
            uint32_t KeyCount = 0u;
        };

        struct CollisionState
        {
            VfxModuleType Type = VfxModuleType::CollisionPlane;
            VfxCollisionResponse Response = VfxCollisionResponse::Kill;
            float GroundY = 0.0f;
            float KillBelowY = 0.0f;
            float Restitution = 0.0f;
            float ChildProbability = 0.0f;
            uint32_t ChildEmitterIndex = std::numeric_limits<uint32_t>::max();
        };

        struct ChildSpawn
        {
            uint32_t EmitterIndex = 0u;
            Math::Vector3 Position;
        };

        struct EmitterState
        {
            VariableArray<VfxParticleState> Particles;
            VariableArray<ModuleBinding> Bindings;
            VariableArray<WindInfluenceState> WindInfluences;
            VariableArray<TurbulenceState> Turbulences;
            VariableArray<CollisionState> Collisions;
            LifeCurveState Curves[static_cast<uint32_t>(VfxCurveTarget::Stretch) + 1u];
            float SpawnAccumulator = 0.0f;
            uint32_t RandomState = 0u;
            uint32_t StreamSeed = 0u;
            uint32_t NextSerial = 0u;
            uint32_t Seed = 0u;
            uint32_t MaxParticles = 0u;
            uint32_t GpuPoolIndex = std::numeric_limits<uint32_t>::max();
            uint32_t GpuPoolGeneration = 0u;
            uint32_t GpuEpoch = 0u;
            uint32_t GpuPendingBurstCount = 0u;
            uint32_t GpuDuplicateWarnings = 0u;
            float GpuPendingDeltaTime = 0.0f;
            bool bGpuAllocationWarned = false;
            VfxExecutor Executor = VfxExecutor::Cpu;
            VfxRandomStream RandomStream = VfxRandomStream::Sequential;
            VfxTimeChannel TimeChannel = VfxTimeChannel::Particle;
            VfxWrapPolicy WrapPolicy = VfxWrapPolicy::Die;
            VfxSpawnShape Shape = VfxSpawnShape::Point;
            uint32_t Space = 0u;
            uint32_t SortKey = 0u;
            uint32_t Layer = 0u;
            uint32_t Blend = 0u;
            int32_t FlipbookIndex = -1;
            bool bHasRenderer = false;
            bool bHasMeshRenderer = false;
            bool bAlignToVelocity = false;
            bool bHasVelocity = false;
            bool bHasDrag = false;
            bool bHasCurves = false;
            bool bHasActivation = false;
            float Drag = 0.0f;
            float ActivationFraction = 1.0f;
            float ActivationRamp = 0.0f;
            float ShapeRadius = 0.0f;
            float ConeAngle = 0.0f;
            float ConeLength = 0.0f;
            Math::Vector3 BoxHalfExtents;
            float CameraBoxHalfWidth = 0.0f;
            float CameraBoxHeight = 0.0f;
            Math::Vector3 Offset;
            Math::Vector3 VelocityMin;
            Math::Vector3 VelocityMax;
            Math::Vector3 Acceleration;
            Math::Vector4 Color;
            Math::Vector2 Size;
            float Lifetime = 1.0f;
            float DissolveStart = 0.5f;
            float DissolveDuration = 0.5f;
            float EdgeWidth = 0.05f;
            Math::Vector4 EdgeColor = Math::Vector4(4.0f, 1.5f, 0.3f, 1.0f);
        };

        struct PendingEvent
        {
            Container::String Name;
            Math::Vector3 Position;
            Math::Vector3 Normal;
            double Time = 0.0;
            bool bOriented = false;
        };

        struct InstanceSlot
        {
            VfxEffectAsset Asset;
            VfxInstanceDesc Desc;
            VariableArray<EmitterState> Emitters;
            VariableArray<PendingEvent> Events;
            uint32_t PropertyIndices[8] = {};
            double Time = 0.0;
            Math::Vector3 PreviousGpuPosition;
            uint32_t Generation = 1u;
            bool bHasPreviousGpuPosition = false;
            bool bActive = false;
            bool bRetired = false;
        };

        static uint32_t InitialRandomState(uint32_t seed, uint32_t emitterIndex);
        static float NextUnitFloat(uint32_t& state);
        static bool CacheBindings(InstanceSlot& slot);
        void RefreshEmitter(InstanceSlot& slot, uint32_t emitterIndex);
        GpuExecutionState& EnsureGpuState();
        static Rendering::GpuEmitterParams PackGpuEmitterParams(const InstanceSlot& slot, uint32_t emitterIndex,
            EmitterState& emitter, const VariableArray<uint32_t>& frameIndices, Rendering::GpuParticleScene& out);
        static void UpdateFlipbook(const InstanceSlot& slot, const EmitterState& emitter, VfxParticleState& particle);
        static void UpdateOverLife(const EmitterState& emitter, VfxParticleState& particle);
        static bool ResolveCollisions(const EmitterState& emitter, VfxParticleState& particle,
            const VfxHeightFieldView& heightField, VariableArray<ChildSpawn>& children);
        static void SpawnParticles(InstanceSlot& slot, EmitterState& emitter, uint32_t count,
            const Math::Vector3& position, double birthTime, float age, const VfxTickContext& context,
            const Math::Vector3* orientationNormal = nullptr, bool bUsePositionForCameraBox = false);
        void ReleaseSlot(uint32_t index);

        TUniquePtr<GpuExecutionState> m_GpuState;
        VariableArray<InstanceSlot> m_Instances;
        VariableArray<uint32_t> m_FreeInstanceIndices;
    };
} // namespace NorvesLib::Core::Particle
