# GR84: クリップ変換とルート移動の保存

## 入口

BVHとglTF/GLBのアニメーションを、指定した作者骨格へ変換して独立v1 ClipBankに保存する。

```text
AssetCook --retarget-clip --input run.bvh --skeleton dog.gltf --role-profile roles.json --out RuntimeRun --logical Animations/Run --clip-name Run
```

出力は新規directoryのみ。既存directoryを上書きしない。入力はUnicode対応、出力は管理cookと同じASCII path制限を持つ。既存runtimeへの追加・更新はasset-set v2のanimation項目に次を指定する。

- `skeleton_path`: source root相対のターゲットglTF/GLB
- `role_profile`: source root相対のRole Profile JSON
- `source_clip`: glTF内のクリップ名。省略時は全クリップ。BVHでは指定しない
- `clip_name`: 出力クリップ名。変換結果が1本の場合だけ指定できる

出力formatは`nvskel.v1.clips`。対象骨格とprofile、source/targetの外部bufferとsidecarのbytes・有無を既存の中央増分判定に含める。

## Role Profile v1の任意設定

既存の明示C方式を保持し、次を追加する。`match`/`align_bones`ではtarget roleの`C`を省略する。

```json
{
  "rest_pose": {"mode":"align_bones", "up_hint":[0,1,0], "maximum_error_degrees":5},
  "root_scale":1,
  "processing": {
    "output_fps":30, "loop_mode":"auto",
    "minimum_period":0.15, "maximum_period":3,
    "extract_root_motion":true, "exclude_roles":["tail"]
  }
}
```

- rest mode: `explicit`は既存C、`match`は骨方向の許容差超過を拒否、`align_bones`は最小swingと定義できるup-hint twistからCを求める。方向が作れない場合は明示Cを使う
- root scale: 正の倍率、または`auto_height`。後者はmapped pelvis（未指定時root）とpawの高さ比を使い、pawがなければ拒否する
- loop mode: `none`は全期間、`range`は`start_seconds`/`end_seconds`、`auto`は十分な反復がある場合に周期を検出する。検出できない場合は全期間を保持する
- glTFの時刻は作者の秒を使う。BVHのfps上書きをglTFに指定すると拒否する
- 旧NVSKEL 0.2入口は新設定を黙って無視せず拒否する

## 処理と境界

sourceのlocal quaternionをSLERP/STEP評価してからFKし、作者restとの差分をターゲット作者restへ写す。IBMやt=0をrestと見なさない。一様scaleと静的RootFrameを扱う。非rootの移動channelは保持せず件数を報告し、restと異なるアニメーションscaleは拒否する。

周期はin-place姿勢の重み付き角度差で探し、格子間の周期も精密化する。重みは下流の骨長、除外roleは0。開始点には姿勢と角速度の差を使い、選んだ周期の残差を全区間へ分散する。最終keyは正確な期間末尾へ置き、bodyの先頭/末尾を一致させる。rootの上下動はbodyに残す。

根の平面XZとworld +Yのtwistを抽出し、開始headingをbind方向へ正規化する。twistが定義できない姿勢は拒否する。移動軌跡は閉じない。区間中に180度以上回るほど疎な入力の旋回回数は一意に復元できないため、必要な時間密度で作者側から出力する。

v1 bankはターゲットの作者rest/frameを保持するため、既存の束縛時rest差検査を通る。自動retargetはこの入口で明示的に行う操作であり、通常の束縛が古いクリップを黙って補正することはない。

## RMTN任意節

profile 2/3だけで使用する。旧クリップは節を出さず既存wireを保持する。各40byte recordはclip index u32、root joint u32、time f32、reserved u32=0、XZ/yawの各f64。先頭time/変位/yawは0、末尾timeはclip duration、時刻は厳密増加。root indexはcanonical化・束縛で対応付ける。sample数は共通予算へ計上する。ANLYは軌跡の要約であり、軌跡そのものの代用ではない。

この段階は抽出・保存まで。再生時の経路差分適用・ループ跨ぎ・controllerへの受渡しはG3/G4で行う。

## 検証

既存SkeletalBvhClipImportTestに16→30fps・11.6frame周期・非周期・端点・巨大frame原点・yawのケース、RigStaticRootFrameWireTestにRMTN往復/拒否、RigSplitCookTestにBVH/glTF共通変換とasset-set→CPUロード、AssetCookRigSplitSmoke.pyに実CLIの新規公開/拒否を追加した。native実行結果はPROGRESS.mdに記録する。GPU/DCC/実犬での見た目はこの合成CPU検証に含めない。
