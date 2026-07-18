#pragma once

// Workstream L-P2a: NorvesLib 用 Bridge エンジンアダプタ。
//
// NorvesEditor Bridge engine SDK の IBridgeEngineAdapter を NorvesLib 向けに実装する。
// P2a は「最小だがスキーマ準拠」の応答に留める（runtime/status/log の本実装は L-P2b、
// log.message ストリーミングは L-P3b）。
//
// スレッド前提: すべてのコールバックは BridgeServerHost::DrainInbound 経由で
// ゲームスレッド上から同期的に呼ばれる。params は呼び出し中のみ借用で、戻り値の
// 後に保持しない。エンジン値は DTO スナップショットへ変換してから返す。
//
// SDK 非設定ビルド対応: SDK 依存は NORVES_BRIDGE_ENABLED でガードする。define が
// 無いビルドでは何も定義しない（このアダプタは BridgeServerHost からのみ参照され、
// その参照経路も同じガード下にある）。

#if defined(NORVES_BRIDGE_ENABLED)

#include <cstdint>
#include <string>
#include <string_view>

#include "Norves/Bridge/adapter.hpp"
#include "Norves/Bridge/error.hpp"
#include "Norves/Bridge/json_value.hpp"
#include "Norves/Bridge/result.hpp"

namespace NorvesLib::Core::Rendering
{
    struct CapturedFrame;
    struct FrameCaptureRequestResult;
} // namespace NorvesLib::Core::Rendering

// GameApplicationHandler を adapter.h で include すると循環になる
// （GameApplicationHandler.h が adapter.h を include する）。adapter は実体を
// .cpp でのみ使うため、ここでは前方宣言に留める。
namespace Game
{
    class GameApplicationHandler;
} // namespace Game

// BridgeServerHost も同様に .cpp でのみ完全型を使う（EmitEvent / Start・StopLogForwarding
// の呼び出し）。ヘッダでは前方宣言に留める。
namespace Game::Bridge
{
    class BridgeServerHost;
} // namespace Game::Bridge

namespace Game::Bridge
{

    /**
     * @brief NorvesLib の Bridge アダプタ（P2a 最小スキーマ準拠実装）
     */
    class NorvesLibBridgeAdapter final : public Norves::Bridge::IBridgeEngineAdapter
    {
    public:
        NorvesLibBridgeAdapter() = default;
        ~NorvesLibBridgeAdapter() override = default;

        /**
         * @brief 実エンジン状態へアクセスするための handler を注入する
         *
         * host.Start の前に GameApplicationHandler::OnInitialize から設定される。
         * adapter は host より長生きするため借用ポインタで十分（所有しない）。
         *
         * @param handler 実エンジン状態アクセス用の handler（借用）
         */
        void SetHandler(Game::GameApplicationHandler& handler) { m_Handler = &handler; }

        /**
         * @brief サーバー発イベントを発火するための host を注入する
         *
         * runtime.stateChanged / log.message の発火に使う。host.Start の前に
         * GameApplicationHandler::OnInitialize から設定される。すべてのコールバックは
         * ゲームスレッド上で逐次実行されるため借用ポインタで十分。
         *
         * @param host イベント発火用の host（借用）
         */
        void SetHost(Game::Bridge::BridgeServerHost& host) { m_Host = &host; }

#if defined(NORVES_BRIDGE_TESTING)
        struct ViewportThumbnailCaptureProviderForTesting
        {
            void* Context = nullptr;
            bool (*TryConsumeCapturedFrame)(void*, NorvesLib::Core::Rendering::CapturedFrame&) = nullptr;
            NorvesLib::Core::Rendering::FrameCaptureRequestResult (*RequestFrameCapture)(void*) = nullptr;
        };

        void SetViewportThumbnailCaptureProviderForTesting(
            ViewportThumbnailCaptureProviderForTesting provider)
        {
            m_ViewportThumbnailCaptureProviderForTesting = provider;
        }
#endif // NORVES_BRIDGE_TESTING

        // --- Handshake ---

        /**
         * @brief bridge.hello。sessionId を採番し HelloResult を返す
         *
         * @param params リクエスト params（借用、呼び出し中のみ有効）
         * @param selectedProtocolVersion サーバが選んだプロトコルバージョン
         * @return HelloResult を収めた JsonValue
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        hello(const Norves::Bridge::JsonValue& params,
              std::string_view selectedProtocolVersion) override;

        /**
         * @brief bridge.getCapabilities。広告する capability 一覧を返す
         *
         * @param params リクエスト params（借用、未使用）
         * @return capabilities を収めた JsonValue
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        getCapabilities(const Norves::Bridge::JsonValue& params) override;

        // --- Engine status / launch ---

        /**
         * @brief engine.getStatus。実エンジン状態を DTO スナップショットへ変換して返す
         *
         * @param params リクエスト params（借用、未使用）
         * @return StatusSnapshot を収めた JsonValue
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        getStatus(const Norves::Bridge::JsonValue& params) override;

        /**
         * @brief engine.launchInfo。pid と title を返す
         *
         * @param params リクエスト params（借用、未使用）
         * @return {pid, title} を収めた JsonValue
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        launchInfo(const Norves::Bridge::JsonValue& params) override;

        // --- Runtime control ---

        /**
         * @brief runtime.play。runtime 状態を Playing へ遷移させ PlayAck を返す
         *
         * @param params リクエスト params（借用、未使用）
         * @return PlayAck を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。runtime.stateChanged を先に emit する。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        runtimePlay(const Norves::Bridge::JsonValue& params) override;

        /**
         * @brief runtime.pause。runtime 状態を Paused へ遷移させ PlayAck を返す
         *
         * @param params リクエスト params（借用、未使用）
         * @return PlayAck を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。runtime.stateChanged を先に emit する。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        runtimePause(const Norves::Bridge::JsonValue& params) override;

        /**
         * @brief runtime.stop。runtime 状態を Stopped へ遷移させ PlayAck を返す
         *
         * @param params リクエスト params（借用、未使用）
         * @return PlayAck を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。runtime.stateChanged を先に emit する。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        runtimeStop(const Norves::Bridge::JsonValue& params) override;

        /**
         * @brief runtime.focusViewport。best-effort でエンジンウィンドウを前面化する
         *
         * @param params リクエスト params（借用、未使用）
         * @return {focused} を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。Win32 ハンドル取得時のみ操作する。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        runtimeFocusViewport(const Norves::Bridge::JsonValue& params) override;

        // --- Log streaming ---

        /**
         * @brief log.subscribe。Logger -> Bridge のログ転送を開始し subscriptionId を返す
         *
         * @param params リクエスト params（借用、filter.minLevel は honor しない）
         * @return {subscriptionId} を収めた JsonValue
         * @note 単一購読前提。ゲームスレッド上から逐次呼ばれる。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        logSubscribe(const Norves::Bridge::JsonValue& params) override;

        /**
         * @brief log.unsubscribe。ログ転送を停止する（冪等）
         *
         * @param params リクエスト params（借用、未使用）
         * @return {ok} を収めた JsonValue
         * @note 単一購読前提で subscriptionId 照合はしない。ゲームスレッド上から逐次呼ばれる。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        logUnsubscribe(const Norves::Bridge::JsonValue& params) override;

        // --- Schema ---

        /**
         * @brief schema.getSnapshot。class スキーマ投影を DTO スナップショットへ変換して返す
         *
         * RuntimeSchemaProjector::BuildClassSchemaSnapshot() が返す値コピー済み DTO
         * （ClassSchemaSnapshot）だけを使い、各 class を typeDescriptor（typeName / kind /
         * properties[{name, valueType}]）へ写す。Entity ポインタや Object ポインタ、生ポインタ、
         * ハンドルは JsonValue に入れない（live memory 非転送）。
         *
         * @param params リクエスト params（借用、未使用）
         * @return {types:[typeDescriptor, ...]} を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。無副作用（読み取りのみ）。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        schemaGetSnapshot(const Norves::Bridge::JsonValue& params) override;

        // --- Scene / Object（読み取り系） ---

        /**
         * @brief scene.getTree。World の Entity 階層を sceneNode ツリーへ写して返す
         *
         * 合成ルート（id="scene-root", kind="Scene"）配下に各ルート Entity を再帰でノード化する。
         * 各ノードの id は Entity の ObjectId（uint64_t）を 10 進文字列化したもの、kind は
         * クラス名（IClass::GetClassName）。Entity ポインタや Object ポインタ、生ポインタ、
         * ハンドルは JsonValue に入れない（live memory 非転送）。GEngine 未生成時は空ツリー
         * （子なしの合成ルート）を返す（not_supported は返さない＝scene.query 広告と整合）。
         *
         * @param params リクエスト params（借用。rootId/maxDepth は本実装では無視し全ツリーを返す）
         * @return {root:<sceneNode>} を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。無副作用（読み取りのみ）。防御的に再帰深さ上限を設ける。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        sceneGetTree(const Norves::Bridge::JsonValue& params) override;

        /**
         * @brief object.getSnapshot。1 つの Entity のプロパティスナップショットを返す
         *
         * params.objectId（文字列）を uint64_t へ解釈し、World から該当 Entity を逆引きする。
         * RuntimeSchemaProjector::ProjectClass / BuildObjectSnapshot が返す値コピー済み DTO
         * （プロパティ名・型・シリアライズ済み値）だけを使い、各プロパティを propertyEntry
         * （name / value / valueType）へ写す。value は SerializedValue を純 JSON 値へ変換する
         * （bool は true/false、算術型は number、Vector は配列、その他は文字列）。Entity ポインタ
         * や Object ポインタ、生ポインタ、ハンドルは JsonValue に入れない（live memory 非転送）。
         * パース不可 or 該当 Entity 無しのときは空スナップショット（{objectId, properties:[]}）を返す。
         *
         * @param params リクエスト params（借用、objectId を読む）
         * @return {objectId, kind?, properties:[propertyEntry, ...]} を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。無副作用（読み取りのみ）。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        objectGetSnapshot(const Norves::Bridge::JsonValue& params) override;

        // --- Object（書き込み系） ---

        /**
         * @brief object.setProperty。1 つの Entity の generic プロパティを書き換える
         *
         * params.objectId（文字列）を uint64_t へ解釈して World から該当 Entity を逆引きし、
         * params.property（文字列）でクラスの ClassProperty を引く。params.value（propertyValue=
         * 純 JSON 値）をエンジン側プロパティ型（prop->GetRuntimeTypeId() 由来の TypeInfo）に基づき
         * NorvesLib 内部シリアライズ表記へ逆変換し（WireJsonToSerialized、AppendWireValue の逆）、
         * PropertyValue::DeserializeStable → ClassProperty::ApplyValue で適用する。wire の valueType は
         * 信用せず、必ずエンジン側プロパティ型の StableId で復元する。適用は呼び出し中の同期・同
         * スレッド文脈（DrainInbound＝ゲームスレッド）でのみ行い、新規スレッド/marshal はしない。
         * 引数 value はすべて値コピーから組み（live memory 非転送）、appliedValue は適用後に Entity を
         * 再投影して読み戻した SerializedValue を wire JSON 値へ変換して入れる（M-6 往復一致）。
         * Entity null / 該当なし / プロパティなし / 型不一致 / 適用失敗のいずれも {"accepted":false}。
         *
         * Position/Rotation/Scale 等の Transform 系プロパティを変えた場合、ワールド変換は
         * World::Tick の UpdateWorldTransforms が次フレームで反映する（既存挙動）。ここで明示呼び出しは
         * しない。同フレーム即時の getSnapshot ではローカル値が反映される。
         *
         * @param params リクエスト params（借用、objectId / property / value を読む）
         * @return {accepted:bool, appliedValue?:<propertyValue>} を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。エンジン状態を変更する（副作用あり）。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        objectSetProperty(const Norves::Bridge::JsonValue& params) override;

        // --- Scene（書き込み系） ---

        /**
         * @brief scene.createObject。World に新規 Entity を生成する
         *
         * params.parentId（任意 string）を uint64_t へ解釈して親 Entity を逆引きし、
         * World::SpawnEntity<Entity>(parent) で生成する。parentId が無ければ親 nullptr（ルート生成）。
         * kind（任意 string）は無視する（動的型 + 親指定の public spawn API が無いため。MVP は常に
         * 基底 Entity を生成する）。生成成功時は scene.treeChanged を発火してから
         * {accepted:true, newId:"<ObjectId 10 進>"} を返す。parentId が指定されているのに逆引きできない、
         * GEngine 未生成、SpawnEntity が nullptr（World 未初期化含む）のいずれも {accepted:false}。
         * newId は ObjectId 値の 10 進文字列コピーのみを綴り、Entity ポインタや生ポインタは JsonValue へ
         * 一切入れない（live memory 非転送）。適用は呼び出し中の同期・同スレッド文脈
         * （DrainInbound＝ゲームスレッド）でのみ行い、新規スレッド/marshal はしない。
         *
         * @param params リクエスト params（借用、parentId / kind を読む）
         * @return {accepted:bool, newId?:string} を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。エンジン状態を変更する（副作用あり）。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        sceneCreateObject(const Norves::Bridge::JsonValue& params) override;

        /**
         * @brief scene.deleteObject。World から Entity を除去する
         *
         * params.objectId（必須 string）を uint64_t へ解釈して World から該当 Entity を逆引きし、
         * World::RemoveEntity(entity) で除去する。その bool を accepted とし、true のときだけ
         * scene.treeChanged を発火する。objectId 欠落 / パース失敗 / GEngine 未生成 / 該当なし /
         * RemoveEntity が false のいずれも {accepted:false}。Entity ポインタや生ポインタは JsonValue へ
         * 一切入れない（live memory 非転送）。適用は呼び出し中の同期・同スレッド文脈
         * （DrainInbound＝ゲームスレッド）でのみ行い、新規スレッド/marshal はしない。
         *
         * @param params リクエスト params（借用、objectId を読む）
         * @return {accepted:bool} を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。エンジン状態を変更する（副作用あり）。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        sceneDeleteObject(const Norves::Bridge::JsonValue& params) override;

        /**
         * @brief scene.reparentObject。Entity を別親（またはルート）へ移動する
         *
         * params.objectId（必須 string）を uint64_t へ解釈して World から該当 Entity を逆引きする。
         * params.newParentId（任意 string）が無ければ新親 nullptr（World 直下ルートへ移動）、あるのに
         * 逆引きできなければ {accepted:false}。World::ReparentEntity(entity, newParent) の bool を
         * accepted とし、true のときだけ scene.treeChanged を発火する。objectId 欠落 / パース失敗 /
         * GEngine 未生成 / 該当なし / ReparentEntity が false のいずれも {accepted:false}。Entity ポインタや
         * 生ポインタは JsonValue へ一切入れない（live memory 非転送）。適用は呼び出し中の同期・同
         * スレッド文脈（DrainInbound＝ゲームスレッド）でのみ行い、新規スレッド/marshal はしない。
         *
         * @param params リクエスト params（借用、objectId / newParentId を読む）
         * @return {accepted:bool} を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。エンジン状態を変更する（副作用あり）。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        sceneReparentObject(const Norves::Bridge::JsonValue& params) override;

        /**
         * @brief scene.duplicateObject。Entity 部分木を複製して別親（または同一親）へ生成する
         *
         * params.objectId（必須 string）を uint64_t へ解釈して World から複製元 Entity を逆引きする。
         * params.newParentId（任意 string）が無ければ複製元の親（GetParentEntity()）直下へ、ルート
         * 複製元なら World 直下ルートへ複製する（同胞複製）。あるのに逆引きできなければ {accepted:false}。
         * 複製は RuntimeSchemaProjector::BuildEntitySubtreeSnapshot で部分木スナップショットを取り、
         * 関数ローカルの ResourceRegistry + 一時 PrefabAsset へ載せて World::SpawnPrefab で生成する
         * （PrefabRoundTripTest と同経路）。生成できたら accepted:true と新ルートの newId（ObjectId の
         * 10 進文字列）を返し、scene.treeChanged を発火する。objectId 欠落 / パース失敗 / GEngine 未生成 /
         * 該当なし / 一時 prefab 生成失敗 / SpawnPrefab が nullptr のいずれも {accepted:false}。Entity
         * ポインタや生ポインタは JsonValue へ一切入れない（live memory 非転送。snapshot は値、registry /
         * prefab は関数ローカルで SpawnPrefab が同期消費するため寿命は十分）。適用は呼び出し中の同期・
         * 同スレッド文脈（DrainInbound＝ゲームスレッド）でのみ行い、新規スレッド/marshal はしない。
         *
         * @param params リクエスト params（借用、objectId / newParentId を読む）
         * @return {accepted:bool[, newId:string]} を収めた JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。エンジン状態を変更する（副作用あり）。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        sceneDuplicateObject(const Norves::Bridge::JsonValue& params) override;

        // --- Asset（読み取り系） ---

        /**
         * @brief asset.resolve。1 つの論理アセットパスを解決し health/メタを DTO で返す
         *
         * params.logicalPath（必須 string）/ kind（任意 string）/ variant（任意 string）を読む。
         * handler が注入されていない、または retained immutable snapshot が null のときは
         * graceful に {status:"invalidManifest", source:"none", normalizedLogicalPath:<入力>} を返す
         * （not_supported は返さない＝asset.read 広告と整合）。それ以外では handler から snapshot を
         * by-value で借りて寿命を保持し、ResolveAsset を呼んで結果メタを
         * camelCase wire（status/source/normalizedLogicalPath と任意 requiresExplicitLog/fallbackAction/
         * failureKind/reason）へ写す。ResolveAsset は健全性検証（hash mismatch 検出）のため cooked
         * 全バイトを Blob に読むが、Blob / Entry / LoosePath / 生バイトは wire へ一切入れない
         * （DTO のメタ値だけを綴る＝live memory 非転送）。
         *
         * @param params リクエスト params（借用、logicalPath / kind / variant を読む）
         * @return asset.resolve.result の JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。無副作用（エンジン状態を変えない）。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        assetResolve(const Norves::Bridge::JsonValue& params) override;

        /**
         * @brief asset.getManifest。読込済み manifest のスナップショットを DTO 配列で返す
         *
         * params.filter（任意 string）/ page（任意 integer）/ pageSize（任意 integer）を読む。
         * handler が注入されていない、または retained immutable snapshot が null のときは graceful に
         * {version:0, entries:[], totalCount:0} を返す。それ以外では snapshot を by-value で借りて
         * GetAssetCount()/GetAssetReference(index) で各 AssetCookedReference を
         * 列挙して assetEntry（logicalPath/kind 必須、variant/format/sourceHash/cookedPackage/
         * entryName/entryType/cookedHash は非空時、cookedVersion は裸 integer）へ写す。filter は
         * logicalPath 部分一致で絞り、totalCount はフィルタ後・ページング前の件数。Blob / Entry /
         * LoosePath / 生 u64 ハッシュ・u32 EntryType は wire へ入れない（live memory 非転送）。
         *
         * @param params リクエスト params（借用、filter / page / pageSize を読む）
         * @return asset.getManifest.result の JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。無副作用（読み取りのみ）。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        assetGetManifest(const Norves::Bridge::JsonValue& params) override;

        /**
         * @brief asset.reloadManifest。構成済み manifest を同期再読込する
         * @param params SDK が空 object として厳格検証済みの params。
         * @return runtime の受理結果だけを含む {accepted:boolean}。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        assetReloadManifest(const Norves::Bridge::JsonValue& params) override;

        // --- Viewport ---

        /**
         * @brief viewport.getThumbnail。最新キャプチャ、キャッシュ、または 1x1 PNG placeholder を返す
         *
         * 完了済みフレームがあれば非同期読み戻し済みデータだけを消費し、Core の PNG thumbnail
         * encoder と Bridge の base64 helper で schema 準拠の成功応答を返す。フレーム未到着、
         * pending、未初期化、GEngine null、キャプチャ/エンコード失敗、params 不正はいずれも
         * BridgeError にせず、cap に収まる直近キャッシュまたは placeholder を返す。
         *
         * @param params リクエスト params（借用、maxWidth/maxHeight を lenient に読む）
         * @return {imageBase64,mimeType,width,height} を収めた成功 JsonValue
         * @note ゲームスレッド上から逐次呼ばれる。GPU 待機・sleep・poll は行わない。
         */
        Norves::Bridge::Result<Norves::Bridge::JsonValue, Norves::Bridge::BridgeError>
        viewportGetThumbnail(const Norves::Bridge::JsonValue& params) override;

    private:
        /**
         * @brief セッション id 採番用の単調カウンタ
         *
         * @note すべてのコールバックはゲームスレッド上で逐次実行されるため、単純な
         *       カウンタで十分（同時呼び出しなし）。
         */
        uint64_t m_NextSessionSeq = 0;

        /**
         * @brief サブスクリプション id 採番用の単調カウンタ
         *
         * @note すべてのコールバックはゲームスレッド上で逐次実行されるため、単純な
         *       カウンタで十分（同時呼び出しなし）。
         */
        uint64_t m_NextSubscriptionSeq = 0;

        /**
         * @brief 実エンジン状態アクセス用の借用ポインタ（非所有）
         *
         * @note SetHandler で注入され、すべてのコールバックはゲームスレッド上で逐次実行される。
         */
        Game::GameApplicationHandler* m_Handler = nullptr;

        /**
         * @brief サーバー発イベント発火用の host への借用ポインタ（非所有）
         *
         * @note SetHost で注入され、EmitEvent / Start・StopLogForwarding はゲームスレッド上から呼ばれる。
         */
        Game::Bridge::BridgeServerHost* m_Host = nullptr;

        std::string m_CachedViewportThumbnailBase64;
        uint32_t m_CachedViewportThumbnailWidth = 0;
        uint32_t m_CachedViewportThumbnailHeight = 0;
        bool m_bHasCachedViewportThumbnail = false;

#if defined(NORVES_BRIDGE_TESTING)
        ViewportThumbnailCaptureProviderForTesting m_ViewportThumbnailCaptureProviderForTesting{};
#endif // NORVES_BRIDGE_TESTING
    };

} // namespace Game::Bridge

#else // NORVES_BRIDGE_ENABLED

namespace Game::Bridge
{

    /**
     * @brief SDK 非設定ビルド用の不活性スタブ。SDK 型に一切依存しない空クラス。
     *
     * BridgeServerHost のスタブ Start() は adapter を無視するため、これは
     * GameApplicationHandler のメンバとして完全型を提供する目的だけを持つ。
     */
    class NorvesLibBridgeAdapter
    {
    public:
        NorvesLibBridgeAdapter() = default;
        ~NorvesLibBridgeAdapter() = default;
    };

} // namespace Game::Bridge

#endif // NORVES_BRIDGE_ENABLED
