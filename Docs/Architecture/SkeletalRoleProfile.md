# ロール経由の骨格Profile v1（GR84）

## 設定と語彙

自己完結のUTF-8 JSON bytesをCookGltfWithRoleProfileToNvskelNativePathへ渡す。先頭UTF-8 BOMは許容する。versionは整数1、vocabularyはquadruped_v1。未知field/role、重複key、型違い、未対応の自動指定は拒否する。

語彙と正準展開順は次の23role。rootは実階層rootを指す技術的なroleであり、pelvisと同じという仮定ではない。

1. root、pelvis
2. spine、neck、head、jaw、tail
3. front_L_clavicle、front_L_upper、front_L_lower、front_L_paw
4. front_R_clavicle、front_R_upper、front_R_lower、front_R_paw
5. hind_L_thigh、hind_L_shin、hind_L_hock、hind_L_paw
6. hind_R_thigh、hind_R_shin、hind_R_hock、hind_R_paw

spine/neck/tailだけがchain。それ以外は単一role。綴りとL/Rの大小文字は固定し、aliasやUnicode正規化は行わない。root以外をゲームの必須集合にしない。

```json
{
  "version": 1,
  "vocabulary": "quadruped_v1",
  "axes": {"up": "+Y", "forward": "+Z", "handedness": "right"},
  "units": {"position_scale": 1},
  "position_convention": "additive",
  "time": {"mode": "header_frame_time"},
  "source_roles": {"root": ["Source"]},
  "target_roles": {
    "root": [{"joint": "Root", "C": [1,0,0,0,1,0,0,0,1]}]
  }
}
```

source_rolesはroleごとの非空joint名配列。target_rolesはjoint/Cの非空要素配列であり、ここにあるroleだけを写像する。両表のrootは必須・1要素。単一roleは1要素、chainは利用者が近位から遠位へ明示し、写像する両側の長さは同じでなければならない。chainの解剖学的正しさを名前から推測しない。

required_rolesは任意の、重複しないrole名配列。指定roleはsourceとtargetの双方に必要。sourceだけのroleも記載名が実BVHに存在することを検査し、SourceOnlyRolesへ所有報告する。source-only aliasは活動pairの再利用とは区別する。活動pairはsource再利用とtarget重複を拒否する。root/pelvisが同じ実jointなら両方を活動pairにしない。

Cはcanonical列ベクトル空間の補正3×3を行順の9有限doubleで明示する。恒等も省略しない。既存のproper rotation許容検査へ通し、式W=C D C^T Bで使う。自動rest補正や元BVH骨方向との無条件一致を意味しない。

axesはup/forwardに±X/±Y/±Z、handednessにright/leftを指定。平行なup/forwardは拒否。position_scaleは正有限、position_conventionはadditive/absolute。time.modeはheader_frame_time（source_fps禁止）かoverride_fps（正有限source_fps必須）で、推測fpsを置かない。heading保持・回転のみ・全sample保持・source再利用Rejectはこのadapterの固定規約。

clip名とAdd/Replaceは別の型付き呼出要求。Profileで暗黙のclip選択やrenameをしない。

## 所有、制限、hash

parse結果、具体名、C、正準順のExpandedRoles、非対象source role、元Profile byte数/hashを所有する。呼出成功時だけ以前の結果を置換し、失敗/確保例外では保持する。
JSON DOM確保前に入力byte数、構造深さ、構造token数（引用符外のopen bracket/colon/comma）を制限する。既定は1MiB、深さ16（hard上限64）、32768token。source要素/展開数は既定各1024、単名4096bytes、名前合計1MiB。制限は全処理RSSの上限ではない。
source-only検査の所有BVH decodeをbridge前に破棄する。現在は検証と実cookのためBVHを2回decodeする。型付きbridgeの既存予算も別途適用し、線形時間/無確保とは称さない。

既存BVH typed hashに、Profile parser版、語彙版、長さ付きraw Profile bytes、Profile limitsを追加する。JSON object順の違いは正準展開後のNVSKEL bytesを変えないが、元入力identityのhashは変える。旧Profile無し・BVH無し経路は変更しない。
file/CLI、依存ファイルの探索、cache skipはまだ提供しない。この入口は毎回cookする。後続file入口ではBVH/Profileもfingerprint・snapshot・出力alias保護・公開前再採取へ同時に接続する。

## 受入れ境界

合成Profileから既存NVSKEL/package/AssetSystem/実Resource/Samplerへ接続するCPU契約を対象にする。異なる実joint名、正準role順、明示非可換C、未写像targetのbind保持、拒否と所有、入力identityを確認する。
語彙IDはゲームの骨格採用や骨格共有の決定ではない。自動C、異長chainの分配、IK、root motion、再サンプル、Stage Bの作者時rest snapshotと再束縛、実Blender/GPU品質は別の機能である。
