# 荒野の正解の絵の比較（SCN-WLR-*、計画は Docs/Plans/WastelandRebuild.md）で、保存した撮影の結果を検査する。
# 重い描画（エンジンの撮影の組・Cycles）は反復の中で行って結果を保存し、verify はこのスクリプトで保存した結果を検査する。
#
# 比較の約束（全 SCN-WLR-* 共通）:
#   値は線形の Rec.709 の原色・白色点 D65、輝度 Y = 0.2126R + 0.7152G + 0.0722B、放射輝度は cd/m²
#   （エンジンのプリエクスポージャを外した値）。測光用の HDR はブルームの前で読む。
#
# 使い方:
#   python Scripts/Terrain/ReferenceCheck.py --record-inputs --kind engine --capture <出力先> [--game-exe <Game.exe>]
#       撮影の入力のハッシュを <出力先>/inputs.json に書く（CaptureScene.ps1 -Hdr が撮り終えたときに呼ぶ）。
#       --game-exe を与えると、撮影に使った実行ファイルの中身の SHA-256 も書く（ソースのハッシュは撮影の後のファイルから
#       作るので、実際に走ったコードが同じかはこちらで照らす）。
#   python Scripts/Terrain/ReferenceCheck.py --hdr --capture <出力先> [--views a,b,...] [--verify]
#       CaptureScene.ps1 -Hdr の撮影（<視点>.png・<視点>.hdr.pfm・<視点>.hdr.json・metrics.json）を視点ごとに検査し、
#       結果を <出力先>/hdr_check.json に書く。--views を省くと metrics.json の全視点を見る。
#       --verify は入力のハッシュ（inputs.json）を今のファイルと照らし、違えば古い結果として失敗にする。
#
# 入力のハッシュ（inputs.json）: 種類ごとに決めたパス（INPUT_SETS）の、git が追跡するか無視されていないファイルの中身の SHA-256 を
# パスの順に並べたものの SHA-256。荒野の撮影では描画のコード・シェーダー・荒野の組み立て・視点の定義・資産の組の定義・撮影の台本・
# 共通の照明（Scripts/Terrain/CommonLight.json。無ければ無いことを記録）。資産の組（ASSET_SET_INPUTS）が参照するメッシュ・テクスチャの
# 実体（glTF の .bin・画像を含む）は git が無視していても中身をハッシュに入れる（無ければ無いことを記録）。
# git のコミットと、入力に未コミットの変更があったかも書く
# （記録のためで、照合には中身のハッシュを使う。撮影の後にコミットしても古い結果にはならない）。
#
# --hdr の検査（視点ごと）:
#   1. 大きさ: PFM の見出し・hdr.json・PNG の幅と高さが一致する。
#   2. 値の範囲: R・G・B に負・NaN・Inf が無い。輝度の最大が正（真っ黒でない）。輝度の分位（1・50・99 %）を書く。
#   3. PNG との照合: HDR にプリエクスポージャを掛けて、エンジンの表示変換（DisplayTransform.py。撮影が記録したポストのパスの値、
#      記録の無い古い撮影は荒野のカメラの差し替えの値）を通した 8 bit の値を、同じ視点の PNG と比べる（DisplayTransform.py が
#      再現していない処理と、ブルームに乗るレンズダートが掛かる撮影は見積もれないので失敗にする）。比べる画素は次をすべて満たすものだけ:
#        - 暗い画素: 露出後の最大の成分が DARK_PEAK 未満（NeutralLinear が明部を圧縮し始める 0.8 の手前で、
#          トーンマップが値をそのまま返す範囲。荒野の日なたの地面は露出後 0.5〜0.75 で、0.4 未満の画素は影の中の
#          縁と細かな模様にほぼ限られ、ブルームと FXAA を除くと数が足りない。0.4 未満だけの一致も記録する）
#        - ブルームの影響が小さい: 露出後の値と、その周り（ガウスの半径 BLOOM_SIGMAS 画素）の平均との差に、ブルームの混ぜ具合
#          （SceneView のブルーム 4 %）を掛けた見積もりが、BLOOM_RELATIVE × 値 + BLOOM_ABSOLUTE 以下
#        - 縁でない: PNG と HDR から作った値の 3×3 の輝度の幅が EDGE_LEVELS 段以下（FXAA が縁を混ぜるため）
#      合否: 比べる画素が MIN_SAMPLE_RATIO 以上あり、3 成分とも ±WITHIN_LEVELS 段に入る画素が WITHIN_RATIO 以上、
#      差の絶対値の中央値が MEDIAN_LEVELS 段以下。表示変換の厳密な照合（1 段以内）は SCN-WLR-DISPLAY-TRANSFORM で行う。
#      ブルームの前であることの確かめとして、ブルームの影響が大きい暗い画素の PNG − 見積もりの平均（正なら PNG にだけブルームが乗る）も書く。
#
#   python Scripts/Terrain/ReferenceCheck.py --display --capture <出力先> [--verify]
#       表示変換の再現（DisplayTransform.py）を、Game の --display-check（ブルームと FXAA を外す）で撮った 3 組と照らし、
#       結果を <出力先>/display_check.json に書く。--verify は組ごとの inputs.json を今のファイルと照らす。
#       <出力先>/ramp-neutral: 荒野（カメラの差し替えの NeutralLinear とグレーディングの式 1）を --display-check=ramp で撮った 1 視点
#       <出力先>/ramp-aces: 差し替えの無いカメラのシーンを --tone-map=aces20-lut --display-check=ramp で撮った 1 視点
#           どちらもトーンマップの入力が色の階段（DisplayTransform.display_ramp。明るさ 12 段 × R・G・B・C・M・Y・灰）になる。
#           区画の縁から RAMP_BORDER 画素より内側のすべての画素で、PNG と再現の差が 3 成分とも RAMP_WITHIN_LEVELS 段以内なら合格。
#       <出力先>/scene: 荒野の SCN-WLR-BASELINE と同じ 8 視点を --display-check=scene -Hdr で撮ったもの。
#           視点ごとに、HDR に表示変換を掛けた 8 bit の値と PNG の差が 3 成分とも SCENE_WITHIN_LEVELS 段以内の画素が
#           SCENE_WITHIN_RATIO 以上なら合格。
#       どの組も、撮影の記録（hdr.json の post）でブルームと FXAA が外れ、色の階段の有無と演算子が組の約束どおりであることも見る。
#
#   python Scripts/Terrain/ReferenceCheck.py --plate --capture <出力先> [--verify]
#       共通の照明（Scripts/Terrain/CommonLight.json）の灰色の板の明るさを、入力だけから求めた期待の値と照らし、
#       結果を <出力先>/plate_check.json に書く。--verify は組ごとの inputs.json を今のファイルと照らす。
#       <出力先>/sun・<出力先>/sky: Game の --wasteland-common-light --wasteland-gray-plate --common-light-case=sun|sky を
#           CaptureScene.ps1 -Hdr で撮ったもの（metrics.json の game_args と、Game のログの WASTELAND_COMMON_LIGHT・
#           WASTELAND_GRAY_PLATE の行で、その指定で撮れたことを確かめる）。
#       期待の値（エンジンが計算した照度は使わない）: 板はアルベド ρ の鏡面の無い水平の面なので、
#           sun: ρ × E⊥ × cos(天頂角) / π（E⊥ は太陽に垂直な面の照度、cos(天頂角) = sin(視点の太陽の仰角)）
#           sky: ρ × 空の放射輝度（一様な空の下の水平のランバート面の放射輝度）
#       合否: 視点ごとに、画面の中心の PLATE_WINDOW（幅・高さに対する割合）の窓の HDR の輝度の平均が期待に PLATE_TOLERANCE 以内、
#           かつ窓の中の輝度の変動係数（標準偏差 / 平均）が PLATE_UNIFORMITY 以下（窓が板の中に収まり、影・ほかの物が入らない）。
#           R・G・B ごとの比も書く（sky は空の色の比が保たれるか）。
#
#   python Scripts/Terrain/ReferenceCheck.py --aov --capture <出力先> [--views a,b,...] [--verify]
#       CaptureScene.ps1 -Aov の撮影（<視点>.aov.json と、そこに書かれたアルベド・法線・粗さ・深度の PFM）を視点ごとに検査し、
#       結果を <出力先>/aov_check.json に書く。--verify は入力のハッシュ（inputs.json）を今のファイルと照らす。
#       値の約束: アルベドは GBuffer の線形の値、法線はワールド空間（+Y が上）の GBuffer の値のまま、深度はカメラの前方向への
#       距離 m（視線方向の深度）、空の画素は深度が +∞ で、ほかの値は 0。
#       1. 法線: 長さが 1 ± AOV_NORMAL_LENGTH_TOLERANCE の画素が AOV_NORMAL_LENGTH_RATIO 以上、カメラに向く（視線と逆向きの成分が正）
#          画素が AOV_NORMAL_FACING_RATIO 以上、y の平均が AOV_NORMAL_MEAN_Y より大きい（+Y が上）。
#       2. 深度: 表面は有限でカメラの近い面より奥、空は +∞。aov.json のカメラで位置に戻し、縦に隣り合う表面の画素の組で、
#          上の画素の水平の距離が下より縮む組（許容 AOV_MONOTONIC_RELATIVE × 距離 + AOV_MONOTONIC_ABSOLUTE）が
#          AOV_MONOTONIC_MAX_INVERSIONS 以下（高さ場の地形は、同じ縦の面の中で上の光線ほど遠くに当たる。前に立つ物・宙の物で崩れる）。
#          列ごとのいちばん下の空より上にある表面が AOV_MAX_SURFACE_ABOVE_SKY 以下。
#          深度から作った面の向き（隣の画素の位置の差の外積）と GBuffer の法線の角度の中央値が AOV_DEPTH_NORMAL_MEDIAN_DEG 以下
#          （深度の単位・軸とカメラの記録の確かめ。法線マップの細部の分だけずれる）。
#       3. アルベドと粗さ: 0〜1 に入り、アルベドの輝度の 1〜99 % が AOV_ALBEDO_Y_RANGE（自然の地面の範囲）に収まる。
#       4. 起動引数が --wasteland-hide=all（7 つすべて）のとき: 2 の縮む組・空より上の表面が AOV_BARE_MAX_INVERSIONS・
#          AOV_BARE_MAX_SURFACE_ABOVE_SKY 以下（高さ場だけなら稜線の縁の数画素しか崩れない）。Game のログに WASTELAND_HIDE mask=127 があり、草・石を
#          渡した行（WASTELAND_FOLIAGE_PUBLISHED）がどれも batches=0、建物（WASTELAND_BUILDING）・崖錐の岩（WASTELAND_TALUS）の
#          行が無い。共通の照明（--wasteland-common-light）なら、空の画素の HDR が CommonLight.json の一様な空の放射輝度に
#          ±AOV_SKY_TOLERANCE で合う画素が AOV_SKY_RATIO 以上（雲・霧・霞が空に乗らない）。
#
#   python Scripts/Terrain/ReferenceCheck.py --projection --capture <出力先> [--views a,b,...] [--verify]
#       エンジンの深度（CaptureScene.ps1 -Aov を --wasteland-common-light・--wasteland-hide=all・--wasteland-finest-lod で撮った
#       <視点>.aov.depth.pfm）と、同じカメラで Blender の Cycles が 1 サンプルで描いた深度（BlenderReference.py --depth が
#       <出力先>/blender/ に書いたもの）を並べ、結果を <出力先>/projection_check.json に、輪郭の重なりの画像を
#       <出力先>/projection_<視点>.png（エンジンの輪郭が赤、Blender が青緑、重なりが白）に書く。--views を省くと手本の 6 視点。
#       --verify は撮影の inputs.json と、Blender の結果の blender/inputs.json（台本・書き出しの一覧と読んだ PLY の中身・
#       地形のメッシュの元の入力 terrain_mesh のハッシュ）を今のファイルと照らす。
#       1. カメラ: エンジンの aov.json のカメラと、Blender が置いたカメラの記録が一致する（位置 PROJECTION_CAMERA_POSITION_M、
#          向き PROJECTION_CAMERA_DIRECTION、縦の画角 PROJECTION_CAMERA_FOV_DEG 以内、縦横比と画素数が同じ、透視）。
#       2. 崖の輪郭と稜線: 深度の輪郭（空と表面の境の表面の側と、隣の画素との深度の比が 1 + PROJECTION_JUMP_STRONG を超えて跳ぶ
#          組の手前の側）の画素から、相手の画像の輪郭（閾値の際での取りこぼしを避けて 1 + PROJECTION_JUMP_WEAK）の画素までの
#          距離が PROJECTION_RADIUS_PX 画素以内のものが、両方向とも PROJECTION_EDGE_RATIO 以上。
#          列ごとのいちばん上の表面の画素（空との稜線）から相手の空との境の画素までの距離が PROJECTION_RADIUS_PX 画素以内のものが、
#          両方向とも PROJECTION_SKYLINE_RATIO 以上（行の差で測ると、ほぼ縦に走る崖の側面の稜線で横 1 画素のずれが何十画素にもなる）。
#          空と表面の分かれ方が食い違う画素が PROJECTION_SKY_MISMATCH 以下。
#       3. 深度の値: 輪郭から PROJECTION_DEPTH_EDGE_MARGIN 画素より離れた共通の表面で、相対差の中央値が PROJECTION_DEPTH_MEDIAN、
#          95 % が PROJECTION_DEPTH_P95 以下（単位・軸・原点の取り違えと、エンジンが粗い段で描いた所を見つける）。
#       4. 撮影の起動引数と Game のログに、共通の照明・地形だけ（WASTELAND_HIDE mask=127）・最も細かい段（WASTELAND_FINEST_LOD）がある。
#
#   python Scripts/Terrain/ReferenceCheck.py --atmos-dump --capture <出力先> [--views a,b,...] [--verify]
#       CaptureScene.ps1 -GameArgs atmosphere-dump の撮影（<視点>.atmos.json と、そこに書かれた 32 bit 浮動小数の生のファイル）を
#       視点ごとに検査し、結果を <出力先>/atmos_dump_check.json に書く。--verify は入力のハッシュ（inputs.json）を今のファイルと照らす。
#       書き出しの中身: 空の透過率 LUT・空の放射輝度 LUT（GPU へ上げた半精度の値）、透過率の表（光学的深さ）・多重散乱の表・
#       sky-view の表（CPU の前計算）、決めた視線（太陽からの方位差 4 つ × 仰角 × 距離、地表への視線 × 距離）の高さフォグと
#       空気遠近（GPU が volumetrics.frag と同じ式で評価した、事前露出の前の値）、地表の太陽と空の照度（lux）。
#       1. 揃い: 6 つのファイルが JSON の形どおりにあり、NaN・Inf・負（視線の値を除く）が無く、単位の記述が約束どおり。
#       2. 視線: 方位 4 つ、仰角 ATMOS_ELEVATION_RANGE、距離 ATMOS_DISTANCE_RANGE を覆う格子と、地表（カメラの下
#          ground_eye_height_m の水平面）への視線が同じ距離を覆う。
#       3. 値の範囲: 透過率は 0〜1、散乱光は 0 以上。同じ向きで距離が伸びると、透過率は増えず空気遠近の散乱光は減らない。
#       4. エンジンの値どうしが揃う（書き出しが描画に使われた値そのものであることの確かめ）:
#          a. 透過率 LUT が透過率の表を CPU と同じ引き方（高さは √、余弦は x|x| の並び）で引いた値に ATMOS_HALF_TOLERANCE で合う。
#          b. 空の放射輝度 LUT が sky-view の表を CPU と同じ引き方で引いた値（半精度へ上げるときの上限 ATMOS_LUT_CLAMP で
#             頭打ちにした値）に ATMOS_HALF_TOLERANCE で合う。頭打ちになった texel の数と、頭打ちの前の最大を記録する。
#          c. GPU が視線の向きで引いた空の放射輝度（視線の値の行 5）が、書き出した LUT を GPU の線形の標本で引いた値に
#             ATMOS_SAMPLE_TOLERANCE で合う（書き出した LUT が GPU の LUT と同じ）。
#          d. 高さフォグの透過率が、書き出した定数で volumetrics.frag の式を求めた値に ATMOS_FOG_T_TOLERANCE で合う。
#          e. 空気遠近の透過率と散乱光が、書き出した定数と LUT で volumetrics.frag の式を求めた値に
#             ATMOS_AERIAL_T_TOLERANCE・ATMOS_SAMPLE_TOLERANCE で合う。
#          f. 合成の値（行 6）が、高さフォグの色 × 不透明度 + 太陽の単一散乱 + 空気遠近の散乱光 × 高さフォグの透過率に合う。
#          g. 地表の太陽の照度 = 大気の外の照度 × 地表の透過率（水平面は × sin(高度)）、エンジンの太陽の方向光の照度と同じ、
#             地表の透過率が透過率の表を引いた値に ATMOS_SUN_TABLE_TOLERANCE で合う、空の照度が正。
#       撮影の起動引数に --atmosphere-dump があり、Game のログに書き出しのパス（ATMOSPHERE_DUMP）と保存の行がある。
#
#   python Scripts/Terrain/ReferenceCheck.py --atmos-ref --capture <出力先> [--views a,b,...] [--verify]
#       --atmos-dump の書き出し（省くと ref_ground_02）を、AtmosphereReference.py の独立な正解（Hillaire 2020 の定義から数値積分。
#       多重散乱は論文の近似 A と、各回で位相関数を使い方向別の放射輝度を回数で足す B）と比べ、<出力先>/atmos_ref_check.json に書く。比べるもの: 空の方向別の輝度（設定を
#       1 つずつ論文へ戻した正解も）、地表の太陽と空の照度、地表への視線と水平の視線の距離別の透過率と散乱光（エンジンと同じ媒質の
#       正解と、空の大気そのままの正解）、高さフォグと空気遠近の光学的深さと散乱光の内訳、多重散乱の表、写真 ground_02 の空の色度。
#       合否: 入力のハッシュが今のファイルと合う（--verify）、AtmosphereReference.py --verify の結果が合格で同じ設定、同じ式で求まる値
#       （地表の太陽の透過率、同じ媒質の高さフォグと空気遠近の透過率）が正解と ATMOS_REF_SAME_MODEL_TOLERANCE で合う。
#       散乱光と空の輝度の差は記録するだけ（値は変えない）。
#
#   python Scripts/Terrain/ReferenceCheck.py --blur-static --capture <出力先> [--verify]
#       遠景のぼけを、条件を 1 つずつ変えた静止の撮影（ref_far_01・ref_cliff_02）で切り分け、<出力先>/blur_static_check.json と、
#       視点ごとの切り出しの並び <出力先>/blur_<視点>.png を書く。組は <出力先> の下のフォルダ（BLUR_REFERENCE・BLUR_CASES）。
#       正解: 同じ視点を 2 倍の解像度・AA なし（-Aov 付き）で撮り、線形の光で 2×2 を平均して縮めた画（超標本化）。
#       範囲: 正解の深度が BLUR_FAR_MIN_M より奥の地表（遠景）と、そのうち法線の |y| が BLUR_CLIFF_NORMAL_Y 未満の面（崖）。
#       測り方: 表示の値（PNG の符号化のまま）の輝度を、隣り合うガウスの差の帯域（BLUR_SIGMAS）に分け、帯域ごとに
#         細部の残り = <条件, 正解> / <正解, 正解>（条件の帯域を正解の帯域へ射影した利得。1 − 残り = 細部の欠け）
#         エイリアス = |条件 − 残り × 正解|² / |正解|²（正解に無い成分。鮮明さには数えない）
#       を範囲の画素で求める。細かい帯域（BLUR_FINE_BANDS）は正解の量で重みを付けてまとめる。霞は正解との比ではなく、
#       霞あり ÷ 霞なしの帯域の量（コントラストの低下）で記録する。比べる組は正解と同じジオメトリの取り分（BLUR_SHARE_ARG）で撮り、
#       条件ごとの効きは、取り分だけを広げた組（BLUR_BASELINE）との差の 2 視点の平均（base との差は既定の取り分の効き）。
#       合否: 全部の組があり（--verify は入力のハッシュが今のファイルと合い、組どうしで揃う）、起動引数とログの印が組の約束どおり、
#       仮想テクスチャを使っていない（使っていれば常駐の確かめが要るので失敗）、base 以外は撮った時点でジオメトリのページの
#       進行中（要求・読み込み・書き込み待ち・書き込み）が 0 で、落ち着きの基準から保存まで追い出しが無い（base は記録だけ）、
#       範囲が BLUR_MIN_REGION_PX 画素以上、較正（正解をぼかした画が細部の欠けに、雑音を足した画がエイリアスに数えられる）が通る。
#       欠けとエイリアスの大きさは記録するだけ（既定は変えない）。
#
#   python Scripts/Terrain/ReferenceCheck.py --blur-motion --capture <出力先> [--verify]
#       遠景のぼけを、カメラを一定の速さで振りながら撮った画と、同じ向きで止めて撮った画で切り分け、<出力先>/blur_motion_check.json と
#       切り出しの並び <出力先>/blur_motion_<視点>.png を書く。撮影は MOTION_VIEW を台本 MOTION_SCRIPT_ARG で振る 1 回の起動で、
#       台本が TAA の履歴を消し（エポック）、k フレーム後（MOTION_OFFSETS）を振りの途中（motion-k）と止めて（static-k）撮り、
#       --capture-views が止めて 60 フレーム後（static-60。--blur-static と同じ撮り方）を撮る。組は MOTION_GROUPS（正解・FXAA・TAA は
#       <出力先> の下のフォルダ、base は <出力先> そのもの。base は記録だけ）。測り方は --blur-static と同じで、正解は同じ名前の
#       2 倍の解像度・AA なしの撮影。静止との差は同じ k どうしの（振り − 止め）、履歴による細部の消失は TAA と FXAA の差の差。
#       合否: 全部の組があり（--verify は入力のハッシュが今のファイルと合い、組どうしで揃う）、起動引数とログの印が約束どおり、
#       台本の撮影がすべて約束のエポックからのフレーム数で撮れ、撮れたパケットの番号との差が一定（フレームが飛んでいない）、
#       振りと止めの同じ k が同じ向きで基準の視点に MOTION_YAW_TOLERANCE 以内、motion-30-pre が約束の速さだけ手前、画のずれ
#       （位相相関）が振りと止めの間で MOTION_ALIGN_MAX_PX 未満、motion-30-pre → motion-30 が振りの角の画素数と合う、
#       base 以外は撮影の間のページの進行中・追い出し・描画の溢れが無い、較正が通る。差の大きさは記録するだけ（既定は変えない）。
#
#   python Scripts/Terrain/ReferenceCheck.py --gray --capture <出力先> [--views a,b,...] [--verify]
#       灰色の材質（CommonLight.json の gray_material）の地形を、エンジン（<出力先> の撮影。共通の照明・地形だけ・最細の LOD・
#       --wasteland-gray-material）と Cycles（BlenderReference.py --gray が <出力先>/cycles/ に書いたもの）で比べ、
#       <出力先>/gray_check.json と見比べの画像 <出力先>/gray_<視点>.png を書く。--views を省くと GRAY_VIEWS。
#       原因の見立て用に、同じ起動引数に --common-light-case=sun|sky を足した撮影を <出力先>/engine-sun・engine-sky に置く。
#       領域と収束の定義は GrayRegions.py（日なたの地面・影の中の地面・日なたの崖・影の崖・遠景）。
#       1. エンジンの撮影（3 組とも）: 起動引数とログの印、撮った時点のジオメトリのページの進行中 0・追い出しなし、描くクラスタが
#          溢れていない。太陽と空の両方の撮影の AOV のアルベドが灰色の ±GRAY_ALBEDO_TOLERANCE（8 bit の丸めを許す）、粗さが一致。
#       2. Cycles 側の尺度の較正: 灰色の板の sun・sky が、入力だけから求めた期待（--plate と同じ式・窓）に ±3 % で合う。
#       3. 視点ごと: Cycles の出力が記録のハッシュと合い、カメラがエンジンの aov.json と一致、空と表面の分かれ方の食い違いが
#          GRAY_SKY_MISMATCH 以下、Cycles の直接光 + 間接光・太陽の組 + 空の組が合成の ±GRAY_SPLIT_TOLERANCE。
#          乱数の種を変えた 2 本（A・B）から領域ごとの収束を測り直し、収束した領域を「判定」、しない領域を「未判定」と書く。
#          領域ごとに、エンジンと Cycles の線形の平均（輝度・RGB）と比、Cycles の内訳（太陽・空・直接・間接）、
#          エンジンの太陽だけ・空だけの撮影と Cycles の太陽の組・空の組の比を書く（比の大きさは記録するだけ）。
#       --verify は 3 組の撮影の inputs.json と、cycles/inputs.json（台本・GrayRegions.py・CommonLight.json・PLY・地形のメッシュの
#       元の入力・エンジンの撮影の入力のハッシュ）を今のファイルと照らす。
#
#   python Scripts/Terrain/ReferenceCheck.py --lit --capture <出力先> [--views a,b,...] [--verify]
#       実材質の地形を、エンジン（<出力先> の撮影。共通の照明・地形だけ・最細の LOD）と Cycles（BlenderReference.py --lit が
#       <出力先>/material/ に画素ごとの材質、<出力先>/cycles/ に照明つきの描画を書いたもの）で比べ、<出力先>/lit_check.json と
#       表示変換を掛けた比較画像 <出力先>/lit_<視点>.png（エンジン｜Cycles。エンジンの撮影の hdr.json の表示変換を両方に掛け、
#       Cycles の空は一様な空で埋める）と、領域と比の画像 <出力先>/lit_regions_<視点>.png を書く。--views を省くと LIT_VIEWS。
#       手本の写真とも並べ、写真｜エンジン｜Cycles の <出力先>/lit_photo_<視点>.png と全視点の一覧 lit_photo_overview.png を書き、
#       写真の崖・地面の枠（CompareReference.py の PHOTOS）の表示の値と、露出に依らない崖 ÷ 地面の輝度の比・色の比を記録する
#       （写真とは照明・天気・現像が違うので合否に使わない。写真が無い視点は不合格）。
#       合否: エンジンの撮影の起動引数とログの印・撮った時点のページの常駐（--albedo と同じ）、画素ごとの材質の入力（--albedo と
#       同じ値の表・OSL・ミップの段）と標本の位置のずれが NORMAL_SUBPIXEL_MAX_PX 以下、Cycles の出力のハッシュ・カメラ（ずらしは
#       材質と同じ）・空の分かれ方・光の内訳の和（--gray と同じ）。領域と収束は --gray と同じ定義（1 本あたり
#       LIT_MIN_SPP_PER_SEED サンプルに届かない描画の領域は未判定）で、比の大きさは記録するだけ。
#       領域ごとに、エンジンと Cycles の露出前の線形の平均（輝度・RGB）と比、Cycles の内訳、GBuffer と再現のアルベド、
#       再現した材質の AO の平均と、エンジンだけが掛ける材質の AO を Cycles の太陽の組・空の組に掛けた見込みとの比を書く。
#       --verify は撮影・材質・Cycles の入力の記録を今のファイルと照らす。
#       --require-judged <視点>:<領域>,... を付けると、名指しの領域が判定済みでなければ落とす。本描画（<参照>/cycles）で
#       未判定の領域は、影の切り出しの参照（<参照>/LIT_CROP_DIR/{material,cycles}。BlenderReference.py --lit --crop-regions）
#       で判定する: 切り出しの参照の入力（RefSplit。ずらし 0）とエンジンの撮影との条件の揃い、出力のハッシュ・カメラ・材質の
#       標本の位置、名指しの領域を描いたこと、2 本の差の収束（--gray と同じ基準、1 本 LIT_MIN_SPP_PER_SEED サンプル以上）、
#       太陽の直接光 + 空の直接光 + 間接光・直接光 + 間接光・太陽の組 + 空の組が領域ごとに合成の ±GRAY_SPLIT_TOLERANCE。
#       通った領域は値（エンジンと Cycles の平均・比・内訳・太陽の可視率）を切り出しの描画で置き換え、cycles_source に印を書く。
#
#   python Scripts/Terrain/ReferenceCheck.py --tonemap --capture <出力先> [--lit-capture <SCN-WLR-CYCLES-LIT の出力先>]
#                                            [--views a,b,...] [--verify]
#       表示変換の候補（TONEMAP_CANDIDATES）を、DisplayTransform.py の再現で同じ HDR に掛けて並べ、手本の写真と比べる。
#       HDR は 2 つ: <出力先> のエンジンの撮影（既定の荒野を SCN-WLR-BASELINE と同じ条件で CaptureScene.ps1 -Hdr で撮ったもの）と、
#       --lit-capture の Cycles の描画（cycles/<視点>.cycles.hdr.pfm。空は共通の照明の一様な空で埋め、露出はその撮影の値）。
#       どちらも 1280×720 へ面積の平均で縮めてから、撮影の露出のままと、写真の枠の中の明るさの中央値に露出を合わせた 2 通りで
#       表示の値を作り、写真の崖・地面の枠（CompareReference.py の PHOTOS）の地形の画素で、明るさの分布（符号化した輝度の
#       p05・p50・p95）・彩度（HSV の S の平均）・白飛びと黒つぶれの割合・崖 ÷ 地面の輝度の比・R/G・B/G を測る。
#       写真の現像（露出・ホワイトバランス・トーンカーブ）は分からないので、写真との差は合否に使わず、候補ごとの差の平均を量ごとに
#       候補の間の平均で割って足した点数（小さいほど写真に近い）として記録する。<出力先>/tonemap_check.json、視点ごとの比較画像
#       <出力先>/tonemap_<視点>.png（行: エンジン・Cycles × 撮影の露出・合わせた露出、列: 写真と候補）、一覧
#       <出力先>/tonemap_overview.png を書く。
#       合否: 撮影が既定の荒野の表示（起動引数に TONEMAP_FORBIDDEN_ARG_PREFIXES が無い）で、各視点の HDR が PNG と合い（--hdr と
#       同じ検査）、撮影の表示変換の記録が候補 current と同じ値であること、Cycles の描画とその入力（--lit と同じ記録）と写真が
#       揃い、測った値がすべて有限であること。--verify は撮影と --lit-capture の撮影・Cycles の入力の記録を今のファイルと照らす。
#   参照の入力とエンジンの実装の分け方（SCN-WLR-REF-SPLIT。本体は Scripts/Terrain/RefSplit.py）:
#       --gray・--lit・--blur-static・--blur-motion は、Game の撮影をエンジンの実装（描画のコード・Game/GameApplicationHandler.cpp・
#       カメラ台本・撮影に使った Game.exe・出力の画像のハッシュ。撮影の inputs.json の engine_impl）で、Cycles の参照を参照の入力
#       （地形のメッシュ・材質の値の表・共通の照明・カメラとずらし量・Blender の台本と出力。<参照>/cycles/reference.json）で照らす。
#       Cycles の参照は参照の入力が同じなら、エンジンの実装が変わっても使い続けられる。Game の撮影（ref2x・resident・FXAA などの
#       対照の組を含む）はエンジンの実装が今と一致しないと古いとして落とす。
#       --engine <撮影先>（複数回指定でき、視点・組ごとに最初に見つかる撮影先を読む）と --reference <参照の場所> で読み先を分けられる。
#   python Scripts/Terrain/ReferenceCheck.py --record-reference gray|lit --capture <参照の場所>
#       描いたときの入力が今のファイルと同じことを確かめてから、参照の入力の記録 <参照の場所>/cycles/reference.json を書く。
#   python Scripts/Terrain/ReferenceCheck.py --record-engine --capture <撮影先> --game-exe <Game.exe>
#       保存した撮影の inputs.json にエンジンの実装の記録を書き足す（撮影に使った Game.exe と同じ中身のときだけ）。
#   python Scripts/Terrain/ReferenceCheck.py --ref-split-selftest [--verify]
#       保存の写しを一時の場所に作り、エンジンの実装の 1 ファイル・Game.exe・カメラ台本・出力の画像を書き換えると Game の撮影が
#       落ち、エンジンの実装を書き換えても参照は古くならず、参照の入力（照明・Cycles の出力）を書き換えると参照が落ちることを確かめる。
#   python Scripts/Terrain/ReferenceCheck.py --gray|--lit|--blur-static|--blur-motion ... --expect <仕様の JSON> [--verify]
#       比較の検査に、領域・成分ごとの合否の閾値の仕様（Scripts/Terrain/ExpectSpecs/。形は ExpectSpec.py）の各行の判定を足す。
#       1 行でも範囲の外か、未判定を許さない行が未判定なら落とす。判定は報告の expect に書く。
#   python Scripts/Terrain/ReferenceCheck.py --expect-selftest [--verify]
#       保存した灰色・実材質・遠景のぼけの静止と動きの撮影で、今の値で通る仕様（ExpectSpecs/selftest-pass*.json）が通り、
#       外れる行（selftest-fail*.json）・許さない未判定・仕様の誤りで落ちることを確かめる。
#   python Scripts/Terrain/ReferenceCheck.py --camera-offset --engine <撮影先> [--views a,b,...] [--verify]
#       撮影の画と記録のカメラのずれ（本体は Scripts/Terrain/CameraOffset.py）。記録のカメラのまま補正なし（ずらし 0）で描いた
#       Blender の深度（無いか古ければその場で描いて <撮影先>/blender に保存する）とエンジンの深度のずれが 0.05 画素以内であることを、
#       既定では ref_cliff_01・ref_cliff_02 で検査する。報告は <撮影先>/camera_offset_check.json。
#   旧参照の移行（SHIFTED_REFERENCE_SCOPE）: ずらしありで描いた旧参照は --gray・--lit の領域の平均の比較だけに使い、
#       --albedo・--normal・--projection はずらし 0 で描き直すまで古いとして落とす。
#   python Scripts/Terrain/ReferenceCheck.py --regression --capture <after> --reference <before> [--verify]
#       回帰の撮影（Scripts/Terrain/CaptureRegression.ps1）の after を before と並べる（本体は Scripts/Terrain/Regression.py）。
#
#   python Scripts/Terrain/ReferenceCheck.py --gap-list [--capture <出力先>] [--require-atmos] [--verify]
#       節目の差の一覧（GapList.py）。灰色・アルベド・法線・実材質・大気の正解・遠景のぼけの静止と動き・表示変換の比較の報告が揃い、
#       --verify で合格した記録で、撮影・描画の入力が今のファイルと一致することを検査し、差を「描画の欠陥」「中身の欠陥」
#       「条件不一致・未判定」に分けて大きい順に並べた表を Docs/RenderingValidation/WastelandGapList.md の印の間に書き
#       （--verify では書かずに同じであることを検査する）、写真｜エンジン｜Cycles の一覧の画像を <出力先>/gap_overview.png に、
#       結果を <出力先>/gap_list_check.json に書く。<出力先> の既定は GAP_LIST_DIR。大気の正解の比較が古いことは
#       未判定の印として許し、--require-atmos では許さない（B の直し待ちの項目が残っていても失敗にする）。
#
#   python Scripts/Terrain/ReferenceCheck.py --grading --engine <撮影先> [--capture <6 視点の撮影先>] [--views a,b,...] [--verify]
#       荒野のグレーディングを中立にした変更の検査（GradeNeutral.py）。<撮影先> の既定の荒野の撮影（既定の視点は GRADING_VIEWS の 2 つ）で、
#       撮影の表示変換の記録が候補 neutral_plain と同じ値で、HDR に neutral_plain を掛けた画が撮影の PNG と 8 bit で 1 段以内に合う
#       （--display の scene の組と同じ基準を画面全体で。既定の撮影の PNG はブルームと FXAA が乗るので、撮影の記録どおりに PostEffects.py で再現して掛ける）ことを検査し、同じ HDR に変更前の表示変換（候補 current）を掛けた画との間で、地面の R/G と
#       白飛びの割合（写真の崖・地面の枠の地形の画素と画面全体）の前後を記録する。--capture に 6 視点を -Hdr で撮った別の撮影先を渡すと、
#       その各視点も検査して、写真｜前｜後の比較画像 <6 視点の撮影先>/grading_sheet.png を書く。報告は <撮影先>/grading_check.json。
#       --verify は撮影の入力の記録を今のファイルと照らす。
#
# 終了コード: すべて合格なら 0、どれかが不合格なら 1。
import argparse
import hashlib
import json
import math
import os
import re
import subprocess
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import AtmosphereReference as AR  # noqa: E402
import DisplayTransform as DT  # noqa: E402
import GrayRegions as GR  # noqa: E402

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# 種類ごとの入力（リポジトリのルートからのパス。フォルダはその下のすべて）
INPUT_SETS = {
    "engine": [
        "Assets/Shaders",
        "Library/Core/Private/Rendering",
        "Library/Core/Public/Rendering",
        "Game/GameModes/Wasteland",
        "Game/Scenes/SceneCatalog.h",
        "Assets/AssetSets/WastelandTerrain.json",
        "Assets/AssetSets/WastelandModels.json",
        "Assets/AssetSets/WastelandBuildingsModels.json",
        "Scripts/CaptureScene.ps1",
        "Scripts/Terrain/CommonLight.json",
    ],
    # 地形のメッシュの書き出し（Game の --wasteland-export-terrain）の元: DEM と遠景の記述、区画を焼くクッカー、書き出しのコード
    "terrain_mesh": [
        "Assets/AssetSets/WastelandTerrain.json",
        "Assets/Terrain/ValleyOfTheGods",
        "Tools/AssetCook/MeshCooker.cpp",
        "Tools/AssetCook/TerrainCliffDetail.cpp",
        "Tools/AssetCook/TerrainCliffDetail.h",
        "Game/GameModes/Wasteland/WastelandTerrainExport.cpp",
        "Game/GameModes/Wasteland/WastelandTerrainExport.h",
    ],
}

# 種類ごとの資産の組の定義。参照する資産の実体（メッシュの source_path・extra_sources、glTF の buffers・images、
# テクスチャの source_path・orm_*）を、git が無視するファイルも含めて入力に加える
ASSET_SET_INPUTS = {
    "engine": [
        "Assets/AssetSets/WastelandTerrain.json",
        "Assets/AssetSets/WastelandModels.json",
        "Assets/AssetSets/WastelandBuildingsModels.json",
    ],
    "terrain_mesh": [
        "Assets/AssetSets/WastelandTerrain.json",
    ],
}
# 入力の一覧で、資産の組から辿ったファイルをまとめる名前
ASSET_SOURCES_KEY = "asset_sources"

# Rec.709 の輝度の重み
LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)

# PNG との照合の基準（上の説明を参照）
DARK_PEAK = 0.8
# 記録だけに使う、より暗い画素の上限
DEEP_DARK_PEAK = 0.4
BLOOM_INTENSITY = 0.04
BLOOM_SIGMAS = (8.0, 32.0, 96.0)
BLOOM_RELATIVE = 0.01
BLOOM_ABSOLUTE = 0.0002
EDGE_LEVELS = 6
MIN_SAMPLE_RATIO = 0.005
WITHIN_LEVELS = 2
WITHIN_RATIO = 0.95
MEDIAN_LEVELS = 1.0

# --display の基準（上の説明を参照）
RAMP_BORDER = 2
RAMP_WITHIN_LEVELS = 1
SCENE_WITHIN_LEVELS = 1
SCENE_WITHIN_RATIO = 0.99
# 組ごとの約束: (組の名前, 色の階段か, 演算子)。scene は荒野の基準の 8 視点を全部要る
DISPLAY_SETS = (
    ("ramp-neutral", True, DT.OPERATOR_NEUTRAL_LINEAR),
    ("ramp-aces", True, DT.OPERATOR_ACES20_LUT),
    ("scene", False, DT.OPERATOR_NEUTRAL_LINEAR),
)
DISPLAY_SCENE_VIEWS = ("ref_far_01", "ref_far_02", "ref_cliff_01", "ref_cliff_02", "ref_ground_01", "ref_ground_02",
                       "outpost", "ground")

# --plate の基準（上の説明を参照）
COMMON_LIGHT_PATH = "Scripts/Terrain/CommonLight.json"
PLATE_CASES = ("sun", "sky")
PLATE_WINDOW = 0.2
PLATE_TOLERANCE = 0.03
PLATE_UNIFORMITY = 0.01
PLATE_GAME_ARGS = ("--wasteland-common-light", "--wasteland-gray-plate")

# --aov の基準（上の説明を参照）
AOV_HIDE_PREFIX = "--wasteland-hide="
AOV_HIDE_NAMES = ("clouds", "foliage", "rocks", "gravel", "buildings", "aerial", "fog")
AOV_NORMAL_LENGTH_TOLERANCE = 0.01
AOV_NORMAL_LENGTH_RATIO = 0.999
AOV_NORMAL_FACING_RATIO = 0.98
AOV_NORMAL_MEAN_Y = 0.3
AOV_MONOTONIC_RELATIVE = 0.02
AOV_MONOTONIC_ABSOLUTE = 0.05
AOV_MONOTONIC_MAX_INVERSIONS = 0.01
AOV_MAX_SURFACE_ABOVE_SKY = 0.001
# 地形だけの撮影（--wasteland-hide=all）の厳しい上限。高さ場だけなら崩れるのは稜線の縁の数画素で、草・岩・建物が立つと
# 何十倍にも増える（ref_cliff_01 で地形だけ 3.7e-6・1.2e-6、既定の荒野 9.1e-5・3.4e-5）
AOV_BARE_MAX_INVERSIONS = 5e-5
AOV_BARE_MAX_SURFACE_ABOVE_SKY = 1e-5
AOV_DEPTH_NORMAL_MEDIAN_DEG = 20.0
AOV_ALBEDO_Y_RANGE = (0.02, 0.9)
AOV_SKY_TOLERANCE = 0.01
AOV_SKY_RATIO = 0.999
# 地形だけの撮影で Game のログに要る行・あってはならない行
AOV_BARE_LOG_LINES = ("WASTELAND_HIDE mask=127 ",)
AOV_BARE_ABSENT_LOG_LINES = ("WASTELAND_BUILDING id=", "WASTELAND_TALUS casters=")
# 草・石を RenderWorld へ渡した行と、その束の数の語
AOV_FOLIAGE_PUBLISHED_LINE = "WASTELAND_FOLIAGE_PUBLISHED "
AOV_FOLIAGE_PUBLISHED_KEY = "batches="

# --projection（Blender の深度とエンジンの深度の投影の照合）
PROJECTION_VIEWS = ("ref_far_01", "ref_far_02", "ref_cliff_01", "ref_cliff_02", "ref_ground_01", "ref_ground_02")
PROJECTION_GAME_ARGS = ("--wasteland-common-light", "--wasteland-finest-lod")
PROJECTION_LOG_LINES = ("WASTELAND_FINEST_LOD ", "WASTELAND_HIDE mask=127 ")
PROJECTION_JUMP_STRONG = 0.08
PROJECTION_JUMP_WEAK = 0.04
PROJECTION_RADIUS_PX = 2
PROJECTION_EDGE_RATIO = 0.99
PROJECTION_SKYLINE_RATIO = 0.99
PROJECTION_SKY_MISMATCH = 0.005
PROJECTION_DEPTH_EDGE_MARGIN = 3
PROJECTION_DEPTH_MEDIAN = 0.005
PROJECTION_DEPTH_P95 = 0.02
PROJECTION_MAX_DISTANCE_PX = 16
PROJECTION_CAMERA_POSITION_M = 1e-3
PROJECTION_CAMERA_DIRECTION = 1e-5
PROJECTION_CAMERA_FOV_DEG = 1e-4

# --gray（灰色の地形のエンジンと Cycles の比較）
GRAY_VIEWS = ("ref_cliff_01", "ref_far_01")
GRAY_GAME_ARGS = ("--wasteland-common-light", "--wasteland-finest-lod", "--wasteland-gray-material")
GRAY_LOG_LINES = ("WASTELAND_FINEST_LOD ", "WASTELAND_HIDE mask=127 ", "WASTELAND_GRAY_MATERIAL ")
GRAY_ENGINE_CASES = ("sun", "sky")
GRAY_CYCLES_DIR = "cycles"
GRAY_REGIONS_PATH = "Scripts/Terrain/GrayRegions.py"
GRAY_PLATE_VIEW = "ref_cliff_01"
# GBuffer のアルベドは 8 bit（0.3 は 76/255 = 0.298、−0.65 %）なので ±1 % で見る
GRAY_ALBEDO_TOLERANCE = 0.01
GRAY_ALBEDO_RATIO = 0.999
GRAY_ROUGHNESS_TOLERANCE = 0.01
GRAY_SKY_MISMATCH = 0.005
GRAY_SPLIT_TOLERANCE = 0.01
# --albedo（地表の材質の照明の無いアルベドの再現）
# --lit（実材質の地形のエンジンと Cycles の比較）
LIT_VIEWS = ("ref_far_01", "ref_far_02", "ref_cliff_01", "ref_cliff_02", "ref_ground_01", "ref_ground_02")
LIT_MATERIAL_DIR = "material"
# 収束を判定する 1 本あたりのサンプル数の下限（BlenderReference.py の MIN_SPP_PER_SEED と同じ。届かない描画の領域は未判定）
LIT_MIN_SPP_PER_SEED = 512
# 本描画で収束しない領域の切り出しの参照の置き場（<参照> の下。BlenderReference.py の LIT_CROP_DIR と同じ）と、その内訳
LIT_CROP_DIR = "shadow"
LIT_CROP_PARTS = ("sun", "sky", "direct", "indirect", "sun_direct", "sky_direct")
# --tonemap（表示変換の候補を同じ HDR に掛けて比べる）
TONEMAP_VIEWS = LIT_VIEWS
# Cycles の HDR（と、その露出を決めたエンジンの撮影）を借りる SCN-WLR-CYCLES-LIT の出力先
TONEMAP_LIT_DIR = os.path.join(".harness", "runs", "scene-capture", "SCN-WLR-CYCLES-LIT")
# 既定の荒野の表示で撮った撮影であることの確かめ: 起動引数に置いてはいけないもの（照明・表示・中身を差し替える指定）
TONEMAP_FORBIDDEN_ARG_PREFIXES = ("--wasteland-common-light", "--wasteland-hide=", "--wasteland-gray", "--wasteland-finest-lod",
                                  "--display-check", "--tone-map", "--common-light-case")
# 候補（名前, 説明, 演算子, グレーディングの式, コントラスト, 彩度, 色温度）。露出（プリエクスポージャ）と VignettePass は撮影の値の
# まま、トーンマップの中のビネットは無し（撮影の既定と同じ）。ACES 2.0 の LUT は表示変換そのものでグレーディングを掛けない
TONEMAP_CANDIDATES = (
    ("current", "今の NeutralLinear＋グレーディング（コントラスト 1.35・彩度 1.2・色温度 0.05）",
     DT.OPERATOR_NEUTRAL_LINEAR, 1, 1.35, 1.2, 0.05),
    ("neutral_mild", "NeutralLinear＋弱いグレーディング（コントラスト 1.15・彩度 1.1・色温度 0.05）",
     DT.OPERATOR_NEUTRAL_LINEAR, 1, 1.15, 1.1, 0.05),
    ("neutral_plain", "NeutralLinear＋中立のグレーディング", DT.OPERATOR_NEUTRAL_LINEAR, 1, 1.0, 1.0, 0.0),
    ("aces20_lut", "ACES 2.0 SDR の LUT（中立のグレーディング）", DT.OPERATOR_ACES20_LUT, 1, 1.0, 1.0, 0.0),
    ("aces_filmic", "ACES Filmic の近似の曲線＋中立のグレーディング", DT.OPERATOR_ACES_FILMIC, 0, 1.0, 1.0, 0.0),
)
# 候補 current の値が撮影の記録（hdr.json の post）と合うことの許容
TONEMAP_PARAM_TOLERANCE = 1e-4
# 露出を写真に合わせる探索: プリエクスポージャに掛ける 2^s の s の範囲[段]と二分の回数
TONEMAP_MATCH_RANGE_EV = (-8.0, 8.0)
TONEMAP_MATCH_STEPS = 16
# 彩度（HSV の S）を数える画素の明るさの下限（符号化値の最大成分）、白飛び・黒つぶれの 8 bit の値
TONEMAP_SATURATION_MIN_VALUE = 0.02
TONEMAP_CLIP_LEVEL = 254
TONEMAP_CRUSH_LEVEL = 3
# 写真との差の点数に使う量（名前, 説明）。p50 は撮影の露出のままの比較だけで使う（合わせた露出では一致させてある）
TONEMAP_SCORE_METRICS = (
    ("p50", "明るさの中央値（符号化した輝度）"),
    ("spread", "明るさの幅 p95 − p05（符号化した輝度）"),
    ("saturation", "彩度の平均（HSV の S）"),
    ("log_cliff_over_ground", "崖 ÷ 地面の輝度の比の対数"),
    ("ground_r_over_g", "地面の R/G"),
    ("ground_b_over_g", "地面の B/G"),
    ("clip", "白飛びの割合"),
)
# 比較画像の 1 枚の大きさ[px]と見出しの高さ[px]
TONEMAP_SHEET_PANEL = (480, 270)
TONEMAP_OVERVIEW_PANEL = (320, 180)
TONEMAP_LABEL_HEIGHT = 30

ALBEDO_VIEWS = ("ref_cliff_01", "ref_ground_01")
ALBEDO_GAME_ARGS = ("--wasteland-common-light", "--wasteland-finest-lod")
ALBEDO_LOG_LINES = ("WASTELAND_FINEST_LOD ", "WASTELAND_HIDE mask=127 ", "WASTELAND_TERRAIN_MATERIAL ")
ALBEDO_RESULT_DIR = "albedo"
TM_SCRIPT = "Scripts/Terrain/TerrainMaterialReference.py"
# 領域: Blender の頂点の法線の y と、エンジンの深度（m）で分ける
ALBEDO_FLAT_NORMAL_Y = 0.8
ALBEDO_CLIFF_NORMAL_Y = 0.5
ALBEDO_NEAR_M = 30.0
ALBEDO_FAR_M = 300.0
ALBEDO_REGIONS = ("ground_near", "ground_mid", "ground_far", "slope", "cliff")
# 輪郭（空との境・深度の跳び）から離す画素の数（投影の照合で輪郭は 1〜2 画素以内で重なる）
ALBEDO_EDGE_MARGIN_PX = 3
ALBEDO_MIN_REGION_PX = 5000
# 平均: R・G・B の線形の平均の比（Blender ÷ エンジン）が 1 ± これ以内
ALBEDO_MEAN_TOLERANCE = 0.05
# 局所の模様: 輝度を σ ALBEDO_PATTERN_BLUR_PX でぼかし（標本の位置の 1 画素未満のずれと、GPU の異方性の実装の差で
# 画素の細かさの成分が入れ替わるのを除く）、ALBEDO_WINDOW_PX 角の窓（ALBEDO_WINDOW_STEP_PX おき、窓の全画素が領域の中）の
# SSIM（定数はアルベドの幅 ALBEDO_SSIM_RANGE で C1 = (0.01 L)²・C2 = (0.03 L)²）の平均が ALBEDO_SSIM_MIN 以上。
# 対照として、Blender の画を窓 1 つ分（ALBEDO_WINDOW_PX）横へずらした SSIM も測り、ALBEDO_SSIM_MIN − ALBEDO_CONTROL_MARGIN
# 未満なら見分けられる（模様の比べ方が効く）領域とする。視点ごとに見分けられる領域が 1 つ以上要る
ALBEDO_PATTERN_BLUR_PX = 1.0
ALBEDO_WINDOW_PX = 16
ALBEDO_WINDOW_STEP_PX = 8
ALBEDO_SSIM_RANGE = 0.3
ALBEDO_SSIM_MIN = 0.7
ALBEDO_CONTROL_MARGIN = 0.1
ALBEDO_MIN_WINDOWS = 20
# --normal（地表の材質の照明の無い法線と粗さの再現）。撮影・領域・輪郭の除き方は --albedo と同じ
NORMAL_RESULT_DIR = "normal"
# 法線: 画素ごとのエンジンの GBuffer の法線と Blender の再現の法線のなす角（度）の、領域ごとの中央値と 95 百分位の上限。
# 差の元は GPU の異方性の標本の実装（アルベドの照合で近景の細部の比 0.98〜1.03 に合わせた段の偏りの残り）と BC5 の圧縮で、
# 画素ごとの傾きの差の標準偏差を 1〜3° と見積もり（2 次元の大きさの中央値は約 1.2 倍、95 百分位は約 2.4 倍）、上限を決めた
NORMAL_MEDIAN_MAX_DEG = 2.0
NORMAL_P95_MAX_DEG = 8.0
# 粗さ: 領域の平均の差（Blender − エンジン）の絶対値の上限。エンジンは 8 bit（1/255）で書き、ORM は BC7 で張る
ROUGHNESS_MEAN_TOLERANCE = 0.02
# 対照: 法線は傾きを足す前の法線（Blender の BaseNormal）、粗さは遠くで落とした傾きの分を足す前の粗さで同じ値を測り、
# 法線の 95 百分位が NORMAL_P95_MAX_DEG を超える領域・粗さの平均の差が ROUGHNESS_MEAN_TOLERANCE を超える領域を
# 「比べ方が見分けられる領域」とする。視点ごとに、法線と粗さのそれぞれで 1 つ以上要る
# 法線の角度はカメラから NORMAL_MAX_DEPTH_M 未満の画素で判定する（それより先は記録だけ）。3 km より先はエンジンの描く地形の形が
# 書き出しの LOD 0 と食い違い（ref_ground_01 の Blender の深度との相対差の 95 百分位が 1〜3 km の 2.3e-4 から 3〜8 km で
# 1.6e-3 へ増える）、幾何の法線の差（対照と同じ大きさ）が材質の差を覆う。粗さは幾何に依らないので全画素で判定する
NORMAL_MAX_DEPTH_M = 3000.0
# 判定の内訳（判定には使わない）: 判定する画素を、幾何が画素で分解できている画素（Blender の頂点の法線と上下左右の隣の画素の
# 頂点の法線のなす角の最大が NORMAL_GEOMETRY_RESOLVED_DEG 未満）とそれ以外に分けて記録する。崖の細部の三角形が画素より
# 小さい所では頂点の法線が隣の画素で十数度変わり、標本の位置の小さな差で幾何の法線が変わるので、差の元の切り分けに使う
NORMAL_GEOMETRY_RESOLVED_DEG = 4.0
# 標本の位置: Blender の深度（BlenderReference.py が描いた depth）とエンジンの深度から求めた画素の小数のずれの残りがこれを
# 超えたら不合格（同じ点を比べていない）
NORMAL_SUBPIXEL_MAX_PX = 0.1
# 旧参照の移行（SCN-WL-CAMERA-OFFSET）: エンジンの視線行列の float の丸めで、撮影の画が記録のカメラから視点ごとに最大 0.8 画素
# ずれていた（直した）。その間に描いた参照は Blender のカメラをずらして揃えてあり、ずらし量は描画の記録（camera_shift_px）と
# 参照の署名（cycles/reference.json の cameras）に残る。ずらしありの旧参照（SCN-WLR-CYCLES-GRAY・LIT）は領域の平均の比較
# （--gray・--lit）に限って使い続け、--lit の標本の位置は記録のカメラ（ずらし 0）からの残りで照らす。画素ごとの比較
# （--albedo・--normal・--projection）はずらし 0 で描き直すまで古いとして落とす。以後の新しい描画はずらし 0 で描く
# （BlenderReference.py はずらしが 0 でない描画を Blender を起動する前に断り、RefSplit.write_reference もずらしありの参照の
# 記録を書かない）
SHIFTED_REFERENCE_SCOPE = "region_means_only"
# 描いた画を変えない BlenderReference.py の変更: 今の台本のハッシュ → 同じ画を描く前の版のハッシュ。前の版で描いた結果の
# 記録（inputs.json の script_sha256・参照の署名の blender.scripts）も今の台本で描いたものとして照らす。
# SCN-WL-CAMERA-OFFSET: ずらし 0 でない描画を起動の前に断り、自動補正と --camera-shift-hint を除いた（ふつうの Python の側だけ。
# Blender の中で走る描画の関数は変えていない）。ずらしありの旧参照を --gray・--lit で使い続けるため
# SCN-WLR-LIT-SHADOW-REF-C01: 切り出しの描画（--crop-regions。job の crop があるときだけ動く枝）と、材質の書き出し先の引数を
# 足した。crop の無い job の描画（--depth・--gray・--albedo・--normal・--lit）は変えていない
BLENDER_SCRIPT_EQUIVALENT_SHA256 = {
    "ab3aff4ec3c7776b27ad766059506d9e2f83b0e64904e76f76f54bbb508881e0":
        ("ad3cea721bf57a593caa9e7ab97228a6367fa94acd67bc00115e01886598be49",),
    "74c6ee7e3fce1a91d42929fd5700ac225c8db2b9ba8f924ee1330036da50a5a4":
        ("ab3aff4ec3c7776b27ad766059506d9e2f83b0e64904e76f76f54bbb508881e0",
         "ad3cea721bf57a593caa9e7ab97228a6367fa94acd67bc00115e01886598be49"),
}
# 小数のずれを求めるのに使う画素: 深度の勾配が 1 画素あたり深度のこの割合未満（1 次の近似が効く所）
SUBPIXEL_FIT_MAX_RELATIVE_GRADIENT = 0.01
# --atmos-dump: 空と霞の値の書き出し（SCN-WLR-ATMOS-DUMP）
ATMOS_GAME_ARG = "--atmosphere-dump"
ATMOS_LOG_LINES = ("ATMOSPHERE_DUMP ",)
ATMOS_TABLE_KEYS = ("probe", "transmittance_lut", "radiance_lut", "transmittance_table", "multiple_scattering", "sky_view")
ATMOS_REQUIRED_UNITS = {
    "transmittance_lut": "transmittance",
    "radiance_lut": "cd/m2",
    "transmittance_table": "optical_depth (transmittance = exp(-value))",
    "multiple_scattering": "cd/m2 per lux of sun illuminance (psi_ms)",
    "sky_view": "cd/m2",
}
ATMOS_PROBE_ROWS = 7
ATMOS_ELEVATION_RANGE = (-5.0, 90.0)
ATMOS_DISTANCE_RANGE = (300.0, 30000.0)
# volumetrics.frag の kAerialPerspectiveMinSkyDirectionY
ATMOS_AERIAL_MIN_SKY_Y = 0.035
# 半精度で上げた LUT と、単精度の表から引いた値の相対差（半精度の丸めは 4.9e-4）
ATMOS_HALF_TOLERANCE = 2e-3
# GPU の線形の標本と numpy の線形の標本の相対差（GPU の重みの精度の分）
ATMOS_SAMPLE_TOLERANCE = 0.01
# 放射輝度の相対差の分母の下限（cd/m²）
ATMOS_RADIANCE_FLOOR = 1.0
ATMOS_FOG_T_TOLERANCE = 1e-4
ATMOS_AERIAL_T_TOLERANCE = 1e-3
ATMOS_COMPOSITE_TOLERANCE = 1e-4
ATMOS_SUN_TABLE_TOLERANCE = 0.01
# SkyAtmospherePass の FloatToHalfRne が LUT の値を頭打ちにする上限（半精度の最大 65504 の 0.9 倍）
ATMOS_LUT_CLAMP = 65504.0 * 0.9
# --atmos-ref: 独立な正解との比較（SCN-WLR-ATMOS-REF）
ATMOS_REF_DEFAULT_VIEW = "ref_ground_02"
# 正解の段（AtmosphereReference.py の段 1。--verify で段 2 と 0.5 % 以内）
ATMOS_REF_LEVEL = 1
ATMOS_REF_ELEVATIONS = (0.0, 2.0, 5.0, 10.0, 20.0, 30.0, 45.0, 60.0, 90.0)
ATMOS_REF_OFFSETS = (0.0, 45.0, 90.0, 135.0, 180.0)
# 霞の表に使う方向の視線の仰角（地表への視線はすべて使う）
ATMOS_REF_HAZE_ELEVATIONS = (0.0, 2.0)
ATMOS_REF_SEGMENT_STEPS = 512
# 同じ媒質・同じ式の値（地表の太陽の透過率・霧と空気遠近の透過率）が正解と合う幅。空気遠近と霧のエンジンの式は平らな地面
# なので、30 km 先で球の丸み（高さ 70 m）の分だけずれる
ATMOS_REF_SAME_MODEL_TOLERANCE = 0.02
# Ψ_ms を比べる範囲（荒野の空と霞の視線が通る高さと、太陽が地平線より上）
ATMOS_REF_PSI_MAX_ALTITUDE = 10000.0
ATMOS_REF_PSI_MIN_COSINE = 0.0
# 写真: ReferencePhotos.py の ground_02（GPS 37.300958, -109.835461、2023-10-13 14:52 MDT = 20:52 UTC、通日 286）
ATMOS_REF_PHOTO = os.path.join(REPO_ROOT, "Assets", "Reference", "ValleyOfTheGods", "ground_02.jpg")
ATMOS_REF_PHOTO_TIME = (37.300958, -109.835461, 20.0 + 52.0 / 60.0, 286)
# ref_ground_02 の yaw・pitch（SceneCatalog.h）と縦の画角（WastelandRoutine.cpp）
ATMOS_REF_VIEW_POSE = (-32.4, 1.4, 10.5)
ATMOS_REF_PHOTO_BANDS = 4
# --blur-static: 遠景のぼけの静止の切り分け（SCN-WLR-FAR-BLUR-STATIC）
BLUR_VIEWS = ("ref_far_01", "ref_cliff_02")
BLUR_SIZE = (1920, 1080)
# 正解: 同じ視点を 2 倍の解像度・AA なしで撮った組（線形の光で 2×2 を平均して縮める）
BLUR_REFERENCE = "ref2x"
BLUR_REFERENCE_SCALE = 2
# 4K は描画先が大きく VRAM のジオメトリの取り分の目標が縮み、ページの塊を足せずに要求が残る。取り分だけ広げる（描くものは変えない）
BLUR_REFERENCE_ARGS = ("--wasteland-aa=none", "--vram-geometry-share=9")
BLUR_REFERENCE_LOGS = ("WASTELAND_AA_PASS fxaa=off", "WASTELAND_BLUR_CASE aa=none ", "VRAM_POOL_SHARE geometry=9 ")
# 比べる組は、正解と同じジオメトリの取り分（9）で撮り、撮った時点でページが常駐しきって追い出しも止まっていることを確かめる
# （既定の取り分 1 は 1920×1080 の 2 視点の作業集合が目標に収まらず、追い出しと読み直しが続いたまま撮れてしまう）。
# 条件の効きは、取り分だけを広げた組（BLUR_BASELINE）との差で数える。既定の取り分そのものの効きは base との差で数える
BLUR_SHARE_ARG = "--vram-geometry-share=9"
BLUR_SHARE_LOG = "VRAM_POOL_SHARE geometry=9 "
BLUR_BASELINE = "resident"
# 条件ごとの組（撮影の出力先の下のフォルダ）。args は起動引数に要るもの、logs は Game のログに要る行の一部、
# resident は撮った時点の常駐を求めるか（base は既定の取り分のまま撮るので求めない。状態は記録する）
BLUR_CASES = (
    {"name": "base", "factor": "既定（取り分 1・FXAA・霞あり・内部解像度 1・LOD 既定・ミップの偏り 0・異方性 16）",
     "args": (), "logs": (), "resident": False},
    {"name": BLUR_BASELINE, "factor": "ジオメトリの取り分 9（ページが常駐しきる。ほかは既定）", "args": (BLUR_SHARE_ARG,),
     "logs": (BLUR_SHARE_LOG,), "resident": True},
    {"name": "aa-taa", "factor": "AA を TAA に", "args": ("--wasteland-aa=taa", BLUR_SHARE_ARG),
     "logs": ("WASTELAND_BLUR_CASE aa=taa ", BLUR_SHARE_LOG), "resident": True},
    {"name": "aa-none", "factor": "AA なし（FXAA のパスを外す）", "args": ("--wasteland-aa=none", BLUR_SHARE_ARG),
     "logs": ("WASTELAND_BLUR_CASE aa=none ", "WASTELAND_AA_PASS fxaa=off", BLUR_SHARE_LOG), "resident": True},
    {"name": "haze-off", "factor": "霞（空気遠近と高さフォグ）を切る", "args": ("--wasteland-hide=aerial+fog", BLUR_SHARE_ARG),
     "logs": ("WASTELAND_HIDE mask=96 ", BLUR_SHARE_LOG), "resident": True},
    {"name": "scale-075", "factor": "内部解像度 0.75", "args": ("--wasteland-render-scale=0.75", BLUR_SHARE_ARG),
     "logs": ("WASTELAND_BLUR_CASE aa=default render_scale=0.750", BLUR_SHARE_LOG), "resident": True},
    {"name": "mip-bias-m1", "factor": "地形のミップの偏り −1", "args": ("--terrain-mip-bias=-1", BLUR_SHARE_ARG),
     "logs": ("TERRAIN_SAMPLER pass=MegaGeometry mip_bias=-1.000 max_anisotropy=16",
              "TERRAIN_SAMPLER pass=VisibilityResolve mip_bias=-1.000 max_anisotropy=16", BLUR_SHARE_LOG), "resident": True},
    {"name": "aniso-1", "factor": "地形の異方性フィルタを切る（等方）", "args": ("--terrain-anisotropy=1", BLUR_SHARE_ARG),
     "logs": ("TERRAIN_SAMPLER pass=MegaGeometry mip_bias=0.000 max_anisotropy=1",
              "TERRAIN_SAMPLER pass=VisibilityResolve mip_bias=0.000 max_anisotropy=1", BLUR_SHARE_LOG), "resident": True},
    # 地形の LOD を最細に固定する。起動引数がジオメトリの取り分を 255 に、要求の列の容量を 131072 に、可視クラスタの列と
    # 区間の描画コマンドの上限を 8388608 に上げる（描くものは段のほかは変えない）
    {"name": "finest-lod", "factor": "地形の LOD を最も細かい段に固定", "args": ("--wasteland-finest-lod",),
     "logs": ("WASTELAND_FINEST_LOD lod=0", "VRAM_POOL_SHARE geometry=255 ",
              "GEOMETRY_PAGE_REQUEST_CAPACITY capacity=131072 ", "MEGA_VISIBLE_LIST_CAPACITY capacity=8388608 "),
     "resident": True},
)
# 撮った時点の常駐の確かめ: GEOMETRY_PAGES は常駐・進行中の各段・追い出しのどれかが変わると、落ち着き待ちの間は毎フレーム出る。
# 保存の前の最後の行の進行中（wanted・reading・ready・uploading）がすべて 0 で、最後の落ち着きの基準から保存までの行の
# 追い出しの数が変わっていないこと
# 撮った時点の描画の溢れ: 保存の前の最後の MEGA_CAPACITY の行の draw_dropped と visible_list_overflow が 0（描かれないクラスタが無い）
BLUR_CAPACITY_RE = re.compile(r"MEGA_CAPACITY draw_dropped=(\d+) .*visible_list_overflow=(\d+) ")
BLUR_PAGES_RE = re.compile(r"GEOMETRY_PAGES resident=(\d+) uploading=(\d+) ready=(\d+) reading=(\d+) wanted=(\d+) "
                           r"failed=\d+ resident_mb=\S+ evicted=(\d+)")
# 条件を替える起動引数の頭（組の約束にないものが混ざっていないかを見る）
BLUR_CONTROLLED_PREFIXES = ("--wasteland-aa=", "--wasteland-hide=", "--wasteland-render-scale=", "--wasteland-finest-lod",
                            "--terrain-mip-bias=", "--terrain-anisotropy=", "--mega-lod-error-px=",
                            "--vram-geometry-share=")
# 既定の組（base）に出てはいけないログの行（差し替えが効いていない）
BLUR_BASE_FORBIDDEN_LOGS = ("WASTELAND_BLUR_CASE ", "WASTELAND_AA_PASS ", "TERRAIN_SAMPLER ", "WASTELAND_FINEST_LOD ",
                            "WASTELAND_HIDE ", "VRAM_POOL_SHARE ", "GEOMETRY_PAGE_REQUEST_CAPACITY ",
                            "MEGA_VISIBLE_LIST_CAPACITY ")
# 範囲: 正解の深度が BLUR_FAR_MIN_M より奥の地表（2×2 のすべてが奥）。崖はそのうち法線の |y| が BLUR_CLIFF_NORMAL_Y 未満
BLUR_FAR_MIN_M = 1000.0
BLUR_CLIFF_NORMAL_Y = 0.8
BLUR_MIN_REGION_PX = 20000
# 帯域: 隣り合うガウス（σ 画素）の差。帯域 k は G(σk) − G(σk+1)（σ0 = 0 は画像そのもの）。範囲の外（空・近景）から
# 2σk+1 画素以内の画素は数えない（空との輪郭の強い縁が帯域に漏れるため）
BLUR_SIGMAS = (0.0, 1.0, 2.0, 4.0, 8.0)
BLUR_FINE_BANDS = (0, 1)
# 測り方の較正: 正解を σ BLUR_CALIB_BLUR_SIGMA でぼかした画は細部の欠けに、正解に白色雑音（帯域 0 の正解の RMS と同じ強さ）を
# 足した画はエイリアスに数えられる（エイリアスを鮮明さに数えない）ことを確かめる
BLUR_CALIB_BLUR_SIGMA = 0.8
BLUR_CALIB_BLUR_MIN_LOSS = 0.3
# ぼかしは帯域の中の形も少し変えるので残りが 0.05 前後出る（雑音は 0.7 以上）
BLUR_CALIB_BLUR_MAX_ALIAS = 0.1
BLUR_CALIB_NOISE_GAIN = 0.05
BLUR_CALIB_NOISE_MIN_ALIAS = 0.3
BLUR_CALIB_SEED = 20261019
# 荒野は仮想テクスチャを使わない（VRAM_POOLS の vt_used_mb がどの行も 0）。使っていれば常駐の確かめが要るので失敗にする
BLUR_VT_LOG_KEY = "VRAM_POOLS "

# --blur-motion: 遠景のぼけの動きの切り分け（SCN-WLR-FAR-BLUR-MOTION）
# 撮影は 1 視点（MOTION_VIEW）を、カメラの台本 MOTION_SCRIPT で一定の速さで振りながら撮る（Scripts/CameraScripts/wasteland-far-pan.txt）。
# 台本の撮る名前と、エポック（TAA の履歴の消去）から撮るまでのフレーム数
MOTION_VIEW = "ref_far_01"
MOTION_SCRIPT_ARG = "--camera-script=Scripts/CameraScripts/wasteland-far-pan.txt"
MOTION_OFFSETS = (0, 8, 30)
MOTION_PRE_TAG = "motion-30-pre"
MOTION_TAG_OFFSETS = {**{f"motion-{k}": k for k in MOTION_OFFSETS}, **{f"static-{k}": k for k in MOTION_OFFSETS},
                      MOTION_PRE_TAG: 22}
# --capture-views が台本の後に撮る、止めてエポックから 60 フレーム後の画（SCN-WLR-FAR-BLUR-STATIC と同じ撮り方）
MOTION_FINAL = "static-60"
# 台本の振り: 毎フレーム 0.03°（mouse=0.2 × 0.15°/px）。motion-30-pre は motion-30 の 8 フレーム前
MOTION_YAW_PER_FRAME = 0.03
MOTION_PRE_FRAMES = 8
# 撮った向きが基準の視点の向き（FREE_CAMERA_POSE phase=start、小数 3 桁）と合う幅[度]と、motion-30-pre との差の幅[度]
MOTION_YAW_TOLERANCE = 0.002
MOTION_PRE_YAW_TOLERANCE = 0.0005
# ref_far_01 の縦の画角[度]（Game/GameModes/Wasteland/WastelandRoutine.cpp の kReferenceShots）。画面の中心の 1° あたりの画素を決める
MOTION_VERTICAL_FOV_DEG = 12.77
# 画のずれ（位相相関）の合否: 振りと静止の同じ名前どうしは MOTION_ALIGN_MAX_PX 未満（1 フレームずれると約 2.5 画素）、
# motion-30-pre → motion-30 は振りの角から求めた画素数と MOTION_SHIFT_TOLERANCE_PX 以内
MOTION_ALIGN_MAX_PX = 0.25
MOTION_SHIFT_TOLERANCE_PX = 0.5
MOTION_ALIGN_CROP = (432, 768)
# 組: 正解（2 倍の解像度・AA なし）と、FXAA・TAA を正解と同じ取り分で撮った組。base は verify の撮影（<出力先> そのもの。
# 既定の取り分・FXAA）で、記録だけ
MOTION_REFERENCE = "ref2x"
MOTION_GROUPS = (
    {"name": MOTION_REFERENCE, "dir": MOTION_REFERENCE, "args": BLUR_REFERENCE_ARGS, "logs": BLUR_REFERENCE_LOGS,
     "size": (BLUR_SIZE[0] * BLUR_REFERENCE_SCALE, BLUR_SIZE[1] * BLUR_REFERENCE_SCALE), "resident": True},
    {"name": "fxaa", "dir": "fxaa", "args": (BLUR_SHARE_ARG,), "logs": (BLUR_SHARE_LOG,), "size": BLUR_SIZE,
     "resident": True},
    {"name": "taa", "dir": "taa", "args": ("--wasteland-aa=taa", BLUR_SHARE_ARG),
     "logs": ("WASTELAND_BLUR_CASE aa=taa ", BLUR_SHARE_LOG), "size": BLUR_SIZE, "resident": True},
    {"name": "base", "dir": ".", "args": (), "logs": (), "size": BLUR_SIZE, "resident": False},
)
MOTION_AA_GROUPS = ("fxaa", "taa", "base")

# 差の一覧（--gap-list、GapList.py）の出力先
GAP_LIST_DIR = os.path.join(".harness", "runs", "scene-capture", "SCN-WLR-GAP-LIST")
MOTION_EPOCH_RE = re.compile(r"CAMERA_SCRIPT_EPOCH engine_frame=(\d+)")
MOTION_REQUEST_RE = re.compile(r"CAMERA_SCRIPT_CAPTURE requested tag=(\S+) engine_frame=(\d+) position=\(([^)]*)\) "
                               r"yaw=(-?\d+(?:\.\d+)?) pitch=(-?\d+(?:\.\d+)?)")
MOTION_SAVED_RE = re.compile(r"CAMERA_SCRIPT_CAPTURE saved tag=(\S+) frame=(\d+) requested_engine_frame=(\d+) ")
MOTION_WAIT_RE = re.compile(r"CAMERA_SCRIPT_WAIT_READY (ready|timeout) waited=(\d+)")
MOTION_START_YAW_RE = re.compile(r"FREE_CAMERA_POSE phase=start .* yaw=(-?\d+(?:\.\d+)?) pitch=(-?\d+(?:\.\d+)?)")


def git_output(args, attempts=5):
    # 同時に走るコミットが索引を書き換えている間は失敗することがあるので、少し待って試し直す
    # （長い描画の後の入力の記録が、この一時的な失敗で捨てられないように）
    for attempt in range(attempts):
        try:
            return subprocess.run(["git"] + args, cwd=REPO_ROOT, capture_output=True, text=True, check=True).stdout
        except (OSError, subprocess.CalledProcessError):
            if attempt + 1 < attempts:
                time.sleep(2.0)
    return None


def list_input_files(paths):
    """パスの下の、git が追跡するか無視されていないファイル（リポジトリのルートからの / 区切りのパス、順に並べる）"""
    listed = git_output(["ls-files", "-z", "--cached", "--others", "--exclude-standard", "--"] + paths)
    if listed is None:
        raise RuntimeError("git ls-files を実行できない")
    files = sorted({f for f in listed.split("\0") if f and os.path.isfile(os.path.join(REPO_ROOT, f))})
    return files


def normalize_rel(path):
    return os.path.normpath(path).replace("\\", "/")


def gltf_references(rel):
    """glTF が外のファイルとして参照する buffers・images（リポジトリのルートからのパス）"""
    with open(os.path.join(REPO_ROOT, rel), encoding="utf-8-sig") as f:
        document = json.load(f)
    base = os.path.dirname(rel)
    refs = []
    for entry in (document.get("buffers") or []) + (document.get("images") or []):
        uri = entry.get("uri") if isinstance(entry, dict) else None
        if isinstance(uri, str) and uri and not uri.startswith("data:"):
            refs.append(normalize_rel(os.path.join(base, uri.replace("%20", " "))))
    return refs


def list_asset_sources(asset_sets, skip_keys=()):
    """資産の組が参照する資産の実体（git の無視に関わらず。テクスチャの skip_keys の鍵は辿らない）。(あるファイルの一覧, 無いファイルの一覧)"""
    referenced = set()
    for asset_set in asset_sets:
        path = os.path.join(REPO_ROOT, asset_set)
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8-sig") as f:
            definition = json.load(f)
        for model in definition.get("models") or []:
            for source in [model.get("source_path")] + list(model.get("extra_sources") or []):
                if isinstance(source, str) and source:
                    referenced.add(normalize_rel(source))
        for texture in definition.get("textures") or []:
            for key, value in texture.items():
                if (isinstance(value, str) and value and key not in skip_keys
                        and (key == "source_path" or key.startswith("orm_"))):
                    referenced.add(normalize_rel(value))
    for rel in sorted(referenced):
        if rel.lower().endswith(".gltf") and os.path.isfile(os.path.join(REPO_ROOT, rel)):
            referenced.update(gltf_references(rel))
    present = sorted(r for r in referenced if os.path.isfile(os.path.join(REPO_ROOT, r)))
    missing = sorted(referenced - set(present))
    return present, missing


def asset_set_digest(rel, skip_keys):
    """資産の組の定義のハッシュ。テクスチャの skip_keys の鍵を除いた中身（並べた JSON）で取る"""
    with open(os.path.join(REPO_ROOT, rel), encoding="utf-8-sig") as f:
        definition = json.load(f)
    for texture in definition.get("textures") or []:
        for key in skip_keys:
            texture.pop(key, None)
    return hashlib.sha256(json.dumps(definition, sort_keys=True, ensure_ascii=False).encode("utf-8")).hexdigest()


def hash_inputs(kind, skip_texture_keys=()):
    """種類ごとの入力のハッシュ。skip_texture_keys を与えると、資産の組のテクスチャのその鍵（とそれが指す画像）を入れない"""
    paths = INPUT_SETS[kind]
    tracked = list_input_files(paths)
    asset_sets = ASSET_SET_INPUTS.get(kind, [])
    asset_files, asset_missing = list_asset_sources(asset_sets, skip_texture_keys)
    files = sorted(set(tracked) | set(asset_files))
    asset_set = set(asset_files)
    total = hashlib.sha256()
    per_path = {}
    for path in paths:
        per_path[path] = hashlib.sha256()
    asset_hash = hashlib.sha256()
    absent = [p for p in paths if not os.path.exists(os.path.join(REPO_ROOT, p))]
    for rel in files:
        if skip_texture_keys and rel in asset_sets:
            line = f"{rel}\0{asset_set_digest(rel, skip_texture_keys)}\n".encode("utf-8")
        else:
            digest = hashlib.sha256()
            with open(os.path.join(REPO_ROOT, rel), "rb") as f:
                for block in iter(lambda: f.read(1 << 24), b""):
                    digest.update(block)
            line = f"{rel}\0{digest.hexdigest()}\n".encode("utf-8")
        total.update(line)
        for path in paths:
            if rel == path or rel.startswith(path.rstrip("/") + "/"):
                per_path[path].update(line)
        if rel in asset_set:
            asset_hash.update(line)
    for path in absent:
        total.update(f"{path}\0absent\n".encode("utf-8"))
    for rel in asset_missing:
        line = f"{rel}\0absent\n".encode("utf-8")
        total.update(line)
        asset_hash.update(line)
    path_hashes = {p: ("absent" if p in absent else per_path[p].hexdigest()) for p in paths}
    path_hashes[ASSET_SOURCES_KEY] = asset_hash.hexdigest()
    return {
        "files_sha256": total.hexdigest(),
        "file_count": len(files),
        "asset_source_count": len(asset_files),
        "asset_sources_missing": asset_missing,
        "paths": path_hashes,
    }


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 24), b""):
            digest.update(block)
    return digest.hexdigest()


def record_inputs(capture_dir, kind, game_exe=None):
    hashed = hash_inputs(kind)
    commit = (git_output(["rev-parse", "HEAD"]) or "").strip()
    status = git_output(["status", "--porcelain", "--"] + INPUT_SETS[kind]) or ""
    record = {
        "kind": kind,
        "git_commit": commit,
        "inputs_dirty_at_capture": bool(status.strip()),
        **hashed,
    }
    if game_exe:
        if not os.path.isfile(game_exe):
            print(f"REFERENCE_CHECK result=fail reason=game_exe_missing path={game_exe}")
            return 1
        record["game_exe_sha256"] = file_sha256(game_exe)
        record["game_exe_mtime"] = time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(os.path.getmtime(game_exe)))
    if kind == "engine":
        # エンジンの実装の署名（RefSplit.py。Cycles の参照の入力とは分けて照らす）
        import RefSplit
        record["engine_impl"] = RefSplit.engine_impl_record(sys.modules[__name__], capture_dir, game_exe)
        # 出力の画像は metrics.json の視点から辿る。metrics.json を書く前に呼ぶと空になるので、空の署名は書かない
        if not record["engine_impl"]["outputs_sha256"]:
            print("REFERENCE_CHECK result=fail reason=engine_outputs_empty "
                  "（metrics.json の視点から出力の画像を辿れない。metrics.json を書いた後に記録する）")
            return 1
    path = os.path.join(capture_dir, "inputs.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)
    print(f"REFERENCE_CHECK record_inputs kind={kind} files={hashed['file_count']} "
          f"asset_sources={hashed['asset_source_count']} asset_missing={len(hashed['asset_sources_missing'])} "
          f"sha256={hashed['files_sha256']} "
          f"commit={commit[:12]} dirty={int(record['inputs_dirty_at_capture'])} "
          f"game_exe_sha256={record.get('game_exe_sha256', 'none')}")
    return 0


def check_inputs(capture_dir):
    """inputs.json を今のファイルと照らす。(合格か, 理由の一覧, 記録)"""
    path = os.path.join(capture_dir, "inputs.json")
    if not os.path.isfile(path):
        return False, ["inputs.json が無い（撮影が入力のハッシュを書いていない）"], None
    with open(path, encoding="utf-8") as f:
        record = json.load(f)
    kind = record.get("kind")
    if kind not in INPUT_SETS:
        return False, [f"inputs.json の種類 {kind} を知らない"], record
    current = hash_inputs(kind)
    if current["files_sha256"] == record.get("files_sha256"):
        return True, [], record
    changed = [p for p, h in current["paths"].items() if record.get("paths", {}).get(p) != h]
    return False, [f"入力が撮影の後に変わった（古い結果）: {', '.join(changed) or '一覧'}"], record


def box_blur(image, radius):
    """縦横の箱の平均（端は端の値を延ばす）"""
    if radius <= 0:
        return image
    out = image
    for axis in (0, 1):
        pad = [(0, 0)] * out.ndim
        pad[axis] = (radius + 1, radius)
        padded = np.pad(out, pad, mode="edge")
        csum = np.cumsum(padded, axis=axis)
        n = out.shape[axis]
        upper = np.take(csum, np.arange(2 * radius + 1, 2 * radius + 1 + n), axis=axis)
        lower = np.take(csum, np.arange(0, n), axis=axis)
        out = (upper - lower) / (2 * radius + 1)
    return out


def gaussian_blur(image, sigma):
    """箱の平均を 3 回重ねたガウスの近似（半径は分散が sigma² に合うように選ぶ）"""
    radius = max(1, int(round((np.sqrt(4.0 * sigma * sigma + 1.0) - 1.0) / 2.0)))
    out = image
    for _ in range(3):
        out = box_blur(out, radius)
    return out


def local_range(luma):
    """3×3 の窓の最大 − 最小"""
    padded = np.pad(luma, 1, mode="edge")
    h, w = luma.shape
    windows = [padded[dy:dy + h, dx:dx + w] for dy in range(3) for dx in range(3)]
    stack = np.stack(windows)
    return stack.max(axis=0) - stack.min(axis=0)


def check_hdr_view(capture_dir, view):
    result = {"view": view, "pass": False, "failures": []}
    fails = result["failures"]
    png_path = os.path.join(capture_dir, f"{view}.png")
    pfm_path = os.path.join(capture_dir, f"{view}.hdr.pfm")
    meta_path = os.path.join(capture_dir, f"{view}.hdr.json")
    for path in (png_path, pfm_path, meta_path):
        if not os.path.isfile(path):
            fails.append(f"{os.path.basename(path)} が無い")
    if fails:
        return result
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    try:
        hdr = DT.read_pfm(pfm_path).astype(np.float64)
    except ValueError as error:
        fails.append(f"PFM を読めない: {error}")
        return result
    png = np.asarray(Image.open(png_path).convert("RGB"), dtype=np.float64)

    # 1. 大きさ
    height, width = hdr.shape[:2]
    result["size"] = f"{width}x{height}"
    if (meta.get("width"), meta.get("height")) != (width, height):
        fails.append(f"hdr.json の大きさ {meta.get('width')}x{meta.get('height')} が PFM {width}x{height} と違う")
    if png.shape[:2] != (height, width):
        fails.append(f"PNG の大きさ {png.shape[1]}x{png.shape[0]} が HDR {width}x{height} と違う")
    if meta.get("units") != "cd/m2" or meta.get("source") != "scene_color_before_bloom":
        fails.append(f"hdr.json の単位・読み出し元が約束と違う（units={meta.get('units')} source={meta.get('source')}）")

    # 2. 値の範囲
    nan_count = int(np.isnan(hdr).sum())
    inf_count = int(np.isinf(hdr).sum())
    negative_count = int((hdr < 0.0).sum())
    result["nan_values"] = nan_count
    result["inf_values"] = inf_count
    result["negative_values"] = negative_count
    if nan_count or inf_count or negative_count:
        fails.append(f"負・NaN・Inf の値がある（負 {negative_count}・NaN {nan_count}・Inf {inf_count}）")
    finite = np.where(np.isfinite(hdr), hdr, 0.0)
    luminance = (finite * LUMA).sum(axis=-1)
    result["luminance_cd_m2"] = {
        "min": float(luminance.min()),
        "p01": float(np.percentile(luminance, 1)),
        "p50": float(np.percentile(luminance, 50)),
        "p99": float(np.percentile(luminance, 99)),
        "max": float(luminance.max()),
        "mean": float(luminance.mean()),
    }
    if not luminance.max() > 0.0:
        fails.append("輝度の最大が 0（真っ黒）")
    pre_exposure = float(meta.get("pre_exposure", 0.0))
    result["pre_exposure"] = pre_exposure
    if not pre_exposure > 0.0:
        fails.append(f"プリエクスポージャが正でない（{pre_exposure}）")
    if fails:
        return result

    # 3. PNG との照合（表示変換は DisplayTransform.py）
    try:
        params = DT.params_from_capture(meta)
    except DT.UnsupportedDisplay as error:
        fails.append(f"表示変換を再現できず PNG を見積もれない: {error}")
        return result
    # レンズダートはブルームに乗るので、ブルームの影響の小さい画素の見積もりにも入らない
    display = meta.get("display", {})
    bloom_on = (meta.get("post") or {}).get("bloom", True)
    if bloom_on and display.get("lens_dirt_intensity", 0.0) > 0.0:
        fails.append("レンズダートが掛かり、PNG を見積もれない")
        return result
    exposed = DT.expose(finite, pre_exposure).astype(np.float64)
    _, predicted = DT.display_transform(exposed.astype(np.float32), params)
    predicted = predicted.astype(np.float64)
    peak = exposed.max(axis=-1)
    dark = peak < DARK_PEAK
    bloom_estimate = np.zeros(peak.shape)
    for sigma in BLOOM_SIGMAS:
        blurred = gaussian_blur(exposed, sigma)
        bloom_estimate = np.maximum(bloom_estimate, (BLOOM_INTENSITY * np.abs(blurred - exposed)).max(axis=-1))
    low_bloom = bloom_estimate <= BLOOM_RELATIVE * peak + BLOOM_ABSOLUTE
    png_luma = (png * LUMA).sum(axis=-1)
    predicted_luma = (predicted * LUMA).sum(axis=-1)
    flat = (local_range(png_luma) <= EDGE_LEVELS) & (local_range(predicted_luma) <= EDGE_LEVELS)
    selected = dark & low_bloom & flat
    count = int(selected.sum())
    total = width * height
    difference = png - predicted
    result["png_match"] = {
        "dark_pixels": int(dark.sum()),
        "selected_pixels": count,
        "selected_ratio": count / total,
    }
    if count < MIN_SAMPLE_RATIO * total:
        fails.append(f"比べる画素が少ない（{count} 画素、{count / total:.4f}。下限 {MIN_SAMPLE_RATIO}）")
        return result
    selected_difference = difference[selected]
    worst = np.abs(selected_difference).max(axis=-1)
    within = float((worst <= WITHIN_LEVELS + 0.5).mean())
    median = float(np.median(np.abs(selected_difference)))
    result["png_match"].update({
        "within_levels": WITHIN_LEVELS,
        "within_ratio": within,
        "median_abs_levels": median,
        "mean_signed_levels_rgb": [float(v) for v in selected_difference.mean(axis=0)],
        "p99_abs_levels": float(np.percentile(worst, 99)),
    })
    deep = selected & (peak < DEEP_DARK_PEAK)
    result["png_match"]["deep_dark_pixels"] = int(deep.sum())
    if deep.any():
        result["png_match"]["deep_dark_within_ratio"] = float(
            (np.abs(difference[deep]).max(axis=-1) <= WITHIN_LEVELS + 0.5).mean())
        result["png_match"]["deep_dark_median_abs_levels"] = float(np.median(np.abs(difference[deep])))
    # ブルームの影響が大きい暗い画素（縁でない）での PNG − 見積もり。PNG にだけブルームが乗っていれば正になる
    halo = dark & ~low_bloom & flat
    if halo.any():
        result["png_match"]["bloom_halo_mean_signed_levels"] = float((difference[halo] * LUMA).sum(axis=-1).mean())
        result["png_match"]["bloom_halo_pixels"] = int(halo.sum())
    if within < WITHIN_RATIO:
        fails.append(f"±{WITHIN_LEVELS} 段に入る画素が {within:.4f}（下限 {WITHIN_RATIO}）")
    if median > MEDIAN_LEVELS:
        fails.append(f"差の絶対値の中央値が {median:.3f} 段（上限 {MEDIAN_LEVELS}）")
    result["pass"] = not fails
    return result


def views_from_metrics(capture_dir):
    path = os.path.join(capture_dir, "metrics.json")
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8-sig") as f:
        metrics = json.load(f)
    views = metrics.get("views") or []
    if isinstance(views, dict):
        views = [views]
    return [v["view"] for v in views if isinstance(v, dict) and "view" in v]


def run_hdr(capture_dir, views, verify):
    failures = []
    inputs_ok, input_failures, record = check_inputs(capture_dir)
    if verify and not inputs_ok:
        failures.extend(input_failures)
    metric_views = views_from_metrics(capture_dir)
    if metric_views is None:
        failures.append("metrics.json が無い（CaptureScene.ps1 -Hdr の出力先でない）")
        metric_views = []
    targets = views if views else metric_views
    if not targets:
        failures.append("検査する視点が無い")
    for view in targets:
        if view not in metric_views:
            failures.append(f"{view}: metrics.json に無い視点")
    results = []
    for view in targets:
        result = check_hdr_view(capture_dir, view)
        results.append(result)
        lum = result.get("luminance_cd_m2", {})
        match = result.get("png_match", {})
        print(f"REFERENCE_CHECK hdr view={view} pass={int(result['pass'])} size={result.get('size')} "
              f"pre_exposure={result.get('pre_exposure')} "
              f"Y_p01={lum.get('p01', float('nan')):.4g} Y_p50={lum.get('p50', float('nan')):.4g} "
              f"Y_p99={lum.get('p99', float('nan')):.4g} Y_max={lum.get('max', float('nan')):.4g} "
              f"bad_values={result.get('negative_values', 0) + result.get('nan_values', 0) + result.get('inf_values', 0)} "
              f"selected={match.get('selected_pixels')} within={match.get('within_ratio')} "
              f"median_abs={match.get('median_abs_levels')} bias_rgb={match.get('mean_signed_levels_rgb')} "
              f"deep_dark={match.get('deep_dark_pixels')}/{match.get('deep_dark_within_ratio')} "
              f"bloom_halo={match.get('bloom_halo_mean_signed_levels')}")
        for failure in result["failures"]:
            failures.append(f"{view}: {failure}")
    report = {
        "mode": "hdr",
        "verify": bool(verify),
        "inputs_current": inputs_ok,
        "inputs_record": record,
        "criteria": {
            "dark_peak": DARK_PEAK, "deep_dark_peak": DEEP_DARK_PEAK, "bloom_intensity": BLOOM_INTENSITY, "bloom_sigmas": list(BLOOM_SIGMAS),
            "bloom_relative": BLOOM_RELATIVE, "bloom_absolute": BLOOM_ABSOLUTE, "edge_levels": EDGE_LEVELS,
            "min_sample_ratio": MIN_SAMPLE_RATIO, "within_levels": WITHIN_LEVELS, "within_ratio": WITHIN_RATIO,
            "median_levels": MEDIAN_LEVELS,
        },
        "views": results,
        "failures": failures,
    }
    with open(os.path.join(capture_dir, "hdr_check.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=hdr views={len(targets)} inputs_current={int(inputs_ok)} "
          f"report={os.path.join(capture_dir, 'hdr_check.json')}")
    return 0 if not failures else 1


def capture_files(set_dir, view):
    """撮影の視点の PNG と hdr.json の場所（既定の視点は default.png に撮られる）"""
    base = os.path.join(set_dir, view)
    if not os.path.isfile(base + ".png") and os.path.isfile(os.path.join(set_dir, "default.png")):
        base = os.path.join(set_dir, "default")
    return base + ".png", base + ".hdr.pfm", base + ".hdr.json"


def check_display_record(meta, expect_ramp, expect_operator):
    """撮影の記録が組の約束どおりか。失敗の理由の一覧"""
    post = meta.get("post")
    if not post or not post.get("tone_map_recorded"):
        return ["hdr.json にポストのパスの記録（post）が無い"]
    fails = []
    if post.get("bloom") or post.get("fxaa"):
        fails.append(f"ブルームか FXAA が外れていない（bloom={post.get('bloom')} fxaa={post.get('fxaa')}。--display-check で撮る）")
    if bool(post.get("display_ramp")) != expect_ramp:
        fails.append(f"色の階段の入力が約束と違う（display_ramp={post.get('display_ramp')}、約束 {expect_ramp}）")
    if int(post.get("tone_map_operator", -1)) != expect_operator:
        fails.append(f"トーンマップの演算子が約束と違う（{post.get('tone_map_operator')}、約束 {expect_operator}）")
    if not (post.get("hardware_srgb_encode") or post.get("shader_srgb_encode")):
        fails.append("提示の sRGB の符号化の記録が無い")
    return fails


def check_ramp_view(set_dir, view, expect_operator, lut):
    result = {"view": view, "pass": False, "failures": []}
    fails = result["failures"]
    png_path, _, meta_path = capture_files(set_dir, view)
    for path in (png_path, meta_path):
        if not os.path.isfile(path):
            fails.append(f"{os.path.basename(path)} が無い")
    if fails:
        return result
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    fails.extend(check_display_record(meta, True, expect_operator))
    try:
        params = DT.params_from_capture(meta)
    except DT.UnsupportedDisplay as error:
        fails.append(str(error))
    if fails:
        return result
    png = np.asarray(Image.open(png_path).convert("RGB"), dtype=np.int32)
    height, width = png.shape[:2]
    ramp, column_map, row_map = DT.display_ramp(width, height)
    predicted, encoded = DT.display_transform(ramp, params, lut)
    # 区画の縁から RAMP_BORDER 画素より内側
    x = np.arange(width)
    y = np.arange(height)
    column_edges = (np.arange(DT.RAMP_COLUMNS + 1) * width + DT.RAMP_COLUMNS - 1) // DT.RAMP_COLUMNS
    row_edges = (np.arange(DT.RAMP_ROWS + 1) * height + DT.RAMP_ROWS - 1) // DT.RAMP_ROWS
    inside_x = np.min(np.abs(x[:, None] - column_edges[None, :]), axis=1) >= RAMP_BORDER
    inside_y = np.min(np.abs(y[:, None] - row_edges[None, :]), axis=1) >= RAMP_BORDER
    inside = inside_y[:, None] & inside_x[None, :]
    difference = png - predicted.astype(np.int32)
    worst = np.abs(difference).max(axis=-1)
    selected = worst[inside]
    within = float((selected <= RAMP_WITHIN_LEVELS).mean())
    result.update({
        "size": f"{width}x{height}",
        "operator": params["operator"],
        "grading_mode": params["grading_mode"],
        "compared_pixels": int(inside.sum()),
        "within_levels": RAMP_WITHIN_LEVELS,
        "within_ratio": within,
        "exact_ratio": float((selected == 0).mean()),
        "max_abs_levels": int(selected.max()),
        "mean_signed_levels_rgb": [float(v) for v in difference[inside].mean(axis=0)],
    })
    # 区画ごと（行 × 列）の中央の画素の PNG・再現の値と、区画の中の最大の差
    patches = []
    for row in range(DT.RAMP_ROWS):
        for column in range(DT.RAMP_COLUMNS):
            mask = inside & (row_map == row) & (column_map == column)
            ys, xs = np.nonzero(mask)
            if len(ys) == 0:
                continue
            cy, cx = ys[len(ys) // 2], xs[len(xs) // 2]
            patches.append({
                "row": DT.RAMP_ROW_NAMES[row],
                "exposed": float(2.0 ** (column - DT.RAMP_EXPONENT_OFFSET)),
                "png_center": [int(v) for v in png[cy, cx]],
                "predicted_center": [int(v) for v in predicted[cy, cx]],
                "max_abs_levels": int(worst[mask].max()),
            })
    result["patches"] = patches
    if within < 1.0:
        bad = [p for p in patches if p["max_abs_levels"] > RAMP_WITHIN_LEVELS]
        fails.append(f"±{RAMP_WITHIN_LEVELS} 段を外れる画素がある（{within:.6f}、最大 {result['max_abs_levels']} 段、区画 "
                     + ", ".join(f"{p['row']}@{p['exposed']:g}:{p['max_abs_levels']}" for p in bad[:12]) + "）")
    result["pass"] = not fails
    return result


def check_scene_view(set_dir, view, expect_operator, lut):
    result = {"view": view, "pass": False, "failures": []}
    fails = result["failures"]
    png_path, pfm_path, meta_path = capture_files(set_dir, view)
    for path in (png_path, pfm_path, meta_path):
        if not os.path.isfile(path):
            fails.append(f"{os.path.basename(path)} が無い")
    if fails:
        return result
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    fails.extend(check_display_record(meta, False, expect_operator))
    try:
        params = DT.params_from_capture(meta)
    except DT.UnsupportedDisplay as error:
        fails.append(str(error))
    try:
        hdr = DT.read_pfm(pfm_path)
    except ValueError as error:
        fails.append(f"PFM を読めない: {error}")
        hdr = None
    pre_exposure = float(meta.get("pre_exposure", 0.0))
    if not pre_exposure > 0.0:
        fails.append(f"プリエクスポージャが正でない（{pre_exposure}）")
    if fails:
        return result
    if not np.isfinite(hdr).all() or (hdr < 0.0).any():
        fails.append("HDR に負・NaN・Inf がある")
        return result
    png = np.asarray(Image.open(png_path).convert("RGB"), dtype=np.int32)
    if png.shape[:2] != hdr.shape[:2]:
        fails.append(f"PNG の大きさ {png.shape[1]}x{png.shape[0]} が HDR {hdr.shape[1]}x{hdr.shape[0]} と違う")
        return result
    predicted, _ = DT.display_transform(DT.expose(hdr, pre_exposure), params, lut)
    difference = png - predicted.astype(np.int32)
    worst = np.abs(difference).max(axis=-1)
    within = float((worst <= SCENE_WITHIN_LEVELS).mean())
    result.update({
        "size": f"{png.shape[1]}x{png.shape[0]}",
        "pre_exposure": pre_exposure,
        "operator": params["operator"],
        "grading_mode": params["grading_mode"],
        "within_levels": SCENE_WITHIN_LEVELS,
        "within_ratio": within,
        "exact_ratio": float((worst == 0).mean()),
        "over_2_levels_ratio": float((worst > 2).mean()),
        "p999_abs_levels": float(np.percentile(worst, 99.9)),
        "max_abs_levels": int(worst.max()),
        "mean_signed_levels_rgb": [float(v) for v in difference.reshape(-1, 3).mean(axis=0)],
    })
    if within < SCENE_WITHIN_RATIO:
        fails.append(f"±{SCENE_WITHIN_LEVELS} 段に入る画素が {within:.5f}（下限 {SCENE_WITHIN_RATIO}）")
    result["pass"] = not fails
    return result


def run_display(capture_dir, verify):
    failures = []
    lut = DT.load_aces_lut()
    sets = []
    for name, expect_ramp, expect_operator in DISPLAY_SETS:
        set_dir = os.path.join(capture_dir, name)
        entry = {"set": name, "display_ramp": expect_ramp, "operator": expect_operator, "views": []}
        sets.append(entry)
        if not os.path.isdir(set_dir):
            failures.append(f"{name}: 撮影の組が無い（{set_dir}）")
            continue
        inputs_ok, input_failures, record = check_inputs(set_dir)
        entry["inputs_current"] = inputs_ok
        entry["inputs_commit"] = (record or {}).get("git_commit")
        if verify and not inputs_ok:
            failures.extend(f"{name}: {failure}" for failure in input_failures)
        metric_views = views_from_metrics(set_dir)
        if metric_views is None:
            failures.append(f"{name}: metrics.json が無い（CaptureScene.ps1 -Hdr の出力先でない）")
            continue
        if expect_ramp:
            targets = metric_views[:1]
        else:
            targets = list(DISPLAY_SCENE_VIEWS)
            missing = [v for v in targets if v not in metric_views]
            if missing:
                failures.append(f"{name}: 基準の視点が撮られていない（{', '.join(missing)}）")
                targets = [v for v in targets if v in metric_views]
        if not targets:
            failures.append(f"{name}: 検査する視点が無い")
        for view in targets:
            if expect_ramp:
                result = check_ramp_view(set_dir, view, expect_operator, lut)
                print(f"REFERENCE_CHECK display set={name} view={view} pass={int(result['pass'])} "
                      f"operator={result.get('operator')} compared={result.get('compared_pixels')} "
                      f"within{RAMP_WITHIN_LEVELS}={result.get('within_ratio')} exact={result.get('exact_ratio')} "
                      f"max_abs={result.get('max_abs_levels')} bias_rgb={result.get('mean_signed_levels_rgb')}")
            else:
                result = check_scene_view(set_dir, view, expect_operator, lut)
                print(f"REFERENCE_CHECK display set={name} view={view} pass={int(result['pass'])} "
                      f"pre_exposure={result.get('pre_exposure')} within{SCENE_WITHIN_LEVELS}={result.get('within_ratio')} "
                      f"exact={result.get('exact_ratio')} over2={result.get('over_2_levels_ratio')} "
                      f"p99.9={result.get('p999_abs_levels')} max_abs={result.get('max_abs_levels')} "
                      f"bias_rgb={result.get('mean_signed_levels_rgb')}")
            entry["views"].append(result)
            failures.extend(f"{name}/{view}: {failure}" for failure in result["failures"])
    report = {
        "mode": "display",
        "verify": bool(verify),
        "criteria": {
            "ramp_border": RAMP_BORDER, "ramp_within_levels": RAMP_WITHIN_LEVELS,
            "scene_within_levels": SCENE_WITHIN_LEVELS, "scene_within_ratio": SCENE_WITHIN_RATIO,
            "lut_weight_steps": DT.LUT_WEIGHT_STEPS,
        },
        "sets": sets,
        "failures": failures,
    }
    report_path = os.path.join(capture_dir, "display_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=display sets={len(DISPLAY_SETS)} report={report_path}")
    return 0 if not failures else 1


def load_common_light():
    with open(os.path.join(REPO_ROOT, COMMON_LIGHT_PATH), encoding="utf-8-sig") as f:
        return json.load(f)


def plate_sun_altitude(light, view):
    for item in light.get("views", []):
        if item.get("name") == view:
            return float(item["altitude_degrees"])
    return float(light["default_sun"]["altitude_degrees"])


def plate_expected_rgb(light, case, view):
    """入力（CommonLight.json）だけから求めた板の放射輝度の期待（cd/m²、RGB）と、その元の値"""
    albedo = float(light["gray_plate"]["albedo"])
    if case == "sun":
        altitude = plate_sun_altitude(light, view)
        irradiance = float(light["sun_illuminance_normal_lux"]) * np.sin(np.radians(altitude))
        return np.full(3, albedo * irradiance / np.pi), {"sun_altitude_degrees": altitude,
                                                          "horizontal_illuminance_lux": float(irradiance)}
    sky = np.asarray(light["sky_radiance_rgb"], dtype=np.float64)
    return albedo * sky, {"sky_radiance_rgb": sky.tolist()}


def plate_capture_args(case_dir):
    path = os.path.join(case_dir, "metrics.json")
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8-sig") as f:
        metrics = json.load(f)
    args = metrics.get("game_args") or []
    return [args] if isinstance(args, str) else list(args)


def check_plate_view(case_dir, case, view, light):
    result = {"view": view, "pass": False, "failures": []}
    fails = result["failures"]
    pfm_path = os.path.join(case_dir, f"{view}.hdr.pfm")
    meta_path = os.path.join(case_dir, f"{view}.hdr.json")
    log_path = os.path.join(case_dir, f"{view}.Game.log")
    for path in (pfm_path, meta_path):
        if not os.path.isfile(path):
            fails.append(f"{os.path.basename(path)} が無い")
    if fails:
        return result
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    if meta.get("units") != "cd/m2" or meta.get("source") != "scene_color_before_bloom":
        fails.append(f"hdr.json の単位・読み出し元が約束と違う（units={meta.get('units')} source={meta.get('source')}）")
    # Game がこの視点で共通の照明と板を実際に置いたこと（開発ビルドでない Game は起動引数を無視する）
    if os.path.isfile(log_path):
        with open(log_path, encoding="utf-8", errors="replace") as f:
            log_text = f.read()
        light_line = f"WASTELAND_COMMON_LIGHT view={view} case={case}"
        if light_line not in log_text:
            fails.append(f"Game のログに「{light_line}」が無い（共通の照明が掛かっていない）")
        if "WASTELAND_GRAY_PLATE " not in log_text:
            fails.append("Game のログに WASTELAND_GRAY_PLATE が無い（板が置かれていない）")
    else:
        fails.append(f"{os.path.basename(log_path)} が無い（共通の照明が掛かったかを確かめられない）")
    try:
        hdr = DT.read_pfm(pfm_path).astype(np.float64)
    except ValueError as error:
        fails.append(f"PFM を読めない: {error}")
        return result
    height, width = hdr.shape[:2]
    half_w = max(1, int(round(width * PLATE_WINDOW / 2.0)))
    half_h = max(1, int(round(height * PLATE_WINDOW / 2.0)))
    cy, cx = height // 2, width // 2
    window = hdr[cy - half_h:cy + half_h, cx - half_w:cx + half_w]
    if not np.isfinite(window).all() or (window < 0.0).any():
        fails.append("窓の中に負・NaN・Inf の値がある")
        return result
    luminance = (window * LUMA).sum(axis=-1)
    mean_rgb = window.reshape(-1, 3).mean(axis=0)
    mean_y = float(luminance.mean())
    cv = float(luminance.std() / mean_y) if mean_y > 0.0 else float("inf")
    expected_rgb, inputs = plate_expected_rgb(light, case, view)
    expected_y = float((expected_rgb * LUMA).sum())
    ratio = mean_y / expected_y if expected_y > 0.0 else float("inf")
    result.update({
        "window_pixels": f"{2 * half_w}x{2 * half_h}",
        "inputs": inputs,
        "albedo": float(light["gray_plate"]["albedo"]),
        "expected_luminance_cd_m2": expected_y,
        "measured_luminance_cd_m2": mean_y,
        "ratio": ratio,
        "expected_rgb_cd_m2": expected_rgb.tolist(),
        "measured_rgb_cd_m2": mean_rgb.tolist(),
        "ratio_rgb": (mean_rgb / np.maximum(expected_rgb, 1e-30)).tolist(),
        "luminance_cv": cv,
        "luminance_min_max": [float(luminance.min()), float(luminance.max())],
        "pre_exposure": float(meta.get("pre_exposure", 0.0)),
    })
    if not abs(ratio - 1.0) <= PLATE_TOLERANCE:
        fails.append(f"板の輝度 {mean_y:.3f} cd/m² が期待 {expected_y:.3f} cd/m² と {100.0 * (ratio - 1.0):+.2f} % 違う"
                     f"（許容 ±{100.0 * PLATE_TOLERANCE:.0f} %）")
    if not cv <= PLATE_UNIFORMITY:
        fails.append(f"窓の中の輝度の変動係数 {cv:.4f} が {PLATE_UNIFORMITY} を超える（窓が板からはみ出すか、影・模様がある）")
    result["pass"] = not fails
    return result


def run_plate(capture_dir, verify):
    failures = []
    light = load_common_light()
    cases = []
    for case in PLATE_CASES:
        case_dir = os.path.join(capture_dir, case)
        entry = {"case": case, "views": []}
        cases.append(entry)
        if not os.path.isdir(case_dir):
            failures.append(f"{case}: 撮影の組が無い（{case_dir}）")
            continue
        inputs_ok, input_failures, record = check_inputs(case_dir)
        entry["inputs_current"] = inputs_ok
        entry["inputs_commit"] = (record or {}).get("git_commit")
        if verify and not inputs_ok:
            failures.extend(f"{case}: {failure}" for failure in input_failures)
        game_args = plate_capture_args(case_dir)
        entry["game_args"] = game_args
        if game_args is None:
            failures.append(f"{case}: metrics.json が無い（CaptureScene.ps1 -Hdr の出力先でない）")
            continue
        wanted = list(PLATE_GAME_ARGS) + [f"--common-light-case={case}"]
        missing = [a for a in wanted if a not in game_args]
        if missing:
            failures.append(f"{case}: 撮影の起動引数に {', '.join(missing)} が無い（game_args={game_args}）")
        views = views_from_metrics(case_dir) or []
        if not views:
            failures.append(f"{case}: 検査する視点が無い")
        for view in views:
            result = check_plate_view(case_dir, case, view, light)
            print(f"REFERENCE_CHECK plate case={case} view={view} pass={int(result['pass'])} "
                  f"expected={result.get('expected_luminance_cd_m2')} measured={result.get('measured_luminance_cd_m2')} "
                  f"ratio={result.get('ratio')} ratio_rgb={result.get('ratio_rgb')} cv={result.get('luminance_cv')} "
                  f"window={result.get('window_pixels')} inputs_current={int(bool(inputs_ok))}")
            entry["views"].append(result)
            failures.extend(f"{case}/{view}: {failure}" for failure in result["failures"])
    report = {
        "mode": "plate",
        "verify": bool(verify),
        "common_light": COMMON_LIGHT_PATH,
        "criteria": {"window": PLATE_WINDOW, "tolerance": PLATE_TOLERANCE, "uniformity_cv": PLATE_UNIFORMITY},
        "cases": cases,
        "failures": failures,
    }
    report_path = os.path.join(capture_dir, "plate_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=plate cases={len(PLATE_CASES)} report={report_path}")
    return 0 if not failures else 1


# ---- --aov（GBuffer の生の値）----

def read_pfm_any(path):
    """RGB（"PF"）か 1 チャンネル（"Pf"）の PFM を (高さ, 幅, チャンネル数) の float32 で読む（画像の上を 0 行目にする）"""
    with open(path, "rb") as f:
        data = f.read()
    tokens = []
    pos = 0
    while len(tokens) < 4:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        if start == pos:
            raise ValueError("PFM の見出しが短い")
        tokens.append(data[start:pos].decode("ascii"))
    pos += 1
    if tokens[0] not in ("PF", "Pf"):
        raise ValueError(f"PFM でない（{tokens[0]}）")
    channels = 3 if tokens[0] == "PF" else 1
    width, height, scale = int(tokens[1]), int(tokens[2]), float(tokens[3])
    dtype = "<f4" if scale < 0 else ">f4"
    count = width * height * channels
    if len(data) - pos != count * 4:
        raise ValueError(f"PFM の画素の量が見出しと合わない（{len(data) - pos} バイト、見出しから {count * 4} バイト）")
    pixels = np.frombuffer(data, dtype=dtype, count=count, offset=pos)
    return pixels.reshape(height, width, channels)[::-1].astype(np.float32)


def view_game_log(capture_dir, view):
    """metrics.json の視点の game_log（Game のログのファイル名。複数視点の撮影は全視点で 1 つを共有する。無ければ None）"""
    path = os.path.join(capture_dir, "metrics.json")
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8-sig") as f:
        metrics = json.load(f)
    views = metrics.get("views") or []
    if isinstance(views, dict):
        views = [views]
    for entry in views:
        if isinstance(entry, dict) and entry.get("view") == view:
            return entry.get("game_log")
    return None


def aov_capture_args(capture_dir):
    """metrics.json の Game の起動引数（無ければ None）"""
    return plate_capture_args(capture_dir)


def aov_hide_names(game_args):
    """起動引数の --wasteland-hide= から外したものの名前の集合（all は全部に開く）"""
    names = set()
    for arg in game_args or []:
        if arg.startswith(AOV_HIDE_PREFIX):
            for token in arg[len(AOV_HIDE_PREFIX):].split("+"):
                names.update(AOV_HIDE_NAMES if token == "all" else {token})
    return names


def aov_pixel_rays(camera, width, height):
    """画素の中心を通る、カメラの前方向の成分が 1 の向き（ワールド。(高さ, 幅, 3)）。深度（前方向への距離）を掛けると位置になる"""
    forward = np.asarray(camera["forward"], dtype=np.float64)
    up = np.asarray(camera["up"], dtype=np.float64)
    right = np.asarray(camera["right"], dtype=np.float64)
    tan_half = np.tan(np.radians(float(camera["vertical_fov_degrees"])) / 2.0)
    aspect = float(camera["aspect_ratio"])
    xs = ((np.arange(width) + 0.5) / width * 2.0 - 1.0) * tan_half * aspect
    ys = (1.0 - (np.arange(height) + 0.5) / height * 2.0) * tan_half
    return (forward[None, None, :] + xs[None, :, None] * right[None, None, :] + ys[:, None, None] * up[None, None, :])


def check_aov_view(capture_dir, view, game_args, light):
    result = {"view": view, "pass": False, "failures": []}
    fails = result["failures"]
    meta_path = os.path.join(capture_dir, f"{view}.aov.json")
    png_path = os.path.join(capture_dir, f"{view}.png")
    if not os.path.isfile(meta_path):
        fails.append(f"{os.path.basename(meta_path)} が無い")
        return result
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    expected_meaning = {"albedo": "linear_albedo", "normal": "world_normal_y_up", "roughness": "perceptual_roughness",
                        "depth": "view_axis_distance"}
    images = {}
    for key, channels in (("albedo", 3), ("normal", 3), ("roughness", 1), ("depth", 1)):
        entry = meta.get(key) or {}
        if entry.get("meaning") != expected_meaning[key] or int(entry.get("channels", 0)) != channels:
            fails.append(f"aov.json の {key} の意味・チャンネル数が約束と違う（{entry}）")
        path = os.path.join(capture_dir, str(entry.get("file", "")))
        if not os.path.isfile(path):
            fails.append(f"{key} の画像 {entry.get('file')} が無い")
            continue
        try:
            image = read_pfm_any(path).astype(np.float64)
        except ValueError as error:
            fails.append(f"{key} の PFM を読めない: {error}")
            continue
        if image.shape[2] != channels:
            fails.append(f"{key} の PFM のチャンネル数 {image.shape[2]} が {channels} でない")
            continue
        images[key] = image
    if fails:
        return result
    width, height = int(meta.get("width", 0)), int(meta.get("height", 0))
    result["size"] = f"{width}x{height}"
    for key, image in images.items():
        if image.shape[:2] != (height, width):
            fails.append(f"{key} の大きさ {image.shape[1]}x{image.shape[0]} が aov.json {width}x{height} と違う")
    if os.path.isfile(png_path):
        png_size = Image.open(png_path).size
        if png_size != (width, height):
            fails.append(f"PNG の大きさ {png_size[0]}x{png_size[1]} が AOV {width}x{height} と違う")
    else:
        fails.append(f"{view}.png が無い")
    if fails:
        return result

    albedo = images["albedo"]
    normal = images["normal"]
    roughness = images["roughness"][..., 0]
    depth = images["depth"][..., 0]
    camera = meta.get("camera") or {}

    # 空の画素: 深度が +∞（AOV の約束では空は表面の値がすべて 0）
    sky = np.isposinf(depth)
    surface = ~sky
    surface_count = int(surface.sum())
    result["sky_ratio"] = float(sky.mean())
    if surface_count == 0:
        fails.append("表面の画素が無い（全部が空）")
        return result
    for key, image in (("albedo", albedo), ("normal", normal), ("roughness", roughness[..., None])):
        if np.isnan(image).any() or np.isinf(image).any():
            fails.append(f"{key} に NaN・Inf がある")
        if sky.any() and np.abs(image[sky]).max() != 0.0:
            fails.append(f"空の画素の {key} が 0 でない（空の判定と AOV の書き方が食い違う）")
    if np.isnan(depth).any() or np.isneginf(depth).any():
        fails.append("深度に NaN・−∞ がある")

    # 1. 法線: 長さ 1（GBuffer の値のまま）、カメラに向く、+Y が上
    length = np.linalg.norm(normal[surface], axis=-1)
    length_error = np.abs(length - 1.0)
    within = float((length_error <= AOV_NORMAL_LENGTH_TOLERANCE).mean())
    rays = aov_pixel_rays(camera, width, height)
    unit_rays = rays / np.linalg.norm(rays, axis=-1, keepdims=True)
    facing = float((np.einsum("ij,ij->i", normal[surface], -unit_rays[surface]) > 0.0).mean())
    result["normal"] = {
        "length_within_ratio": within,
        "length_error_p99": float(np.percentile(length_error, 99.0)),
        "length_error_max": float(length_error.max()),
        "facing_camera_ratio": facing,
        "mean_y": float(normal[surface][:, 1].mean()),
    }
    if within < AOV_NORMAL_LENGTH_RATIO:
        fails.append(f"法線の長さが 1 ± {AOV_NORMAL_LENGTH_TOLERANCE} に入る画素 {within:.5f} が {AOV_NORMAL_LENGTH_RATIO} 未満")
    if facing < AOV_NORMAL_FACING_RATIO:
        fails.append(f"カメラに向く法線の割合 {facing:.4f} が {AOV_NORMAL_FACING_RATIO} 未満（法線がワールド空間でない・向きが逆）")
    if not result["normal"]["mean_y"] > AOV_NORMAL_MEAN_Y:
        fails.append(f"法線の y の平均 {result['normal']['mean_y']:.3f} が {AOV_NORMAL_MEAN_Y} 以下（+Y が上でない）")

    # 2. 深度: 表面は有限で正、空は +∞。位置に戻した点の水平の距離が、画面の上へ行くほど縮まない（地形だけの高さ場では
    #    同じ縦の面の中で上の光線ほど遠くに当たる）。深度から作った面の向きが GBuffer の法線と合う（深度の単位と軸の確かめ）
    surface_depth = depth[surface]
    near = float(camera.get("near", 0.0))
    result["depth"] = {
        "min_m": float(surface_depth.min()),
        "p50_m": float(np.percentile(surface_depth, 50.0)),
        "max_m": float(surface_depth.max()),
    }
    if not surface_depth.min() >= near * 0.99:
        fails.append(f"表面の深度の最小 {surface_depth.min():.4g} m がカメラの近い面 {near} m より手前")
    position = np.asarray(camera["position"], dtype=np.float64)
    world = position[None, None, :] + rays * np.where(surface, depth, 0.0)[..., None]
    horizontal = np.hypot(world[..., 0] - position[0], world[..., 2] - position[2])
    # 縦に隣り合う表面の画素の組（上と下）で、上の水平の距離が下より許容を超えて短いもの
    upper, lower = horizontal[:-1, :], horizontal[1:, :]
    pair = surface[:-1, :] & surface[1:, :]
    tolerance = AOV_MONOTONIC_RELATIVE * lower + AOV_MONOTONIC_ABSOLUTE
    inversions = pair & (upper < lower - tolerance)
    inversion_ratio = float(inversions.sum() / max(1, pair.sum()))
    # 空の上に表面が浮く画素（列ごとに、いちばん下の空の画素より上にある表面）
    any_sky = sky.any(axis=0)
    lowest_sky = np.where(any_sky, height - 1 - np.argmax(sky[::-1, :], axis=0), -1)
    rows = np.arange(height)[:, None]
    floating = surface & (rows < lowest_sky[None, :])
    floating_ratio = float(floating.sum() / surface_count)
    # 深度から作った面の向き（右と下の隣との差の外積）と GBuffer の法線の角度
    dx = world[1:-1, 2:] - world[1:-1, :-2]
    dy = world[2:, 1:-1] - world[:-2, 1:-1]
    geometric = np.cross(dy, dx)
    geometric_length = np.linalg.norm(geometric, axis=-1)
    interior = surface[1:-1, 1:-1] & surface[1:-1, 2:] & surface[1:-1, :-2] & surface[2:, 1:-1] & surface[:-2, 1:-1]
    interior &= geometric_length > 0.0
    geometric = geometric / np.maximum(geometric_length, 1e-30)[..., None]
    # 外積の向きをカメラへ向ける（右・下の取り方の符号に依らない）
    flip = np.einsum("...j,...j->...", geometric, -unit_rays[1:-1, 1:-1]) < 0.0
    geometric[flip] *= -1.0
    shading = normal[1:-1, 1:-1] / np.maximum(np.linalg.norm(normal[1:-1, 1:-1], axis=-1), 1e-30)[..., None]
    cosine = np.clip(np.einsum("...j,...j->...", geometric, shading), -1.0, 1.0)
    angles = np.degrees(np.arccos(cosine[interior]))
    result["depth"].update({
        "upward_pairs": int(pair.sum()),
        "horizontal_inversion_ratio": inversion_ratio,
        "surface_above_sky_ratio": floating_ratio,
        "depth_normal_angle_p50_deg": float(np.percentile(angles, 50.0)) if angles.size else None,
        "depth_normal_angle_p90_deg": float(np.percentile(angles, 90.0)) if angles.size else None,
    })
    if inversion_ratio > AOV_MONOTONIC_MAX_INVERSIONS:
        fails.append(f"上へ行くほど水平の距離が縮む縦の組 {inversion_ratio:.5f} が {AOV_MONOTONIC_MAX_INVERSIONS} を超える"
                     "（深度が単調でない・地形の前に物がある）")
    if floating_ratio > AOV_MAX_SURFACE_ABOVE_SKY:
        fails.append(f"空より上にある表面の画素 {floating_ratio:.5f} が {AOV_MAX_SURFACE_ABOVE_SKY} を超える（宙に浮く物がある）")
    if not angles.size or np.percentile(angles, 50.0) > AOV_DEPTH_NORMAL_MEDIAN_DEG:
        fails.append(f"深度から作った面の向きと法線の角度の中央値 {result['depth']['depth_normal_angle_p50_deg']} 度が "
                     f"{AOV_DEPTH_NORMAL_MEDIAN_DEG} 度を超える（深度の単位・軸かカメラの記録が違う）")

    # 3. アルベドと粗さ: 0〜1 に入り、自然の地面の範囲にある
    surface_albedo = albedo[surface]
    albedo_y = surface_albedo @ LUMA
    result["albedo"] = {
        "min": float(surface_albedo.min()),
        "max": float(surface_albedo.max()),
        "y_p01": float(np.percentile(albedo_y, 1.0)),
        "y_p50": float(np.percentile(albedo_y, 50.0)),
        "y_p99": float(np.percentile(albedo_y, 99.0)),
        "mean_rgb": surface_albedo.mean(axis=0).tolist(),
    }
    surface_roughness = roughness[surface]
    result["roughness"] = {
        "min": float(surface_roughness.min()),
        "p50": float(np.percentile(surface_roughness, 50.0)),
        "max": float(surface_roughness.max()),
    }
    if surface_albedo.min() < 0.0 or surface_albedo.max() > 1.0:
        fails.append(f"アルベドが 0〜1 の外にある（最小 {surface_albedo.min():.4g}・最大 {surface_albedo.max():.4g}）")
    if not (AOV_ALBEDO_Y_RANGE[0] <= result["albedo"]["y_p01"] and result["albedo"]["y_p99"] <= AOV_ALBEDO_Y_RANGE[1]):
        fails.append(f"アルベドの輝度の 1〜99 % の幅 {result['albedo']['y_p01']:.3f}〜{result['albedo']['y_p99']:.3f} が "
                     f"自然の地面の範囲 {AOV_ALBEDO_Y_RANGE[0]}〜{AOV_ALBEDO_Y_RANGE[1]} を外れる（線形でない・照明が混ざる）")
    if surface_roughness.min() < 0.0 or surface_roughness.max() > 1.0:
        fails.append(f"粗さが 0〜1 の外にある（最小 {surface_roughness.min():.4g}・最大 {surface_roughness.max():.4g}）")

    # 4. 地形だけの撮影（--wasteland-hide=all）: 雲・物・霞が無い
    hidden = aov_hide_names(game_args)
    bare = hidden >= set(AOV_HIDE_NAMES)
    result["bare"] = {"hidden": sorted(hidden), "checked": bare}
    if bare:
        # 高さ場の地形だけなので、前に立つ物・宙の物で崩れる組が稜線の縁の分しか無い
        if inversion_ratio > AOV_BARE_MAX_INVERSIONS:
            fails.append(f"地形だけの撮影で、上へ行くほど水平の距離が縮む縦の組 {inversion_ratio:.2e} が "
                         f"{AOV_BARE_MAX_INVERSIONS:.0e} を超える（地形の前に物が残る）")
        if floating_ratio > AOV_BARE_MAX_SURFACE_ABOVE_SKY:
            fails.append(f"地形だけの撮影で、空より上にある表面の画素 {floating_ratio:.2e} が "
                         f"{AOV_BARE_MAX_SURFACE_ABOVE_SKY:.0e} を超える（宙に浮く物が残る）")
        log_name = view_game_log(capture_dir, view)
        log_path = os.path.join(capture_dir, log_name) if log_name else None
        if log_path is None:
            fails.append("metrics.json の視点に game_log が無い（外したものを確かめられない）")
        elif not os.path.isfile(log_path):
            fails.append(f"{log_name} が無い（外したものを確かめられない）")
        else:
            with open(log_path, encoding="utf-8", errors="replace") as f:
                log_text = f.read()
            for line in AOV_BARE_LOG_LINES:
                if line not in log_text:
                    fails.append(f"Game のログに「{line}」が無い（外す指定が掛かっていない）")
            for line in AOV_BARE_ABSENT_LOG_LINES:
                if line in log_text:
                    fails.append(f"Game のログに「{line}」がある（外したはずの物が置かれた）")
            # 草・石を RenderWorld へ渡した行がどれも 0 束（外した草・石を渡していない）
            published = [int(token.split("=", 1)[1]) for line in log_text.splitlines()
                         if AOV_FOLIAGE_PUBLISHED_LINE in line
                         for token in line.split(AOV_FOLIAGE_PUBLISHED_LINE, 1)[1].split()
                         if token.startswith(AOV_FOLIAGE_PUBLISHED_KEY)]
            result["bare"]["foliage_published_batches"] = published
            if not published:
                fails.append("Game のログに WASTELAND_FOLIAGE_PUBLISHED が無い（草・石を渡したかを確かめられない）")
            elif max(published) != 0:
                fails.append(f"草・石を {max(published)} 束渡している（外したはずの草・石が描かれる）")
        # 空の画素の色が一様な空と一致する（雲・霧・霞が空に乗らない）。共通の照明でない撮影は空が一様でないので見ない
        if "--wasteland-common-light" in (game_args or []):
            hdr_path = os.path.join(capture_dir, f"{view}.hdr.pfm")
            if not os.path.isfile(hdr_path):
                fails.append(f"{view}.hdr.pfm が無い（空の色を確かめられない。-Hdr と一緒に撮る）")
            elif not sky.any():
                fails.append("空の画素が無い（空の色を確かめられない）")
            else:
                hdr = DT.read_pfm(hdr_path).astype(np.float64)
                if hdr.shape[:2] != (height, width):
                    fails.append("HDR の大きさが AOV と違う")
                else:
                    expected_sky = np.asarray(light["sky_radiance_rgb"], dtype=np.float64)
                    case = next((a.split("=", 1)[1] for a in game_args if a.startswith("--common-light-case=")), "both")
                    if case == "sun":
                        expected_sky = np.zeros(3)
                    sky_pixels = hdr[sky]
                    deviation = np.abs(sky_pixels - expected_sky[None, :]) / np.maximum(expected_sky.max(), 1e-30)
                    sky_within = float((deviation.max(axis=-1) <= AOV_SKY_TOLERANCE).mean())
                    result["bare"].update({
                        "sky_pixels": int(sky.sum()),
                        "sky_expected_rgb": expected_sky.tolist(),
                        "sky_mean_rgb": sky_pixels.mean(axis=0).tolist(),
                        "sky_within_ratio": sky_within,
                        "sky_max_relative_deviation": float(deviation.max()),
                    })
                    if sky_within < AOV_SKY_RATIO:
                        fails.append(f"空の画素のうち一様な空の色に ±{100.0 * AOV_SKY_TOLERANCE:.1f} % で合うもの {sky_within:.5f} が "
                                     f"{AOV_SKY_RATIO} 未満（雲・霧・霞が乗る）")
        else:
            result["bare"]["sky_note"] = "共通の照明でないので空の色は見ない"
    result["pass"] = not fails
    return result


def run_aov(capture_dir, views, verify):
    failures = []
    inputs_ok, input_failures, record = check_inputs(capture_dir)
    if verify and not inputs_ok:
        failures.extend(input_failures)
    metric_views = views_from_metrics(capture_dir)
    game_args = aov_capture_args(capture_dir)
    if metric_views is None or game_args is None:
        failures.append("metrics.json が無い（CaptureScene.ps1 -Aov の出力先でない）")
        metric_views = []
    targets = views if views else metric_views
    if not targets:
        failures.append("検査する視点が無い")
    light = load_common_light()
    results = []
    for view in targets:
        if view not in metric_views:
            failures.append(f"{view}: metrics.json に無い視点")
            continue
        result = check_aov_view(capture_dir, view, game_args, light)
        results.append(result)
        normal = result.get("normal", {})
        depth = result.get("depth", {})
        albedo = result.get("albedo", {})
        bare = result.get("bare", {})
        print(f"REFERENCE_CHECK aov view={view} pass={int(result['pass'])} size={result.get('size')} "
              f"sky_ratio={result.get('sky_ratio')} normal_len_within={normal.get('length_within_ratio')} "
              f"normal_len_err_max={normal.get('length_error_max')} facing={normal.get('facing_camera_ratio')} "
              f"normal_mean_y={normal.get('mean_y')} depth_m={depth.get('min_m')}..{depth.get('max_m')} "
              f"inversions={depth.get('horizontal_inversion_ratio')} above_sky={depth.get('surface_above_sky_ratio')} "
              f"depth_normal_p50={depth.get('depth_normal_angle_p50_deg')} albedo_y={albedo.get('y_p01')}..{albedo.get('y_p99')} "
              f"albedo_range={albedo.get('min')}..{albedo.get('max')} bare={int(bool(bare.get('checked')))} "
              f"sky_within={bare.get('sky_within_ratio')} inputs_current={int(bool(inputs_ok))}")
        failures.extend(f"{view}: {failure}" for failure in result["failures"])
    report = {
        "mode": "aov",
        "verify": bool(verify),
        "inputs_current": inputs_ok,
        "inputs_record": record,
        "game_args": game_args,
        "criteria": {
            "normal_length_tolerance": AOV_NORMAL_LENGTH_TOLERANCE, "normal_length_ratio": AOV_NORMAL_LENGTH_RATIO,
            "normal_facing_ratio": AOV_NORMAL_FACING_RATIO, "normal_mean_y": AOV_NORMAL_MEAN_Y,
            "monotonic_relative": AOV_MONOTONIC_RELATIVE, "monotonic_absolute_m": AOV_MONOTONIC_ABSOLUTE,
            "monotonic_max_inversions": AOV_MONOTONIC_MAX_INVERSIONS, "max_surface_above_sky": AOV_MAX_SURFACE_ABOVE_SKY,
            "bare_max_inversions": AOV_BARE_MAX_INVERSIONS, "bare_max_surface_above_sky": AOV_BARE_MAX_SURFACE_ABOVE_SKY,
            "depth_normal_median_deg": AOV_DEPTH_NORMAL_MEDIAN_DEG, "albedo_y_range": list(AOV_ALBEDO_Y_RANGE),
            "sky_tolerance": AOV_SKY_TOLERANCE, "sky_ratio": AOV_SKY_RATIO,
        },
        "views": results,
        "failures": failures,
    }
    report_path = os.path.join(capture_dir, "aov_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=aov views={len(targets)} inputs_current={int(inputs_ok)} "
          f"report={report_path}")
    return 0 if not failures else 1


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 24), b""):
            digest.update(block)
    return digest.hexdigest()


def blender_script_matches(current_sha256, recorded_sha256):
    """記録の台本のハッシュが今の台本と同じか、同じ画を描く前の版か（BLENDER_SCRIPT_EQUIVALENT_SHA256）"""
    return recorded_sha256 == current_sha256 or \
        recorded_sha256 in BLENDER_SCRIPT_EQUIVALENT_SHA256.get(current_sha256, ())


def check_blender_inputs(blender_dir):
    """Blender の結果の入力の記録（BlenderReference.py が書く inputs.json）を今のファイルと照らす。(合格か, 理由の一覧, 記録)"""
    path = os.path.join(blender_dir, "inputs.json")
    if not os.path.isfile(path):
        return False, ["blender/inputs.json が無い（BlenderReference.py --depth を回していない）"], None
    with open(path, encoding="utf-8") as f:
        record = json.load(f)
    reasons = []
    script = os.path.join(REPO_ROOT, record.get("script", ""))
    if not os.path.isfile(script) or not blender_script_matches(sha256_file(script), record.get("script_sha256")):
        reasons.append(f"Blender の台本 {record.get('script')} が描いた後に変わった（古い結果）")
    current = hash_inputs("terrain_mesh")
    if current["files_sha256"] != (record.get("terrain_mesh_inputs") or {}).get("files_sha256"):
        old_paths = (record.get("terrain_mesh_inputs") or {}).get("paths", {})
        changed = [p for p, h in current["paths"].items() if old_paths.get(p) != h]
        reasons.append(f"地形のメッシュの元の入力が描いた後に変わった（古い結果）: {', '.join(changed) or '一覧'}")
    mesh_dir = os.path.join(REPO_ROOT, record.get("mesh_dir", ""))
    manifest = os.path.join(mesh_dir, "manifest.json")
    if not os.path.isfile(manifest) or sha256_file(manifest) != record.get("manifest_sha256"):
        reasons.append(f"書き出しの一覧 {record.get('mesh_dir')}/manifest.json が無いか、描いた後に変わった")
    changed_ply = []
    for name, digest in (record.get("ply_sha256") or {}).items():
        ply = os.path.join(mesh_dir, name)
        if not os.path.isfile(ply) or sha256_file(ply) != digest:
            changed_ply.append(name)
    if changed_ply:
        reasons.append(f"読んだ PLY が無いか、描いた後に変わった: {', '.join(changed_ply[:5])}"
                       f"{' ほか' if len(changed_ply) > 5 else ''}（{len(changed_ply)} 個）")
    if not record.get("ply_sha256"):
        reasons.append("読んだ PLY の記録が無い")
    return not reasons, reasons, record


def dilate(mask):
    """3×3 の膨張（Chebyshev の距離で 1 画素広げる）"""
    padded = np.pad(mask, 1)
    out = np.zeros_like(mask)
    h, w = mask.shape
    for dy in (0, 1, 2):
        for dx in (0, 1, 2):
            out |= padded[dy:dy + h, dx:dx + w]
    return out


def depth_edges(depth, jump):
    """深度の輪郭の画素。空と表面の境では表面の側、表面どうしでは深度の比が 1 + jump を超えて跳ぶ組の手前の側"""
    surface = np.isfinite(depth)
    edges = np.zeros(depth.shape, dtype=bool)
    for a_sl, b_sl in (((slice(None, -1), slice(None)), (slice(1, None), slice(None))),
                       ((slice(None), slice(None, -1)), (slice(None), slice(1, None)))):
        a = depth[a_sl]
        b = depth[b_sl]
        sa = surface[a_sl]
        sb = surface[b_sl]
        both = sa & sb
        with np.errstate(invalid="ignore", over="ignore"):
            jump_pair = both & (np.maximum(a, b) > np.minimum(a, b) * (1.0 + jump))
        sky_pair = sa ^ sb
        edges[a_sl] |= (jump_pair & (a <= b)) | (sky_pair & sa)
        edges[b_sl] |= (jump_pair & (b < a)) | (sky_pair & sb)
    return edges


def edge_distances(edges_from, edges_to, max_distance):
    """edges_from の各画素から edges_to の最も近い画素までの Chebyshev の距離（max_distance を超えるものは max_distance + 1）"""
    distance = np.full(edges_from.shape, max_distance + 1, dtype=np.int32)
    reach = edges_to.copy()
    for radius in range(max_distance + 1):
        distance[reach & (distance > radius)] = radius
        reach = dilate(reach)
    return distance[edges_from]


def distance_summary(distances, radius):
    if distances.size == 0:
        return {"count": 0}
    return {
        "count": int(distances.size),
        "within_ratio": float(np.mean(distances <= radius)),
        "p50_px": float(np.percentile(distances, 50)),
        "p95_px": float(np.percentile(distances, 95)),
        "p99_px": float(np.percentile(distances, 99)),
        "over_max_ratio": float(np.mean(distances > PROJECTION_MAX_DISTANCE_PX)),
    }


def skyline_rows(depth):
    """列ごとのいちばん上の表面の行（上が 0。表面の無い列は高さ）"""
    surface = np.isfinite(depth)
    rows = np.argmax(surface, axis=0)
    rows[~surface.any(axis=0)] = depth.shape[0]
    return rows


def skyline_points(rows, columns, shape):
    """列ごとのいちばん上の表面の画素の印（columns の列で、表面のある列だけ）"""
    points = np.zeros(shape, dtype=bool)
    cols = np.where(columns & (rows < shape[0]))[0]
    points[rows[cols], cols] = True
    return points


def sky_boundary(depth):
    """空と接する（上下左右に空の画素がある）表面の画素"""
    surface = np.isfinite(depth)
    sky = np.pad(~surface, 1, constant_values=False)
    h, w = surface.shape
    touches = sky[0:h, 1:w + 1] | sky[2:h + 2, 1:w + 1] | sky[1:h + 1, 0:w] | sky[1:h + 1, 2:w + 2]
    return surface & touches


def cameras_match(a, b):
    """2 つのカメラの記録（aov.json の camera の形）の差。(一致か, 差の値)"""
    diff = {
        "position_m": float(np.max(np.abs(np.asarray(a["position"], float) - np.asarray(b["position"], float)))),
        "direction": float(max(np.max(np.abs(np.asarray(a[k], float) - np.asarray(b[k], float)))
                               for k in ("forward", "up", "right"))),
        "vertical_fov_deg": abs(float(a["vertical_fov_degrees"]) - float(b["vertical_fov_degrees"])),
        "aspect": abs(float(a["aspect_ratio"]) - float(b["aspect_ratio"])),
    }
    ok = (diff["position_m"] <= PROJECTION_CAMERA_POSITION_M and diff["direction"] <= PROJECTION_CAMERA_DIRECTION and
          diff["vertical_fov_deg"] <= PROJECTION_CAMERA_FOV_DEG and diff["aspect"] <= 1e-6 and
          not bool(a.get("orthographic")) and not bool(b.get("orthographic")))
    return ok, diff


def write_projection_preview(path, engine, blender, engine_edges, blender_edges):
    """深度の対数の灰色に、エンジンの輪郭を赤、Blender の輪郭を青緑、重なりを白で描いた PNG"""
    finite = np.isfinite(engine)
    gray = np.zeros(engine.shape, dtype=np.float64)
    if finite.any():
        logd = np.log(np.where(finite, engine, 1.0))
        lo, hi = np.percentile(logd[finite], [1, 99])
        gray = np.where(finite, 0.15 + 0.45 * np.clip((hi - logd) / max(hi - lo, 1e-6), 0.0, 1.0), 0.0)
    rgb = np.repeat((gray * 255.0)[:, :, None], 3, axis=2).astype(np.uint8)
    rgb[engine_edges & ~blender_edges] = (255, 40, 40)
    rgb[blender_edges & ~engine_edges] = (40, 220, 255)
    rgb[engine_edges & blender_edges] = (255, 255, 255)
    Image.fromarray(rgb).save(path)


def geometry_pages_at_capture(lines, single_view=None):
    """Game のログの行から、視点ごとに保存の前の最後の GEOMETRY_PAGES と、落ち着きの基準から保存までの追い出しの数の組。
    視点ごとの起動（--capture-png）のログは single_view の名前で返す"""
    pages_at_capture = {}
    last_pages = None
    window_evicted = set()
    for line in lines:
        found = BLUR_PAGES_RE.search(line)
        if found:
            values = [int(v) for v in found.groups()]
            last_pages = {"resident": values[0], "uploading": values[1], "ready": values[2], "reading": values[3],
                          "wanted": values[4], "evicted": values[5]}
            window_evicted.add(values[5])
        if re.search(r"CAPTURE_VIEWS view=\S+ (?:switched|settle baseline) |ApplicationProcessor asset settle baseline ",
                     line):
            window_evicted = {last_pages["evicted"]} if last_pages else set()
        found = re.search(r"CAPTURE_VIEWS view=(\S+) saved ", line)
        saved_view = found.group(1) if found else None
        if saved_view is None and single_view and "ApplicationProcessor capture_png saved " in line:
            saved_view = single_view
        if saved_view and last_pages is not None:
            pages_at_capture[saved_view] = dict(last_pages, settle_evicted=sorted(window_evicted))
    return pages_at_capture


def check_projection_view(capture_dir, blender_dir, view, blender_record, game_args):
    failures = []
    result = {"view": view}
    aov_path = os.path.join(capture_dir, f"{view}.aov.json")
    blender_json_path = os.path.join(blender_dir, f"{view}.json")
    blender_pfm_path = os.path.join(blender_dir, f"{view}.depth.pfm")
    for required in (aov_path, blender_json_path, blender_pfm_path):
        if not os.path.isfile(required):
            failures.append(f"{os.path.relpath(required, capture_dir)} が無い")
    if failures:
        result.update({"pass": False, "failures": failures})
        return result
    with open(aov_path, encoding="utf-8-sig") as f:
        aov = json.load(f)
    with open(blender_json_path, encoding="utf-8") as f:
        blender_meta = json.load(f)

    # カメラ: エンジンの撮影の記録と、Blender が置いたカメラ（描いたときに読んだ記録）が同じ
    recorded = (blender_record.get("views") or {}).get(view) or {}
    for label, other in (("inputs.json", recorded.get("camera")), (f"{view}.json", blender_meta.get("camera_engine"))):
        if not other:
            failures.append(f"Blender の {label} にカメラの記録が無い")
            continue
        ok, diff = cameras_match(aov["camera"], other)
        result[f"camera_diff_{label}"] = diff
        if not ok:
            failures.append(f"エンジンのカメラと Blender のカメラ（{label}）が違う: {json.dumps(diff)}")
    if (int(aov["width"]), int(aov["height"])) != (int(blender_meta["width"]), int(blender_meta["height"])):
        failures.append(f"画素数が違う（エンジン {aov['width']}×{aov['height']}、Blender {blender_meta['width']}×{blender_meta['height']}）")
        result.update({"pass": False, "failures": failures})
        return result
    if int(blender_meta.get("samples", 0)) != 1:
        failures.append(f"Blender の深度が 1 サンプルでない（{blender_meta.get('samples')}）")
    shifted = shifted_reference_failure(blender_meta)
    if shifted:
        failures.append(shifted)

    engine = read_pfm_any(os.path.join(capture_dir, aov["depth"]["file"]))[:, :, 0].astype(np.float64)
    blender = read_pfm_any(blender_pfm_path)[:, :, 0].astype(np.float64)
    engine = np.where(np.isfinite(engine) & (engine > 0.0), engine, np.inf)
    blender = np.where(np.isfinite(blender) & (blender > 0.0), blender, np.inf)
    height, width = engine.shape
    result["size"] = [width, height]

    # 空と表面の分かれ方
    engine_surface = np.isfinite(engine)
    blender_surface = np.isfinite(blender)
    sky_mismatch = float(np.mean(engine_surface != blender_surface))
    result["surface_ratio"] = {"engine": float(np.mean(engine_surface)), "blender": float(np.mean(blender_surface))}
    result["sky_mismatch_ratio"] = sky_mismatch
    if sky_mismatch > PROJECTION_SKY_MISMATCH:
        failures.append(f"空と表面の分かれ方の食い違い {sky_mismatch:.4%} が {PROJECTION_SKY_MISMATCH:.2%} を超える")
    if not engine_surface.any() or not blender_surface.any():
        failures.append("どちらかの深度に表面が無い")
        result.update({"pass": False, "failures": failures})
        return result

    # 崖の輪郭と稜線: 強い輪郭の画素から、相手の弱い輪郭までの距離
    engine_strong = depth_edges(engine, PROJECTION_JUMP_STRONG)
    blender_strong = depth_edges(blender, PROJECTION_JUMP_STRONG)
    engine_weak = depth_edges(engine, PROJECTION_JUMP_WEAK)
    blender_weak = depth_edges(blender, PROJECTION_JUMP_WEAK)
    to_blender = distance_summary(edge_distances(engine_strong, blender_weak, PROJECTION_MAX_DISTANCE_PX),
                                  PROJECTION_RADIUS_PX)
    to_engine = distance_summary(edge_distances(blender_strong, engine_weak, PROJECTION_MAX_DISTANCE_PX),
                                 PROJECTION_RADIUS_PX)
    result["edges_engine_to_blender"] = to_blender
    result["edges_blender_to_engine"] = to_engine
    for label, summary in (("エンジン→Blender", to_blender), ("Blender→エンジン", to_engine)):
        if summary.get("count", 0) == 0:
            failures.append(f"{label}: 輪郭の画素が無い")
        elif summary["within_ratio"] < PROJECTION_EDGE_RATIO:
            failures.append(f"{label}: 輪郭の画素のうち相手の輪郭が {PROJECTION_RADIUS_PX} 画素以内にあるものが "
                            f"{summary['within_ratio']:.4f}（{PROJECTION_EDGE_RATIO} 未満。p99 {summary['p99_px']} 画素）")

    # 稜線（列ごとのいちばん上の表面の画素）から、相手の空との境の画素までの距離。稜線がほぼ縦に走る所（崖の側面）は
    # 横に 1 画素ずれるだけで列ごとの行が何十画素も変わるので、行の差ではなく画像の上の距離で測る
    engine_rows = skyline_rows(engine)
    blender_rows = skyline_rows(blender)
    columns = (engine_rows > 0) | (blender_rows > 0)
    skyline = {"columns": int(columns.sum())}
    if columns.any():
        engine_points = skyline_points(engine_rows, columns, engine.shape)
        blender_points = skyline_points(blender_rows, columns, blender.shape)
        to_blender_sky = edge_distances(engine_points, sky_boundary(blender), PROJECTION_MAX_DISTANCE_PX)
        to_engine_sky = edge_distances(blender_points, sky_boundary(engine), PROJECTION_MAX_DISTANCE_PX)
        distances = np.concatenate([to_blender_sky, to_engine_sky])
        if distances.size:
            skyline.update({
                "within_ratio": float(min(np.mean(to_blender_sky <= PROJECTION_RADIUS_PX) if to_blender_sky.size else 1.0,
                                          np.mean(to_engine_sky <= PROJECTION_RADIUS_PX) if to_engine_sky.size else 1.0)),
                "max_px": int(distances.max()), "p99_px": float(np.percentile(distances, 99)),
                "max_row_diff_px": int(np.abs(engine_rows - blender_rows)[columns].max()),
            })
            if skyline["within_ratio"] < PROJECTION_SKYLINE_RATIO:
                failures.append(f"稜線の画素のうち相手の空との境が {PROJECTION_RADIUS_PX} 画素以内にあるものが "
                                f"{skyline['within_ratio']:.4f}（{PROJECTION_SKYLINE_RATIO} 未満。最大 {skyline['max_px']} 画素）")
    result["skyline"] = skyline

    # 輪郭から離れた表面の深度の一致（単位・原点・軸の確かめ）
    near_edge = engine_strong | blender_strong
    for _ in range(PROJECTION_DEPTH_EDGE_MARGIN):
        near_edge = dilate(near_edge)
    common = engine_surface & blender_surface & ~near_edge
    if common.any():
        rel = np.abs(engine[common] - blender[common]) / blender[common]
        signed = (engine[common] - blender[common]) / blender[common]
        depth = {"pixels": int(common.sum()), "rel_p50": float(np.percentile(rel, 50)),
                 "rel_p95": float(np.percentile(rel, 95)), "rel_p99": float(np.percentile(rel, 99)),
                 "signed_mean": float(np.mean(signed)),
                 "engine_m": [float(engine[common].min()), float(engine[common].max())]}
        result["depth"] = depth
        if depth["rel_p50"] > PROJECTION_DEPTH_MEDIAN or depth["rel_p95"] > PROJECTION_DEPTH_P95:
            failures.append(f"輪郭から離れた表面の深度の相対差 中央値 {depth['rel_p50']:.5f}・95 % {depth['rel_p95']:.5f} が "
                            f"{PROJECTION_DEPTH_MEDIAN}・{PROJECTION_DEPTH_P95} を超える")
    else:
        failures.append("輪郭から離れた共通の表面が無い")

    # 撮影の指定と Game のログ: 共通の照明・地形だけ・最も細かい段で撮れた
    hide = aov_hide_names(game_args)
    if set(AOV_HIDE_NAMES) - hide:
        failures.append(f"撮影が --wasteland-hide=all でない（外していない: {', '.join(sorted(set(AOV_HIDE_NAMES) - hide))}）")
    for arg in PROJECTION_GAME_ARGS:
        if arg not in (game_args or []):
            failures.append(f"撮影の起動引数に {arg} が無い")
    log_name = view_game_log(capture_dir, view)
    log_path = os.path.join(capture_dir, log_name) if log_name else None
    if not log_path or not os.path.isfile(log_path):
        failures.append("Game のログが無い")
    else:
        with open(log_path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        for line in PROJECTION_LOG_LINES:
            if line not in text:
                failures.append(f"Game のログに {line.strip()} が無い")
        # 落ち着き待ちは、追い出しと読み直しが続く作業集合（ページの流しの空回り）を待たずに撮る。
        # 最も細かい段を描ききっていない撮影を拒むため、--blur-static と同じく撮った時点の進行中と追い出しを見る
        single_view = view if log_name == f"{view}.Game.log" else None
        pages = geometry_pages_at_capture(text.splitlines(), single_view).get(view)
        result["geometry_pages_at_capture"] = pages
        if pages is None:
            failures.append("Game のログに撮影の保存の行か、その前の GEOMETRY_PAGES の行が無い")
        else:
            in_flight = {k: pages.get(k) for k in ("wanted", "reading", "ready", "uploading")}
            if any(in_flight.values()) or len(pages["settle_evicted"]) != 1:
                failures.append(f"撮った時点でジオメトリのページが常駐しきっていない（進行中 {in_flight}、"
                                f"落ち着きの基準から保存までの追い出しの数 {pages['settle_evicted']}）")

    write_projection_preview(os.path.join(capture_dir, f"projection_{view}.png"), engine, blender,
                             engine_strong, blender_strong)
    result["blender"] = {k: blender_meta.get(k) for k in ("renderer", "tiles", "triangles", "load_seconds",
                                                          "render_seconds", "depth_source")}
    result.update({"pass": not failures, "failures": failures})
    return result


def run_projection(capture_dir, views, verify):
    failures = []
    inputs_ok, input_failures, record = check_inputs(capture_dir)
    blender_dir = os.path.join(capture_dir, "blender")
    blender_ok, blender_failures, blender_record = check_blender_inputs(blender_dir)
    if verify:
        failures.extend(input_failures)
        failures.extend(blender_failures)
    metric_views = views_from_metrics(capture_dir)
    game_args = aov_capture_args(capture_dir)
    if metric_views is None or game_args is None:
        failures.append("metrics.json が無い（CaptureScene.ps1 -Aov の出力先でない）")
        metric_views = []
    targets = views if views else PROJECTION_VIEWS
    results = []
    for view in targets:
        if view not in metric_views:
            failures.append(f"{view}: metrics.json に無い視点")
            continue
        result = check_projection_view(capture_dir, blender_dir, view, blender_record or {}, game_args)
        results.append(result)
        e2b = result.get("edges_engine_to_blender", {})
        b2e = result.get("edges_blender_to_engine", {})
        sky = result.get("skyline", {})
        depth = result.get("depth", {})
        print(f"REFERENCE_CHECK projection view={view} pass={int(result['pass'])} "
              f"edges_e2b={e2b.get('within_ratio')} (n={e2b.get('count')} p99={e2b.get('p99_px')}) "
              f"edges_b2e={b2e.get('within_ratio')} (n={b2e.get('count')} p99={b2e.get('p99_px')}) "
              f"skyline_within={sky.get('within_ratio')} skyline_max={sky.get('max_px')} "
              f"sky_mismatch={result.get('sky_mismatch_ratio')} depth_rel_p50={depth.get('rel_p50')} "
              f"depth_rel_p95={depth.get('rel_p95')} triangles={(result.get('blender') or {}).get('triangles')}")
        failures.extend(f"{view}: {failure}" for failure in result["failures"])
    report = {
        "mode": "projection",
        "verify": bool(verify),
        "inputs_current": inputs_ok,
        "blender_inputs_current": blender_ok,
        "inputs_record": record,
        "blender_inputs_record": blender_record,
        "game_args": game_args,
        "criteria": {
            "jump_strong": PROJECTION_JUMP_STRONG, "jump_weak": PROJECTION_JUMP_WEAK,
            "radius_px": PROJECTION_RADIUS_PX, "edge_ratio": PROJECTION_EDGE_RATIO,
            "skyline_ratio": PROJECTION_SKYLINE_RATIO, "sky_mismatch": PROJECTION_SKY_MISMATCH,
            "depth_edge_margin_px": PROJECTION_DEPTH_EDGE_MARGIN, "depth_rel_median": PROJECTION_DEPTH_MEDIAN,
            "depth_rel_p95": PROJECTION_DEPTH_P95, "camera_position_m": PROJECTION_CAMERA_POSITION_M,
            "camera_direction": PROJECTION_CAMERA_DIRECTION, "camera_fov_deg": PROJECTION_CAMERA_FOV_DEG,
        },
        "views": results,
        "failures": failures,
    }
    report_path = os.path.join(capture_dir, "projection_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=projection views={len(targets)} inputs_current={int(inputs_ok)} "
          f"blender_inputs_current={int(blender_ok)} report={report_path}")
    return 0 if not failures else 1


def atmos_read_floats(capture_dir, entry):
    """書き出しの JSON の 1 つのファイルの記述（file・dtype・shape）から float32 の配列を読む。(配列, 失敗の理由)"""
    path = os.path.join(capture_dir, entry.get("file", ""))
    if entry.get("dtype") != "float32_le" or entry.get("layout") != "row_major_first_row_first":
        return None, f"{entry.get('file')}: 形式が約束と違う（{entry.get('dtype')}・{entry.get('layout')}）"
    if not os.path.isfile(path):
        return None, f"{entry.get('file')} が無い"
    shape = tuple(int(n) for n in entry.get("shape", []))
    data = np.fromfile(path, dtype="<f4")
    if len(shape) != 3 or data.size != int(np.prod(shape)):
        return None, f"{entry.get('file')}: 値の数 {data.size} が形 {shape} と合わない"
    return data.reshape(shape).astype(np.float64), None


def atmos_bilinear(texture, u, v, wrap_u):
    """GPU の線形の標本（texel の中心が (i + 0.5) / 大きさ。横は wrap_u なら繰り返し、縦と wrap_u でない横は端で止める）"""
    height, width = texture.shape[:2]
    x = u * width - 0.5
    y = v * height - 0.5
    x0 = int(np.floor(x))
    y0 = int(np.floor(y))
    fx = x - x0
    fy = y - y0

    def column(i):
        return i % width if wrap_u else min(max(i, 0), width - 1)

    def row(j):
        return min(max(j, 0), height - 1)

    return ((1 - fx) * (1 - fy) * texture[row(y0), column(x0)] + fx * (1 - fy) * texture[row(y0), column(x0 + 1)] +
            (1 - fx) * fy * texture[row(y0 + 1), column(x0)] + fx * fy * texture[row(y0 + 1), column(x0 + 1)])


def atmos_equirect_uv(direction):
    """volumetrics.frag の EquirectangularUV"""
    u = np.arctan2(direction[2], direction[0]) / (2.0 * np.pi) + 0.5
    v = np.arcsin(np.clip(-direction[1], -1.0, 1.0)) / np.pi + 0.5
    return u, v


def atmos_fog_transmittance(fog, origin_height, direction_y, distance):
    """volumetrics.frag の ComputeAnalyticTransmittance（霧が無効なら密度 0）"""
    density = max(fog["density_at_base_per_m"], 0.0) if fog["enabled"] else 0.0
    falloff = max(fog["height_falloff_per_m"], 0.0)
    if density <= 0.0 or distance <= 0.0:
        return 1.0
    origin_exponent = -falloff * (origin_height - fog["base_height_m"])
    rate = falloff * float(np.clip(direction_y, -1.0, 1.0))
    change = rate * distance
    if abs(rate) < 1e-8 or abs(change) < 1e-4:
        log_integral = np.log(distance)
    else:
        if change > 80.0:
            log_numerator = 0.0
        elif change > 0.0:
            log_numerator = np.log(max(1.0 - np.exp(-change), 1e-35))
        elif change < -80.0:
            log_numerator = -change
        else:
            log_numerator = np.log(max(np.exp(-change) - 1.0, 1e-35))
        log_integral = log_numerator - np.log(abs(rate))
    log_depth = np.log(density) + origin_exponent + log_integral
    if log_depth >= np.log(80.0):
        return float(np.exp(-80.0))
    return float(np.exp(-min(np.exp(log_depth), 80.0)))


def atmos_density_integral(origin_height, direction_y, start, end, scale_height):
    """volumetrics.frag の IntegrateExponentialDensity"""
    length = end - start
    if not (length > 0.0) or not (scale_height > 0.0):
        return 0.0
    start_height = origin_height + direction_y * start
    start_density = np.exp(np.clip(-start_height / scale_height, -80.0, 80.0))
    change = float(np.clip(direction_y * length / scale_height, -80.0, 80.0))
    if abs(change) < 1e-3:
        return start_density * length * (1.0 - 0.5 * change)
    return start_density * length * (1.0 - np.exp(-change)) / change


def atmos_aerial(meta, camera_y, direction, distance, radiance_lut, transmittance_lut):
    """volumetrics.frag の ComputeAerialPerspective を、書き出した定数と LUT で求める。(透過率, 散乱光)"""
    aerial = meta["aerial_perspective"]
    sky = meta["sky"]
    start = max(aerial["start_distance_m"], 0.0)
    if not aerial["available"] or not (distance > start):
        return np.ones(3), np.zeros(3)
    origin = camera_y + aerial["origin_altitude_m"]
    rayleigh = atmos_density_integral(origin, direction[1], start, distance, sky["rayleigh_scale_height_m"])
    mie = atmos_density_integral(origin, direction[1], start, distance, sky["mie_scale_height_m"])
    depth = aerial["distance_scale"] * (np.array(aerial["shader_rayleigh_extinction_per_m"]) * rayleigh +
                                        aerial["shader_mie_extinction_per_m"] * mie)
    transmittance = np.exp(-np.clip(depth, 0.0, 80.0))
    sky_direction = np.array([direction[0], max(direction[1], ATMOS_AERIAL_MIN_SKY_Y), direction[2]])
    sky_direction /= np.linalg.norm(sky_direction)
    u, v = atmos_equirect_uv(sky_direction)
    sky_radiance = np.maximum(atmos_bilinear(radiance_lut[..., :3], u, v, True), 0.0)
    full = atmos_bilinear(transmittance_lut[..., :3], sky_direction[1], aerial["transmittance_lut_observer_v"], False)
    fraction = np.clip((1.0 - transmittance) / np.maximum(1.0 - full, 1e-3), 0.0, 1.0)
    return transmittance, sky_radiance * fraction


def atmos_relative_error(actual, expected, floor):
    actual = np.asarray(actual, dtype=np.float64)
    expected = np.asarray(expected, dtype=np.float64)
    return float(np.max(np.abs(actual - expected) / np.maximum(np.abs(expected), floor)))


def atmos_cosine_to_coordinate(cosine):
    """SkyAtmosphere.cpp の CosineToTableCoordinate"""
    c = np.clip(cosine, -1.0, 1.0)
    return np.sign(c) * np.sqrt(np.abs(c)) * 0.5 + 0.5


def atmos_table_bilinear(table, row_position, column_position):
    rows, columns = table.shape[:2]
    row = min(int(row_position), rows - 2)
    column = min(int(column_position), columns - 2)
    fr = np.float32(row_position - row)
    fc = np.float32(column_position - column)
    return ((1 - fr) * (1 - fc) * table[row, column] + (1 - fr) * fc * table[row, column + 1] +
            fr * (1 - fc) * table[row + 1, column] + fr * fc * table[row + 1, column + 1])


def check_atmos_view(capture_dir, view, game_args):
    failures = []
    result = {"view": view}
    meta_path = os.path.join(capture_dir, f"{view}.atmos.json")
    if not os.path.isfile(meta_path):
        return {"view": view, "pass": False, "failures": [f"{view}.atmos.json が無い"]}
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    if meta.get("format") != "norves_atmosphere_dump_v1":
        failures.append(f"形式が違う（{meta.get('format')}）")
    if ATMOS_GAME_ARG not in (game_args or []):
        failures.append(f"撮影の起動引数に {ATMOS_GAME_ARG} が無い")
    log_name = view_game_log(capture_dir, view)
    log_text = ""
    if log_name and os.path.isfile(os.path.join(capture_dir, log_name)):
        with open(os.path.join(capture_dir, log_name), encoding="utf-8", errors="replace") as f:
            log_text = f.read()
    saved_line = f"CAPTURE_VIEWS view={view} atmosphere saved "
    for line in ATMOS_LOG_LINES + (saved_line,):
        if line not in log_text:
            failures.append(f"Game のログに「{line.strip()}」が無い")

    # 1. ファイルが揃い、形・値の範囲が約束どおり
    arrays = {}
    for key in ATMOS_TABLE_KEYS:
        entry = meta.get(key)
        if not isinstance(entry, dict):
            failures.append(f"{key} の記述が無い")
            continue
        values, reason = atmos_read_floats(capture_dir, entry)
        if reason:
            failures.append(reason)
            continue
        if not np.all(np.isfinite(values)):
            failures.append(f"{key}: NaN か Inf がある")
        if key != "probe" and np.any(values < 0.0):
            failures.append(f"{key}: 負の値がある（最小 {float(values.min()):.4g}）")
        arrays[key] = values
    result["shapes"] = {key: list(values.shape) for key, values in arrays.items()}
    for key, units in ATMOS_REQUIRED_UNITS.items():
        if key in meta and meta[key].get("units") != units:
            failures.append(f"{key}: 単位の記述が {meta[key].get('units')}（約束は {units}）")
    if len(arrays) != len(ATMOS_TABLE_KEYS):
        result.update({"pass": False, "failures": failures})
        return result
    probe = arrays["probe"]
    transmittance_lut = arrays["transmittance_lut"]
    radiance_lut = arrays["radiance_lut"]
    if np.any(transmittance_lut[..., :3] > 1.0 + 1e-3):
        failures.append("透過率 LUT に 1 を超える値がある")
    for key in ("transmittance_table", "multiple_scattering", "sky_view"):
        if float(arrays[key].max()) <= 0.0:
            failures.append(f"{key}: 値がすべて 0")

    # 2. 視線: 方位 4 つ × 仰角 −5〜90° × 距離 0.3〜30 km と、距離 0.3〜30 km の地表への視線
    rays = meta["probe"]["rays"]
    if probe.shape[0] != ATMOS_PROBE_ROWS or probe.shape[1] != len(rays):
        failures.append(f"視線の値の形 {list(probe.shape)} が行 {ATMOS_PROBE_ROWS}・視線 {len(rays)} と合わない")
        result.update({"pass": False, "failures": failures})
        return result
    directions = [r for r in rays if r["kind"] == "direction"]
    grounds = [r for r in rays if r["kind"] == "ground"]
    offsets = sorted({r["azimuth_offset_degrees"] for r in directions})
    elevations = sorted({r["elevation_degrees"] for r in directions})
    distances = sorted({r["nominal_distance_m"] for r in directions})
    ground_distances = sorted({r["nominal_distance_m"] for r in grounds})
    ground_offsets = sorted({r["azimuth_offset_degrees"] for r in grounds})
    result["rays"] = {"direction": len(directions), "ground": len(grounds), "azimuth_offsets_deg": offsets,
                      "elevations_deg": elevations, "distances_m": distances, "ground_distances_m": ground_distances}
    if len(offsets) < 4 or len(ground_offsets) < 4:
        failures.append(f"方位が 4 つ無い（{offsets}・地表 {ground_offsets}）")
    if not elevations or elevations[0] > ATMOS_ELEVATION_RANGE[0] or elevations[-1] < ATMOS_ELEVATION_RANGE[1]:
        failures.append(f"仰角が {ATMOS_ELEVATION_RANGE[0]}〜{ATMOS_ELEVATION_RANGE[1]}° を覆わない（{elevations}）")
    for name, values in (("視線", distances), ("地表への視線", ground_distances)):
        if not values or values[0] > ATMOS_DISTANCE_RANGE[0] or values[-1] < ATMOS_DISTANCE_RANGE[1]:
            failures.append(f"{name}の距離が {ATMOS_DISTANCE_RANGE[0]}〜{ATMOS_DISTANCE_RANGE[1]} m を覆わない（{values}）")
    if len(directions) != len(offsets) * len(elevations) * len(distances):
        failures.append("方位 × 仰角 × 距離の格子に欠けがある")
    for ray in grounds:
        expected = -np.degrees(np.arctan2(meta["probe"]["ground_eye_height_m"], ray["nominal_distance_m"]))
        if abs(ray["elevation_degrees"] - expected) > 1e-3 or ray["direction"][1] >= 0.0:
            failures.append(f"地表への視線（{ray['nominal_distance_m']} m）の向きが地表を向かない")
            break

    # 3. 値の範囲: 透過率は 0〜1、散乱光は 0 以上、距離が伸びると透過率は減り散乱光は増える
    fog_t = probe[0, :, 0]
    aerial_t = probe[3, :, :3]
    if np.any(fog_t < 0.0) or np.any(fog_t > 1.0 + 1e-6) or np.any(aerial_t < 0.0) or np.any(aerial_t > 1.0 + 1e-6):
        failures.append("透過率が 0〜1 を外れる")
    for row in (1, 2, 4, 5, 6):
        if np.any(probe[row, :, :3] < 0.0):
            failures.append(f"視線の値の行 {row} に負の値がある")
    inversions = 0
    for offset in offsets:
        for elevation in elevations:
            indices = [i for i, r in enumerate(rays) if r["kind"] == "direction" and
                       r["azimuth_offset_degrees"] == offset and r["elevation_degrees"] == elevation]
            indices.sort(key=lambda i: rays[i]["distance_m"])
            for a, b in zip(indices, indices[1:]):
                if fog_t[b] > fog_t[a] + 1e-6 or np.any(aerial_t[b] > aerial_t[a] + 1e-6) or \
                        np.any(probe[4, b, :3] < probe[4, a, :3] - 1e-4 * np.maximum(probe[4, a, :3], 1.0)):
                    inversions += 1
    result["distance_inversions"] = inversions
    if inversions:
        failures.append(f"距離が伸びても透過率が減らない・散乱光が増えない組が {inversions} ある")

    # 4. エンジンの値どうしが揃う
    # 4a. 透過率 LUT（GPU へ上げた値）= 透過率の表（光学的深さ）を CPU と同じ引き方で引いた値
    table = arrays["transmittance_table"]
    atmosphere_height = meta["sky"]["atmosphere_height_m"]
    lut_height, lut_width = transmittance_lut.shape[:2]
    worst_lut = 0.0
    for y in range(lut_height):
        altitude = y / (lut_height - 1)
        row_position = np.sqrt(altitude) * (table.shape[0] - 1)
        for x in range(lut_width):
            cosine = (x + 0.5) / lut_width
            column_position = atmos_cosine_to_coordinate(cosine) * (table.shape[1] - 1)
            expected = np.exp(-atmos_table_bilinear(table, row_position, column_position))
            worst_lut = max(worst_lut, atmos_relative_error(transmittance_lut[y, x, :3], expected, 1e-2))
    result["transmittance_lut_vs_table_max_rel"] = worst_lut
    if worst_lut > ATMOS_HALF_TOLERANCE:
        failures.append(f"透過率 LUT が透過率の表と合わない（相対差の最大 {worst_lut:.3g}）")

    # 4b. 空の放射輝度 LUT（GPU へ上げた値）= sky-view の表を CPU と同じ引き方で引いた値
    sky_view = arrays["sky_view"]
    sky_view_meta = meta["sky_view"]
    horizon = sky_view_meta["horizon_elevation_radians"]
    sun_azimuth = sky_view_meta["sun_azimuth_radians"]
    rad_height, rad_width = radiance_lut.shape[:2]
    worst_radiance = 0.0
    clamped_texels = 0
    peak_unclamped = 0.0
    for y in range(rad_height):
        latitude = ((y + 0.5) / rad_height - 0.5) * np.pi
        for x in range(0, rad_width, 2):
            longitude = ((x + 0.5) / rad_width - 0.5) * 2.0 * np.pi
            direction = (np.cos(latitude) * np.cos(longitude), -np.sin(latitude), np.cos(latitude) * np.sin(longitude))
            elevation = np.arcsin(np.clip(direction[1], -1.0, 1.0))
            if elevation >= horizon:
                u = np.sqrt((elevation - horizon) / (0.5 * np.pi - horizon))
            else:
                u = -np.sqrt((horizon - elevation) / (0.5 * np.pi + horizon))
            difference = abs(np.remainder(np.arctan2(direction[2], direction[0]) - sun_azimuth + np.pi, 2.0 * np.pi) - np.pi)
            w = np.sqrt(np.clip(difference / np.pi, 0.0, 1.0))
            row_position = np.clip(u * 0.5 + 0.5, 0.0, 1.0) * (sky_view.shape[0] - 1)
            column_position = w * (sky_view.shape[1] - 1)
            expected = atmos_table_bilinear(sky_view, row_position, column_position)
            peak_unclamped = max(peak_unclamped, float(np.max(expected)))
            if np.any(expected > ATMOS_LUT_CLAMP):
                clamped_texels += 1
            expected = np.minimum(expected, ATMOS_LUT_CLAMP)
            worst_radiance = max(worst_radiance,
                                 atmos_relative_error(radiance_lut[y, x, :3], expected, ATMOS_RADIANCE_FLOOR))
    result["radiance_lut_vs_sky_view_max_rel"] = worst_radiance
    # 照らした texel（横は 1 つおき）のうち、頭打ちになったものの数と、頭打ちの前の sky-view の最大（cd/m²）
    result["radiance_lut_clamped_texels_checked"] = clamped_texels
    result["radiance_lut_peak_before_clamp_cd_m2"] = peak_unclamped
    if worst_radiance > ATMOS_HALF_TOLERANCE:
        failures.append(f"空の放射輝度 LUT が sky-view の表と合わない（相対差の最大 {worst_radiance:.3g}）")

    # 4c. GPU が引いた空の放射輝度 LUT の値（行 5）= 書き出した LUT を同じ向きで引いた値（書き出しが GPU の LUT と同じ）
    camera_y = meta["camera_position"][1]
    worst_sample = 0.0
    worst_fog_t = 0.0
    worst_aerial_t = 0.0
    worst_aerial_s = 0.0
    worst_composite = 0.0
    fog = meta["height_fog"]
    for index, ray in enumerate(rays):
        direction = np.array(ray["direction"], dtype=np.float64)
        direction /= np.linalg.norm(direction)
        distance = ray["distance_m"]
        u, v = atmos_equirect_uv(direction)
        sampled = np.maximum(atmos_bilinear(radiance_lut[..., :3], u, v, True), 0.0)
        if fog["color_from_sky_radiance_lut"]:
            worst_sample = max(worst_sample, atmos_relative_error(probe[5, index, :3], sampled, ATMOS_RADIANCE_FLOOR))
        # 4d. 高さフォグの透過率（行 0）= 書き出した定数で ComputeAnalyticTransmittance を求めた値
        expected_fog_t = atmos_fog_transmittance(fog, camera_y, direction[1], distance)
        worst_fog_t = max(worst_fog_t, abs(probe[0, index, 0] - expected_fog_t))
        # 4e. 空気遠近（行 3・4）= 書き出した定数と LUT で ComputeAerialPerspective を求めた値
        expected_t, expected_s = atmos_aerial(meta, camera_y, direction, distance, radiance_lut, transmittance_lut)
        worst_aerial_t = max(worst_aerial_t, atmos_relative_error(probe[3, index, :3], expected_t, 1e-3))
        worst_aerial_s = max(worst_aerial_s, atmos_relative_error(probe[4, index, :3], expected_s, ATMOS_RADIANCE_FLOOR))
        # 4f. 合成（行 6）= 高さフォグの色 × 不透明度 + 太陽の単一散乱 + 空気遠近 × 高さフォグの透過率
        opacity = probe[0, index, 1]
        composite = probe[1, index, :3] * opacity + probe[2, index, :3] if opacity > 0.0 else np.zeros(3)
        if meta["aerial_perspective"]["available"]:
            composite = composite + probe[4, index, :3] * probe[0, index, 0]
        worst_composite = max(worst_composite, atmos_relative_error(probe[6, index, :3], composite, ATMOS_RADIANCE_FLOOR))
    result.update({"probe_sky_sample_max_rel": worst_sample, "fog_transmittance_max_abs": worst_fog_t,
                   "aerial_transmittance_max_rel": worst_aerial_t, "aerial_scattering_max_rel": worst_aerial_s,
                   "composite_max_rel": worst_composite})
    if worst_sample > ATMOS_SAMPLE_TOLERANCE:
        failures.append(f"GPU が引いた空の放射輝度が書き出した LUT と合わない（相対差の最大 {worst_sample:.3g}）")
    if worst_fog_t > ATMOS_FOG_T_TOLERANCE:
        failures.append(f"高さフォグの透過率が書き出した定数の式と合わない（差の最大 {worst_fog_t:.3g}）")
    if worst_aerial_t > ATMOS_AERIAL_T_TOLERANCE:
        failures.append(f"空気遠近の透過率が書き出した定数の式と合わない（相対差の最大 {worst_aerial_t:.3g}）")
    if worst_aerial_s > ATMOS_SAMPLE_TOLERANCE:
        failures.append(f"空気遠近の散乱光が書き出した定数と LUT の式と合わない（相対差の最大 {worst_aerial_s:.3g}）")
    if worst_composite > ATMOS_COMPOSITE_TOLERANCE:
        failures.append(f"合成の値が高さフォグと空気遠近の値から作った値と合わない（相対差の最大 {worst_composite:.3g}）")
    if not meta["aerial_perspective"]["available"] or not fog["enabled"]:
        failures.append("空気遠近か高さフォグが撮影のフレームで効いていない（荒野は両方を掛ける）")

    # 4g. 地表の照度: 太陽は大気の外の照度 × 地表の透過率、水平面は × sin(高度)。エンジンの太陽の方向光と同じ値
    ground = meta["ground_illuminance"]
    altitude = np.radians(meta["sky"]["sun_altitude_degrees"])
    sun_normal = np.array(ground["sun_normal_lux"])
    sun_expected = ground["sun_top_of_atmosphere_normal_lux"] * np.array(ground["sun_ground_transmittance"])
    sun_error = atmos_relative_error(sun_normal, sun_expected, 1.0)
    horizontal_error = atmos_relative_error(ground["sun_horizontal_lux"], sun_normal * np.sin(altitude), 1.0)
    light_error = atmos_relative_error(fog["sun_light_illuminance_lux"], sun_normal, 1.0) \
        if fog["sun_single_scattering"] else None
    # 地表の太陽の透過率（直接の積分）= 透過率の表を地表・太陽の天頂角で引いた値（表の補間の分だけずれる）
    table_sun = np.exp(-atmos_table_bilinear(table, 0.0, atmos_cosine_to_coordinate(np.sin(altitude)) * (table.shape[1] - 1)))
    sun_table_error = atmos_relative_error(ground["sun_ground_transmittance"], table_sun, 1e-3)
    sky_lux = np.array(ground["sky_horizontal_lux"])
    result["ground_illuminance"] = {
        "sun_normal_lux": sun_normal.tolist(), "sun_horizontal_lux": ground["sun_horizontal_lux"],
        "sky_horizontal_lux": sky_lux.tolist(), "sun_formula_max_rel": sun_error,
        "sun_horizontal_max_rel": horizontal_error, "sun_light_max_rel": light_error,
        "sun_transmittance_vs_table_max_rel": sun_table_error,
    }
    if sun_error > 1e-4 or horizontal_error > 1e-4:
        failures.append(f"地表の太陽の照度が大気の外の照度と透過率から合わない（{sun_error:.3g}・{horizontal_error:.3g}）")
    if light_error is None:
        failures.append("撮影のフレームで太陽の方向光（高さフォグの単一散乱）が使えていない")
    elif light_error > 1e-4:
        failures.append(f"エンジンの太陽の方向光の照度が地表の太陽の照度と合わない（相対差 {light_error:.3g}）")
    if sun_table_error > ATMOS_SUN_TABLE_TOLERANCE:
        failures.append(f"地表の太陽の透過率が透過率の表と合わない（相対差 {sun_table_error:.3g}）")
    if not (float(sky_lux.min()) > 0.0):
        failures.append("地表の空の照度が 0 以下")

    result["pass"] = not failures
    result["failures"] = failures
    return result


def run_atmos_dump(capture_dir, views, verify):
    failures = []
    inputs_ok, input_failures, record = check_inputs(capture_dir)
    if verify and not inputs_ok:
        failures.extend(input_failures)
    metric_views = views_from_metrics(capture_dir)
    game_args = plate_capture_args(capture_dir)
    if metric_views is None or game_args is None:
        failures.append("metrics.json が無い（CaptureScene.ps1 の出力先でない）")
        metric_views = []
    targets = views if views else metric_views
    if not targets:
        failures.append("検査する視点が無い")
    results = []
    for view in targets:
        if view not in metric_views:
            failures.append(f"{view}: metrics.json に無い視点")
            continue
        result = check_atmos_view(capture_dir, view, game_args)
        results.append(result)
        ground = result.get("ground_illuminance", {})
        print(f"REFERENCE_CHECK atmos_dump view={view} pass={int(result['pass'])} rays={result.get('rays')} "
              f"transmittance_lut_vs_table={result.get('transmittance_lut_vs_table_max_rel')} "
              f"radiance_lut_vs_sky_view={result.get('radiance_lut_vs_sky_view_max_rel')} "
              f"radiance_lut_clamped={result.get('radiance_lut_clamped_texels_checked')} "
              f"radiance_peak_before_clamp={result.get('radiance_lut_peak_before_clamp_cd_m2')} "
              f"probe_sky_sample={result.get('probe_sky_sample_max_rel')} fog_t={result.get('fog_transmittance_max_abs')} "
              f"aerial_t={result.get('aerial_transmittance_max_rel')} aerial_s={result.get('aerial_scattering_max_rel')} "
              f"composite={result.get('composite_max_rel')} inversions={result.get('distance_inversions')} "
              f"sun_normal_lux={ground.get('sun_normal_lux')} sky_horizontal_lux={ground.get('sky_horizontal_lux')} "
              f"sun_light={ground.get('sun_light_max_rel')} sun_t_vs_table={ground.get('sun_transmittance_vs_table_max_rel')} "
              f"inputs_current={int(bool(inputs_ok))}")
        failures.extend(f"{view}: {failure}" for failure in result["failures"])
    report = {
        "mode": "atmos_dump",
        "verify": bool(verify),
        "inputs_current": inputs_ok,
        "inputs_record": record,
        "game_args": game_args,
        "criteria": {
            "half_tolerance": ATMOS_HALF_TOLERANCE, "sample_tolerance": ATMOS_SAMPLE_TOLERANCE,
            "radiance_floor_cd_m2": ATMOS_RADIANCE_FLOOR, "fog_transmittance_tolerance": ATMOS_FOG_T_TOLERANCE,
            "aerial_transmittance_tolerance": ATMOS_AERIAL_T_TOLERANCE, "composite_tolerance": ATMOS_COMPOSITE_TOLERANCE,
            "sun_table_tolerance": ATMOS_SUN_TABLE_TOLERANCE, "lut_clamp_cd_m2": ATMOS_LUT_CLAMP,
            "elevation_range_deg": list(ATMOS_ELEVATION_RANGE),
            "distance_range_m": list(ATMOS_DISTANCE_RANGE),
        },
        "views": results,
        "failures": failures,
    }
    report_path = os.path.join(capture_dir, "atmos_dump_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=atmos_dump views={len(targets)} inputs_current={int(inputs_ok)} "
          f"report={report_path}")
    return 0 if not failures else 1


def atmos_sky_view_lookup(sky_view, sky_view_meta, direction):
    """sky-view の表（頭打ちの前の CPU の値）を、CPU と同じ引き方で向き direction（+Y が上）について引く"""
    horizon = sky_view_meta["horizon_elevation_radians"]
    elevation = np.arcsin(np.clip(direction[1], -1.0, 1.0))
    if elevation >= horizon:
        u = np.sqrt((elevation - horizon) / (0.5 * np.pi - horizon))
    else:
        u = -np.sqrt((horizon - elevation) / (0.5 * np.pi + horizon))
    difference = abs(np.remainder(np.arctan2(direction[2], direction[0]) - sky_view_meta["sun_azimuth_radians"] + np.pi,
                                  2.0 * np.pi) - np.pi)
    w = np.sqrt(np.clip(difference / np.pi, 0.0, 1.0))
    row_position = np.clip(u * 0.5 + 0.5, 0.0, 1.0) * (sky_view.shape[0] - 1)
    return atmos_table_bilinear(sky_view, row_position, w * (sky_view.shape[1] - 1))


def atmos_luminance(rgb):
    return float(np.asarray(rgb, dtype=np.float64) @ AR.LUMINANCE_WEIGHTS)


def atmos_ratio(a, b):
    a = atmos_luminance(a) if np.ndim(a) else float(a)
    b = atmos_luminance(b) if np.ndim(b) else float(b)
    return a / b if abs(b) > 1e-30 else float("nan")


def atmos_fog_settings(meta):
    fog = meta["height_fog"]
    return {"density_at_base_per_m": fog["density_at_base_per_m"], "base_height_m": fog["base_height_m"],
            "height_falloff_per_m": fog["height_falloff_per_m"], "phase_anisotropy": fog["phase_anisotropy"]}


def atmos_sun_position(latitude, longitude, utc_hours, day_of_year):
    """写真の撮影の太陽の高度と方位（北から時計回り、度）。NOAA の簡略式（誤差 0.5° 程度）"""
    gamma = 2.0 * np.pi / 365.0 * (day_of_year - 1 + (utc_hours - 12.0) / 24.0)
    equation = 229.18 * (0.000075 + 0.001868 * np.cos(gamma) - 0.032077 * np.sin(gamma) -
                         0.014615 * np.cos(2 * gamma) - 0.040849 * np.sin(2 * gamma))
    declination = (0.006918 - 0.399912 * np.cos(gamma) + 0.070257 * np.sin(gamma) - 0.006758 * np.cos(2 * gamma) +
                   0.000907 * np.sin(2 * gamma) - 0.002697 * np.cos(3 * gamma) + 0.00148 * np.sin(3 * gamma))
    solar_minutes = utc_hours * 60.0 + equation + 4.0 * longitude
    hour_angle = np.radians(solar_minutes / 4.0 - 180.0)
    lat = np.radians(latitude)
    sin_altitude = np.sin(lat) * np.sin(declination) + np.cos(lat) * np.cos(declination) * np.cos(hour_angle)
    altitude = np.arcsin(sin_altitude)
    cos_azimuth = (np.sin(declination) - np.sin(altitude) * np.sin(lat)) / (np.cos(altitude) * np.cos(lat))
    azimuth = np.degrees(np.arccos(np.clip(cos_azimuth, -1.0, 1.0)))
    if hour_angle > 0.0:
        azimuth = 360.0 - azimuth
    return float(np.degrees(altitude)), float(azimuth)


def atmos_view_directions(width, height, rows):
    """ref_ground_02 の視点（SceneCatalog.h の yaw・pitch、WastelandRoutine.cpp の縦の画角）の、指定した行の画素の中心の向き [rows, width, 3]"""
    yaw, pitch, vfov = ATMOS_REF_VIEW_POSE
    azimuth = np.radians(yaw + 180.0)
    p = np.radians(pitch)
    forward = np.array([np.sin(azimuth) * np.cos(p), np.sin(p), -np.cos(azimuth) * np.cos(p)])
    right = np.array([np.cos(azimuth), 0.0, np.sin(azimuth)])
    up = np.cross(right, forward)
    f = (height / 2) / np.tan(np.radians(vfov) / 2)
    us = (np.arange(width) + 0.5 - width / 2) / f
    vs = (height / 2 - (np.asarray(rows) + 0.5)) / f
    directions = forward[None, None, :] + us[None, :, None] * right[None, None, :] + vs[:, None, None] * up[None, None, :]
    return directions / np.linalg.norm(directions, axis=-1, keepdims=True)


def atmos_photo_sky(capture_dir, view, meta, sky_view, reference, variants, sun, sun_lux):
    """写真の空と、エンジンの撮影の空と、正解の空を、同じ視点の空の画素の帯（地形の上端より上）で並べる。
    写真と撮影の PNG は表示の値なので、比べるのは色度（r・b の割合）と、帯の明るさの上下の比だけ"""
    import CompareReference as CR
    from CompareLight import srgb_to_linear
    photo = Image.open(ATMOS_REF_PHOTO).convert("RGB")
    photo_panel, _, _ = CR.fit_photo(photo)
    capture = Image.open(os.path.join(capture_dir, f"{view}.png")).convert("RGB").resize(CR.PANEL_SIZE, Image.LANCZOS)
    width, height = CR.PANEL_SIZE
    result = {"bands": []}
    panels = {"photo": np.asarray(photo_panel), "engine_png": np.asarray(capture)}
    tops = {name: CR.find_terrain_top(rgb) for name, rgb in panels.items()}
    # 両方で空の行（写真と撮影の両方で地形より上の列が半分以上ある行）
    common_top = int(min(np.median(tops["photo"]), np.median(tops["engine_png"])))
    band_edges = np.linspace(0, max(common_top - 4, 8), ATMOS_REF_PHOTO_BANDS + 1).astype(int)
    for b0, b1 in zip(band_edges[:-1], band_edges[1:]):
        rows = np.arange(b0, b1)
        directions = atmos_view_directions(width, height, rows)
        mask = np.ones((len(rows), width), dtype=bool)
        for name in panels:
            mask &= rows[:, None] < tops[name][None, :] - 4
        if mask.sum() < 50:
            continue
        sample = directions[mask][:: max(1, int(mask.sum()) // 400)]
        elevation = float(np.degrees(np.arcsin(np.mean(directions[mask][:, 1]))))
        band = {"rows": [int(b0), int(b1)], "elevation_deg": elevation, "pixels": int(mask.sum())}
        for name, rgb in panels.items():
            linear = srgb_to_linear(rgb[b0:b1].astype(np.float64))[mask]
            band[name] = linear.mean(axis=0).tolist()
        band["engine_radiance"] = np.mean([atmos_sky_view_lookup(sky_view, meta["sky_view"], d) for d in sample],
                                          axis=0).tolist()
        for version in ("A", "B"):
            band[f"reference_{version}"] = (np.mean(reference.sky_radiance(meta["sky"]["observer_altitude_m"], sample, sun,
                                                                           version), axis=0) * sun_lux).tolist()
        # 設定を論文へ戻した正解（Mie の係数が論文の 1/25 の澄んだ空）
        band["reference_paper"] = (np.mean(variants["paper"].sky_radiance(100.0, sample, sun, "B"), axis=0) * sun_lux).tolist()
        for key in ("photo", "engine_png", "engine_radiance", "reference_A", "reference_B", "reference_paper"):
            rgb = np.array(band[key])
            band[f"{key}_chroma_rb"] = (rgb[[0, 2]] / max(float(rgb.sum()), 1e-30)).tolist()
        result["bands"].append(band)
    if len(result["bands"]) >= 2:
        top, bottom = result["bands"][0], result["bands"][-1]
        result["top_over_bottom"] = {key: atmos_ratio(top[key], bottom[key])
                                     for key in ("photo", "engine_png", "engine_radiance", "reference_A", "reference_B",
                                                 "reference_paper")}
    result["photo_sun"] = dict(zip(("altitude_deg", "azimuth_deg"), atmos_sun_position(*ATMOS_REF_PHOTO_TIME)))
    result["view_pose"] = {"yaw_deg": ATMOS_REF_VIEW_POSE[0], "pitch_deg": ATMOS_REF_VIEW_POSE[1],
                           "vfov_deg": ATMOS_REF_VIEW_POSE[2]}
    return result


def atmos_ref_view(capture_dir, view):
    failures = []
    meta_path = os.path.join(capture_dir, f"{view}.atmos.json")
    if not os.path.isfile(meta_path):
        return {"view": view, "pass": False, "failures": [f"{view}.atmos.json が無い"]}
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    arrays = {}
    for key in ("probe", "sky_view", "multiple_scattering"):
        values, reason = atmos_read_floats(capture_dir, meta[key])
        if reason:
            return {"view": view, "pass": False, "failures": [reason]}
        arrays[key] = values
    probe = arrays["probe"]
    sky_view = arrays["sky_view"]
    sky = meta["sky"]
    aerial = meta["aerial_perspective"]
    camera_y = meta["camera_position"][1]
    sun_lux = float(meta["ground_illuminance"]["sun_top_of_atmosphere_normal_lux"])
    sun = AR.sun_direction_from_angles(sky["sun_altitude_degrees"], sky["sun_azimuth_degrees"])
    observer = float(sky["observer_altitude_m"])
    haze_origin = camera_y + float(aerial["origin_altitude_m"])
    fog = atmos_fog_settings(meta)
    settings = AR.settings_from_dump(meta)
    started = time.time()
    reference = AR.Atmosphere(settings, ATMOS_REF_LEVEL)
    # 設定の違いを分けるための、1 つずつ論文の値へ戻した正解
    variants = {
        "mie_scale_height_1200": AR.with_changes(settings, "mie_scale_height_1200",
                                                 mie_scale_height_m=AR.PAPER_SETTINGS["mie_scale_height_m"]),
        "atmosphere_100km": AR.with_changes(settings, "atmosphere_100km",
                                            atmosphere_height_m=AR.PAPER_SETTINGS["atmosphere_height_m"]),
        "albedo_0": AR.with_changes(settings, "albedo_0", ground_albedo=[0.0, 0.0, 0.0]),
        "paper": dict(AR.PAPER_SETTINGS),
    }
    variant_atmospheres = {name: AR.Atmosphere(s, 1) for name, s in variants.items()}
    result = {"view": view, "reference_level": ATMOS_REF_LEVEL, "settings": settings, "sun_direction": sun.tolist(),
              "sun_top_of_atmosphere_lux": sun_lux, "observer_altitude_m": observer, "haze_origin_altitude_m": haze_origin}

    # 1. 空の方向別の輝度（cd/m²）: エンジンの sky-view の表（頭打ちの前）と正解（A・B）、設定を 1 つずつ戻した正解（B）
    sky_rows = []
    worst_sky_b = 0.0
    for elevation in ATMOS_REF_ELEVATIONS:
        for offset in ATMOS_REF_OFFSETS:
            direction = AR.direction_from_sun_offset(sun, np.array([elevation]), np.array([offset]))
            engine = atmos_sky_view_lookup(sky_view, meta["sky_view"], direction[0])
            row = {"elevation_deg": elevation, "azimuth_offset_deg": offset, "engine": engine.tolist()}
            for version in ("A", "B"):
                row[f"reference_{version}"] = (reference.sky_radiance(observer, direction, sun, version)[0] * sun_lux).tolist()
            for name, atmosphere in variant_atmospheres.items():
                altitude = 100.0 if name == "paper" else observer
                row[f"variant_{name}"] = (atmosphere.sky_radiance(altitude, direction, sun, "B")[0] * sun_lux).tolist()
            row["engine_over_B"] = atmos_ratio(row["engine"], row["reference_B"])
            row["engine_over_A"] = atmos_ratio(row["engine"], row["reference_A"])
            row["A_over_B"] = atmos_ratio(row["reference_A"], row["reference_B"])
            row["engine_chroma_rb"] = (np.array(row["engine"])[[0, 2]] / sum(row["engine"])).tolist()
            row["reference_chroma_rb"] = (np.array(row["reference_B"])[[0, 2]] / sum(row["reference_B"])).tolist()
            worst_sky_b = max(worst_sky_b, abs(np.log(row["engine_over_B"])))
            sky_rows.append(row)
    result["sky"] = sky_rows

    # 2. 地表の太陽と空の照度（lux）
    # エンジンの地表の太陽の透過率は高度 0 の値（空の観測点の高度 observer とは別）
    sun_t = reference.sun_transmittance(0.0, sun)
    sin_altitude = sun[1]
    illuminance = {"engine_sun_horizontal": meta["ground_illuminance"]["sun_horizontal_lux"],
                   "engine_sky_horizontal": meta["ground_illuminance"]["sky_horizontal_lux"],
                   "engine_sun_ground_transmittance": meta["ground_illuminance"]["sun_ground_transmittance"],
                   "reference_sun_ground_transmittance": sun_t.tolist(),
                   "reference_sun_transmittance_at_observer": reference.sun_transmittance(observer, sun).tolist(),
                   "reference_sun_transmittance_at_haze_origin": reference.sun_transmittance(haze_origin, sun).tolist(),
                   "reference_sun_horizontal": (sun_t * sin_altitude * sun_lux).tolist()}
    for version in ("A", "B"):
        illuminance[f"reference_sky_horizontal_{version}"] = (
            reference.sky_horizontal_irradiance(observer, sun, version) * sun_lux).tolist()
    illuminance["reference_sky_horizontal_B_at_ground"] = (
        reference.sky_horizontal_irradiance(0.0, sun, "B") * sun_lux).tolist()
    illuminance["reference_sky_horizontal_B_at_haze_origin"] = (
        reference.sky_horizontal_irradiance(haze_origin, sun, "B") * sun_lux).tolist()
    illuminance["engine_sun_over_sky"] = atmos_ratio(illuminance["engine_sun_horizontal"],
                                                     illuminance["engine_sky_horizontal"])
    for version in ("A", "B"):
        illuminance[f"reference_sun_over_sky_{version}"] = atmos_ratio(illuminance["reference_sun_horizontal"],
                                                                       illuminance[f"reference_sky_horizontal_{version}"])
    sun_t_error = atmos_relative_error(illuminance["engine_sun_ground_transmittance"], sun_t, 1e-3)
    illuminance["sun_transmittance_max_rel"] = sun_t_error
    result["illuminance"] = illuminance
    if sun_t_error > ATMOS_REF_SAME_MODEL_TOLERANCE:
        failures.append(f"地表の太陽の透過率（同じ式）が正解と {sun_t_error:.3g} 違う（設定の読み違い）")

    # 3. 距離別の透過率と散乱光: 地表への視線（目の 1.7 m 下の水平面）と水平・仰角 2° の視線。
    #    エンジン（行 0・3・4・6）と、同じ媒質を正しく積んだ正解（Mie の密度 × 0.22・300 m から先・高さフォグを足す）と、
    #    空の大気そのまま（密度 × 1・0 m から・霧なし）の正解
    rays = meta["probe"]["rays"]
    distance_rows = []
    worst_fog_t = 0.0
    worst_aerial_t = 0.0
    for index, ray in enumerate(rays):
        is_ground = ray["kind"] == "ground"
        if not is_ground and ray["elevation_degrees"] not in ATMOS_REF_HAZE_ELEVATIONS:
            continue
        direction = np.array(ray["direction"], dtype=np.float64)
        direction /= np.linalg.norm(direction)
        distance = float(ray["distance_m"])
        same, same_t = reference.march(haze_origin, direction[None, :], sun, "B", start=0.0, end=distance,
                                       steps=ATMOS_REF_SEGMENT_STEPS, mie_scale=float(aerial["mie_density_scale"]),
                                       fog=fog, origin_world_y=camera_y, air_start=float(aerial["start_distance_m"]))
        # 霧だけ・空気遠近だけの透過率（同じ媒質）
        _, fog_only_t = reference.march(haze_origin, direction[None, :], sun, "B", start=0.0, end=distance,
                                        steps=ATMOS_REF_SEGMENT_STEPS, mie_scale=0.0, fog=fog, origin_world_y=camera_y,
                                        air_start=distance + 1.0)
        _, air_only_t = reference.march(haze_origin, direction[None, :], sun, "B", start=0.0, end=distance,
                                        steps=ATMOS_REF_SEGMENT_STEPS, mie_scale=float(aerial["mie_density_scale"]),
                                        air_start=float(aerial["start_distance_m"]))
        physical, physical_t = reference.march(haze_origin, direction[None, :], sun, "B", start=0.0, end=distance,
                                               steps=ATMOS_REF_SEGMENT_STEPS)
        physical_a, _ = reference.march(haze_origin, direction[None, :], sun, "A", start=0.0, end=distance,
                                        steps=ATMOS_REF_SEGMENT_STEPS)
        # 光学的深さの内訳（Rayleigh・Mie・オゾン、空の大気そのまま）
        t = (np.arange(ATMOS_REF_SEGMENT_STEPS) + 0.5) / ATMOS_REF_SEGMENT_STEPS * distance
        dt = distance / ATMOS_REF_SEGMENT_STEPS
        radius = reference.medium.bottom + haze_origin
        r = np.sqrt((t * direction[0]) ** 2 + (radius + t * direction[1]) ** 2 + (t * direction[2]) ** 2)
        rayleigh_density, mie_density, ozone_density = reference.medium.densities(r - reference.medium.bottom)
        physical_mie_depth = float(np.sum(mie_density) * dt * reference.medium.mie_extinction)
        physical_rayleigh_depth = (np.sum(rayleigh_density) * dt * reference.medium.rayleigh).tolist()
        physical_ozone_depth = (np.sum(ozone_density) * dt * reference.medium.ozone).tolist()
        start = max(float(aerial["start_distance_m"]), 0.0)
        engine_origin = camera_y + float(aerial["origin_altitude_m"])
        engine_mie_depth = float(aerial["distance_scale"] * aerial["shader_mie_extinction_per_m"] *
                                 atmos_density_integral(engine_origin, direction[1], start, distance, sky["mie_scale_height_m"]))
        engine_fog_t = float(probe[0, index, 0])
        engine_fog_depth = -np.log(max(engine_fog_t, 1e-30))
        engine_aerial_t = probe[3, index, :3]
        opacity = float(probe[0, index, 1])
        row = {
            "kind": ray["kind"], "azimuth_offset_deg": ray["azimuth_offset_degrees"],
            "elevation_deg": ray["elevation_degrees"], "distance_m": distance,
            "engine": {"fog_transmittance": engine_fog_t, "aerial_transmittance": engine_aerial_t.tolist(),
                       "total_transmittance": (engine_aerial_t * engine_fog_t).tolist(),
                       "fog_sky_color_times_opacity": (probe[1, index, :3] * opacity).tolist(),
                       "fog_sun_single": probe[2, index, :3].tolist(),
                       "aerial_scattering_times_fog_t": (probe[4, index, :3] * engine_fog_t).tolist(),
                       "total_scattering": probe[6, index, :3].tolist()},
            "same_media": {"fog_transmittance": fog_only_t[0].tolist(), "aerial_transmittance": air_only_t[0].tolist(),
                           "total_transmittance": same_t[0].tolist(),
                           "fog_sun": (same["fog_sun"][0] * sun_lux).tolist(),
                           "fog_multiple": (same["fog_multiple"][0] * sun_lux).tolist(),
                           "air_sun": (same["air_sun"][0] * sun_lux).tolist(),
                           "air_multiple": (same["air_multiple"][0] * sun_lux).tolist(),
                           "total_scattering": (same["total"][0] * sun_lux).tolist()},
            "physical": {"transmittance": physical_t[0].tolist(),
                         "total_scattering": (physical["total"][0] * sun_lux).tolist(),
                         "total_scattering_A": (physical_a["total"][0] * sun_lux).tolist()},
            "optical_depth": {"engine_fog": engine_fog_depth, "engine_aerial_mie": engine_mie_depth,
                              "engine_aerial": (-np.log(np.maximum(engine_aerial_t, 1e-30))).tolist(),
                              "physical_mie": physical_mie_depth, "physical_rayleigh": physical_rayleigh_depth,
                              "physical_ozone": physical_ozone_depth},
        }
        row["engine_over_same_media_scattering"] = atmos_ratio(row["engine"]["total_scattering"],
                                                               row["same_media"]["total_scattering"])
        row["engine_over_physical_scattering"] = atmos_ratio(row["engine"]["total_scattering"],
                                                             row["physical"]["total_scattering"])
        row["fog_plus_aerial_mie_over_physical_mie"] = (engine_fog_depth + engine_mie_depth) / max(physical_mie_depth, 1e-30)
        worst_fog_t = max(worst_fog_t, abs(engine_fog_t - float(np.mean(fog_only_t[0]))))
        worst_aerial_t = max(worst_aerial_t, atmos_relative_error(engine_aerial_t, air_only_t[0], 1e-3))
        distance_rows.append(row)
    result["distances"] = distance_rows
    # 同じ媒質の透過率がエンジンと合う（平らな地面の解析式と球の上の数値積分の差だけ）ことで、媒質の読み違いが無いことを確かめる
    result["same_media_fog_transmittance_max_abs"] = worst_fog_t
    result["same_media_aerial_transmittance_max_rel"] = worst_aerial_t
    if worst_fog_t > ATMOS_REF_SAME_MODEL_TOLERANCE:
        failures.append(f"高さフォグの透過率（同じ媒質）が正解と {worst_fog_t:.3g} 違う")
    if worst_aerial_t > ATMOS_REF_SAME_MODEL_TOLERANCE:
        failures.append(f"空気遠近の透過率（同じ媒質）が正解と {worst_aerial_t:.3g} 違う")

    # 4. 多重散乱の表 Ψ_ms（エンジンの表の格子の点で、正解 A・B と比べる）
    ms_meta = meta["multiple_scattering"]
    psi_rows = []
    for i, altitude in enumerate(ms_meta["altitude_m"]):
        if altitude > ATMOS_REF_PSI_MAX_ALTITUDE:
            continue
        for j, cosine in enumerate(ms_meta["sun_cos_zenith"]):
            if cosine < ATMOS_REF_PSI_MIN_COSINE:
                continue
            psi_rows.append({"altitude_m": altitude, "sun_cosine": cosine, "engine": arrays["multiple_scattering"][i, j].tolist()})
    psi_a, psi_b = reference.evaluate_psi([r["altitude_m"] for r in psi_rows], [r["sun_cosine"] for r in psi_rows])
    for row, a, b in zip(psi_rows, psi_a, psi_b):
        row["reference_A"] = a.tolist()
        row["reference_B"] = b.tolist()
        row["engine_over_A"] = atmos_ratio(row["engine"], a)
        row["engine_over_B"] = atmos_ratio(row["engine"], b)
    result["psi"] = psi_rows

    # 5. 写真の空との並べ
    result["photo"] = atmos_photo_sky(capture_dir, view, meta, sky_view, reference, variant_atmospheres, sun, sun_lux)
    result["seconds"] = time.time() - started
    result["sky_engine_over_B_max_log"] = worst_sky_b
    values = [worst_sky_b, worst_fog_t, worst_aerial_t]
    if not all(np.isfinite(v) for v in values):
        failures.append("比べた値に NaN がある")
    result["pass"] = not failures
    result["failures"] = failures
    return result


def run_atmos_ref(capture_dir, views, verify):
    failures = []
    inputs_ok, input_failures, record = check_inputs(capture_dir)
    if verify and not inputs_ok:
        failures.extend(input_failures)
    # 正解の収束（AtmosphereReference.py --verify の結果）が合格で、同じ設定のものであること
    convergence_path = os.path.join(AR.DEFAULT_OUT, "atmosphere_reference_verify.json")
    convergence = None
    if os.path.isfile(convergence_path):
        with open(convergence_path, encoding="utf-8") as f:
            convergence = json.load(f)
    if convergence is None or convergence.get("failures"):
        failures.append(f"正解の収束の確かめ（AtmosphereReference.py --verify）が合格していない: {convergence_path}")
    targets = views if views else [ATMOS_REF_DEFAULT_VIEW]
    results = []
    for view in targets:
        result = atmos_ref_view(capture_dir, view)
        if convergence is not None and result.get("settings") is not None:
            engine_set = convergence.get("sets", {}).get("engine", {})
            if engine_set.get("settings") != json.loads(json.dumps(result["settings"])):
                failures.append(f"{view}: 収束を確かめた正解の設定が、この書き出しの設定と違う")
        results.append(result)
        failures.extend(f"{view}: {failure}" for failure in result.get("failures", []))
        if "sky" in result:
            ill = result["illuminance"]
            print(f"REFERENCE_CHECK atmos_ref view={view} level={result['reference_level']} "
                  f"sky_engine_over_B_max_log={result['sky_engine_over_B_max_log']:.3f} "
                  f"sun_over_sky engine={ill['engine_sun_over_sky']:.3f} A={ill['reference_sun_over_sky_A']:.3f} "
                  f"B={ill['reference_sun_over_sky_B']:.3f} sun_t_max_rel={ill['sun_transmittance_max_rel']:.2e} "
                  f"fog_t_max_abs={result['same_media_fog_transmittance_max_abs']:.2e} "
                  f"aerial_t_max_rel={result['same_media_aerial_transmittance_max_rel']:.2e} "
                  f"seconds={result['seconds']:.0f}")
    report = {"mode": "atmos_ref", "verify": bool(verify), "inputs_current": inputs_ok, "inputs_record": record,
              "criteria": {"same_model_tolerance": ATMOS_REF_SAME_MODEL_TOLERANCE, "reference_level": ATMOS_REF_LEVEL},
              "convergence_report": convergence_path, "views": results, "failures": failures}
    report_path = os.path.join(capture_dir, "atmos_ref_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=atmos_ref views={len(targets)} inputs_current={int(inputs_ok)} "
          f"report={report_path}")
    return 0 if not failures else 1


def blur_decode_srgb(encoded):
    return np.where(encoded <= 0.04045, encoded / 12.92, np.power((encoded + 0.055) / 1.055, 2.4))


def blur_encode_srgb(linear):
    linear = np.clip(linear, 0.0, 1.0)
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(linear, 1.0 / 2.4) - 0.055)


def blur_load_display(png_path, scale):
    """PNG の表示の値（0〜1、(高さ, 幅, 3)）。scale が 2 以上なら線形の光で scale×scale を平均して符号化し直す（超標本化の縮小）"""
    encoded = np.asarray(Image.open(png_path).convert("RGB"), dtype=np.float64) / 255.0
    if scale > 1:
        h, w = encoded.shape[0] // scale, encoded.shape[1] // scale
        linear = blur_decode_srgb(encoded[:h * scale, :w * scale]).reshape(h, scale, w, scale, 3).mean(axis=(1, 3))
        encoded = blur_encode_srgb(linear)
    return encoded


def blur_gaussian(image, sigma):
    """正確なガウス（半径 3σ、端は鏡映）。σ = 0 はそのまま返す"""
    if sigma <= 0.0:
        return image
    radius = int(np.ceil(3.0 * sigma))
    taps = np.arange(-radius, radius + 1, dtype=np.float64)
    kernel = np.exp(-0.5 * (taps / sigma) ** 2)
    kernel /= kernel.sum()
    out = image
    for axis in (0, 1):
        pad = [(0, 0)] * out.ndim
        pad[axis] = (radius, radius)
        padded = np.pad(out, pad, mode="reflect")
        n = out.shape[axis]
        acc = np.zeros_like(out)
        for i, weight in enumerate(kernel):
            acc += weight * (padded[i:i + n] if axis == 0 else padded[:, i:i + n])
        out = acc
    return out


def blur_bands(luma):
    """隣り合うガウスの差の帯域（細かい順）"""
    blurred = [blur_gaussian(luma, sigma) for sigma in BLUR_SIGMAS]
    return [blurred[k] - blurred[k + 1] for k in range(len(BLUR_SIGMAS) - 1)]


def blur_regions(reference_dir, view):
    """正解の深度と法線から、帯域ごとに数える画素の印（遠景・崖）と記録"""
    depth = read_pfm_any(os.path.join(reference_dir, f"{view}.aov.depth.pfm"))[..., 0].astype(np.float64)
    normal = read_pfm_any(os.path.join(reference_dir, f"{view}.aov.normal.pfm")).astype(np.float64)
    s = BLUR_REFERENCE_SCALE
    h, w = depth.shape[0] // s, depth.shape[1] // s
    blocks = depth[:h * s, :w * s].reshape(h, s, w, s)
    finite = np.isfinite(blocks).all(axis=(1, 3))
    nearest = np.where(np.isfinite(blocks), blocks, np.inf).min(axis=(1, 3))
    far = finite & (nearest >= BLUR_FAR_MIN_M)
    # 2×2 のすべてが急な面なら崖
    normal_y = np.abs(normal[:h * s, :w * s, 1]).reshape(h, s, w, s).max(axis=(1, 3))
    steep = normal_y < BLUR_CLIFF_NORMAL_Y
    outside = (~far).astype(np.float64)
    far_masks, cliff_masks = [], []
    for k in range(len(BLUR_SIGMAS) - 1):
        margin = int(np.ceil(2.0 * BLUR_SIGMAS[k + 1]))
        keep = far & ~(box_blur(outside, margin) > 1e-9)
        far_masks.append(keep)
        cliff_masks.append(keep & steep)
    record = {"far_px": int(far.sum()), "cliff_px": int((far & steep).sum()), "sky_ratio": float(1.0 - finite.mean()),
              "far_px_per_band": [int(m.sum()) for m in far_masks],
              "cliff_px_per_band": [int(m.sum()) for m in cliff_masks]}
    return far_masks, cliff_masks, record


def blur_compare(reference_bands, case_bands, masks):
    """帯域ごとに、条件の帯域を正解の帯域へ射影した利得（細部の残り）と、正解に無い成分の量（正解の量に対する比。エイリアス）"""
    rows = []
    for k, (reference, case, mask) in enumerate(zip(reference_bands, case_bands, masks)):
        r = reference[mask]
        c = case[mask]
        reference_energy = float(np.mean(r * r)) if r.size else 0.0
        if reference_energy <= 0.0:
            return None
        gain = float(np.mean(c * r)) / reference_energy
        residual = float(np.mean((c - gain * r) ** 2))
        rows.append({"band": k, "sigma": [BLUR_SIGMAS[k], BLUR_SIGMAS[k + 1]], "pixels": int(r.size),
                     "reference_rms": float(np.sqrt(reference_energy)), "case_rms": float(np.sqrt(np.mean(c * c))),
                     "detail_kept": gain, "detail_loss": 1.0 - gain, "alias": residual / reference_energy})
    fine = [rows[k] for k in BLUR_FINE_BANDS]
    weights = [row["reference_rms"] ** 2 for row in fine]
    kept = sum(row["detail_kept"] * weight for row, weight in zip(fine, weights)) / sum(weights)
    alias = sum(row["alias"] * weight for row, weight in zip(fine, weights)) / sum(weights)
    return {"bands": rows, "fine_detail_loss": 1.0 - kept, "fine_alias": alias}


def blur_forbidden_logs(expected_logs):
    return tuple(f for f in BLUR_BASE_FORBIDDEN_LOGS if not any(e.startswith(f) for e in expected_logs))


def blur_case_check(case_dir, expected_args, expected_logs, size, require_resident, views=BLUR_VIEWS):
    """組の起動引数・ログの印・仮想テクスチャの使用・撮った時点の常駐と描画の溢れ・PNG の大きさを確かめる。(失敗の一覧, 記録)"""
    failures = []
    args = plate_capture_args(case_dir)
    if args is None:
        return ["metrics.json が無い"], {}
    controlled = sorted(a for a in args if a.startswith(BLUR_CONTROLLED_PREFIXES))
    if controlled != sorted(expected_args):
        failures.append(f"条件を替える起動引数が約束と違う: {controlled}（約束 {list(expected_args)}）")
    forbidden = blur_forbidden_logs(expected_logs)
    seen = set()
    vt_lines = 0
    vt_used_max = 0.0
    # 視点ごとの、撮った（CAPTURE_VIEWS ... saved）時点のジオメトリのページの状態
    pages_at_capture = {}
    last_pages = None
    # 視点ごとの、撮った時点の描画の溢れ（描かれなかったクラスタ・可視クラスタの列の溢れ）
    dropped_at_capture = {}
    last_capacity = None
    window_evicted = set()
    for log_name in sorted({view_game_log(case_dir, view) or "" for view in views}):
        path = os.path.join(case_dir, log_name) if log_name else ""
        if not log_name or not os.path.isfile(path):
            failures.append(f"Game のログが無い: {log_name or '（metrics.json に game_log が無い）'}")
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                for needle in expected_logs + forbidden:
                    if needle in line:
                        seen.add(needle)
                if BLUR_VT_LOG_KEY in line:
                    found = re.search(r"vt_used_mb=(\d+(?:\.\d+)?)", line)
                    if found:
                        vt_lines += 1
                        vt_used_max = max(vt_used_max, float(found.group(1)))
                found = BLUR_PAGES_RE.search(line)
                if found:
                    values = [int(v) for v in found.groups()]
                    last_pages = {"resident": values[0], "uploading": values[1], "ready": values[2], "reading": values[3],
                                  "wanted": values[4], "evicted": values[5]}
                    window_evicted.add(values[5])
                found = BLUR_CAPACITY_RE.search(line)
                if found:
                    last_capacity = {"draw_dropped": int(found.group(1)), "visible_list_overflow": int(found.group(2))}
                if re.search(r"CAPTURE_VIEWS view=\S+ switched ", line):
                    last_capacity = None
                if re.search(r"CAPTURE_VIEWS view=\S+ (?:switched|settle baseline) ", line):
                    window_evicted = {last_pages["evicted"]} if last_pages else set()
                found = re.search(r"CAPTURE_VIEWS view=(\S+) saved ", line)
                if found:
                    pages_at_capture[found.group(1)] = dict(last_pages or {}, settle_evicted=sorted(window_evicted))
                    dropped_at_capture[found.group(1)] = last_capacity
    missing = [n for n in expected_logs if n not in seen]
    if missing:
        failures.append(f"Game のログに条件の印が無い: {missing}")
    unexpected = [n for n in forbidden if n in seen]
    if unexpected:
        failures.append(f"Game のログに約束にない差し替えの印がある: {unexpected}")
    if vt_lines == 0:
        failures.append("VRAM_POOLS の行が無い（仮想テクスチャの使用を確かめられない）")
    elif vt_used_max > 0.0:
        failures.append(f"仮想テクスチャが使われている（vt_used_mb の最大 {vt_used_max:g}）。撮る前に要求が満ちたことの確かめが要る")
    for view in views:
        # 落ち着き待ちは、目標に収まらない要求や追い出しの続く作業集合を待たない。撮った時点で進行中のページが残るか、
        # 落ち着きの基準から保存までに追い出しがあれば拒む（base は既定の取り分のまま撮るので記録だけ）
        pages = pages_at_capture.get(view)
        if pages is None:
            failures.append(f"{view}: Game のログに撮影の保存の行が無い")
        else:
            in_flight = {k: pages.get(k) for k in ("wanted", "reading", "ready", "uploading")}
            pages["resident_complete"] = bool(pages.get("resident") is not None and not any(in_flight.values())
                                              and len(pages["settle_evicted"]) == 1)
            if require_resident and not pages["resident_complete"]:
                failures.append(f"{view}: 撮った時点でジオメトリのページが常駐しきっていない（進行中 {in_flight}、"
                                f"落ち着きの基準から保存までの追い出しの数 {pages['settle_evicted']}）")
        dropped = dropped_at_capture.get(view)
        if pages is not None and dropped is None:
            failures.append(f"{view}: 視点を替えてから保存までに MEGA_CAPACITY の行が無い（描画の溢れを確かめられない）")
        elif dropped is not None and any(dropped.values()):
            failures.append(f"{view}: 撮った時点で描かれないクラスタがある（{dropped}）")
        png = os.path.join(case_dir, f"{view}.png")
        if not os.path.isfile(png):
            failures.append(f"{view}.png が無い")
        elif Image.open(png).size != size:
            failures.append(f"{view}.png の大きさ {Image.open(png).size} が {size} でない")
    return failures, {"game_args": args, "vt_pool_lines": vt_lines, "vt_used_mb_max": vt_used_max,
                      "residency_required": bool(require_resident), "geometry_pages_at_capture": pages_at_capture,
                      "draw_overflow_at_capture": dropped_at_capture}


def blur_write_crops(path, tiles, center):
    """範囲の中心の周りを切り出し、2 倍に広げて 3 列に並べる（名前を左上に書く）"""
    from PIL import ImageDraw
    crop_w, crop_h = 384, 216
    cy, cx = center
    top = int(np.clip(cy - crop_h // 2, 0, BLUR_SIZE[1] - crop_h))
    left = int(np.clip(cx - crop_w // 2, 0, BLUR_SIZE[0] - crop_w))
    columns = 3
    rows = (len(tiles) + columns - 1) // columns
    sheet = Image.new("RGB", (crop_w * 2 * columns, crop_h * 2 * rows), (0, 0, 0))
    for index, (name, display) in enumerate(tiles):
        crop = (np.clip(display[top:top + crop_h, left:left + crop_w], 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
        tile = Image.fromarray(crop).resize((crop_w * 2, crop_h * 2), Image.NEAREST)
        ImageDraw.Draw(tile).text((6, 6), name, fill=(255, 255, 0))
        sheet.paste(tile, ((index % columns) * crop_w * 2, (index // columns) * crop_h * 2))
    sheet.save(path)
    return {"top": top, "left": left, "width": crop_w, "height": crop_h}


def run_blur_static(capture_dir, verify, engine_dirs=None, reference_dir=None, expect=None):
    """遠景のぼけの静止の比較。組の撮影は engine_dirs（組ごとに <撮影先>/<組>/metrics.json を持つ最初の撮影先。省くと
    capture_dir）、正解の ref2x は reference_dir（省くと組と同じ探し方）から読み、結果は capture_dir に書く。
    どの組（ref2x を含む）もエンジンの実装（RefSplit.py）が今と一致しないと古いとして落とし、組どうしで揃っていることも見る"""
    import RefSplit
    started = time.time()
    failures = []
    engine_dirs = engine_dirs or [capture_dir]
    reference_dir = reference_dir or RefSplit.resolve_group_dir(engine_dirs, BLUR_REFERENCE)
    groups = [(BLUR_REFERENCE, BLUR_REFERENCE_ARGS, BLUR_REFERENCE_LOGS,
               (BLUR_SIZE[0] * BLUR_REFERENCE_SCALE, BLUR_SIZE[1] * BLUR_REFERENCE_SCALE), True)]
    groups += [(case["name"], case["args"], case["logs"], BLUR_SIZE, case["resident"]) for case in BLUR_CASES]
    group_records = {}
    input_hashes = set()
    exe_hashes = set()
    inputs_all_current = True
    for name, expected_args, expected_logs, size, require_resident in groups:
        group_dir = reference_dir if name == BLUR_REFERENCE else RefSplit.resolve_group_dir(engine_dirs, name)
        if not os.path.isdir(group_dir):
            failures.append(f"{name}: 撮影の組が無い（{group_dir}）")
            inputs_all_current = False
            continue
        inputs_ok, input_failures, record = split_engine_check(group_dir)
        inputs_all_current = inputs_all_current and inputs_ok
        if verify and not inputs_ok:
            failures.extend(f"{name}: {failure}" for failure in input_failures)
        if record is not None:
            input_hashes.add(record.get("identity"))
            # ソースのハッシュは撮影の後のファイルから作るので、走った Game.exe の一致はその中身のハッシュで照らす
            if record.get("game_exe_sha256"):
                exe_hashes.add(record["game_exe_sha256"])
            else:
                failures.append(f"{name}: inputs.json に撮影に使った Game.exe のハッシュが無い（--record-inputs --game-exe）")
        group_failures, info = blur_case_check(group_dir, tuple(expected_args), tuple(expected_logs), size,
                                               require_resident)
        failures.extend(f"{name}: {failure}" for failure in group_failures)
        group_records[name] = {"inputs_current": inputs_ok, "dir": rel_dir(group_dir),
                               "game_exe_sha256": (record or {}).get("game_exe_sha256"), **info}
    if len(input_hashes) > 1:
        failures.append(f"組ごとのエンジンの実装が揃っていない（{len(input_hashes)} 通り。同じ実装で撮った組だけを比べる）")
    if len(exe_hashes) > 1:
        failures.append(f"組ごとの Game.exe のハッシュが揃っていない（{len(exe_hashes)} 通り。同じビルドで撮った組だけを比べる）")
    views = []
    for view in BLUR_VIEWS:
        entry = {"view": view}
        views.append(entry)
        needed = [os.path.join(reference_dir, f"{view}.{suffix}") for suffix in ("png", "aov.depth.pfm", "aov.normal.pfm")]
        if not all(os.path.isfile(p) for p in needed) or any(f.startswith(BLUR_REFERENCE + ":") for f in failures):
            failures.append(f"{view}: 正解の撮影（PNG・深度・法線）が揃っていないので比べない")
            continue
        far_masks, cliff_masks, region = blur_regions(reference_dir, view)
        entry["region"] = region
        if min(region["far_px_per_band"]) < BLUR_MIN_REGION_PX:
            failures.append(f"{view}: 遠景の範囲の画素が {min(region['far_px_per_band'])} で {BLUR_MIN_REGION_PX} に足りない")
            continue
        reference_display = blur_load_display(os.path.join(reference_dir, f"{view}.png"), BLUR_REFERENCE_SCALE)
        reference_luma = reference_display @ LUMA
        reference_bands = blur_bands(reference_luma)
        # 測り方の較正（ぼかした正解と、雑音を足した正解）
        blurred_bands = blur_bands(blur_gaussian(reference_luma, BLUR_CALIB_BLUR_SIGMA))
        noise_rms = float(np.sqrt(np.mean(reference_bands[0][far_masks[0]] ** 2)))
        rng = np.random.default_rng(BLUR_CALIB_SEED)
        noisy_bands = blur_bands(reference_luma + rng.normal(0.0, noise_rms, reference_luma.shape))
        calibration = {"blur": blur_compare(reference_bands, blurred_bands, far_masks),
                       "noise": blur_compare(reference_bands, noisy_bands, far_masks), "noise_rms": noise_rms}
        entry["calibration"] = calibration
        blur_row = calibration["blur"]
        noise_row = calibration["noise"]
        if blur_row is None or noise_row is None:
            failures.append(f"{view}: 較正を求められない（正解の帯域が空）")
        else:
            if blur_row["bands"][0]["detail_loss"] < BLUR_CALIB_BLUR_MIN_LOSS or \
                    blur_row["fine_alias"] > BLUR_CALIB_BLUR_MAX_ALIAS:
                failures.append(f"{view}: 較正（ぼかし）が細部の欠けに数えられない（帯域 0 の欠け "
                                f"{blur_row['bands'][0]['detail_loss']:.3f}・エイリアス {blur_row['fine_alias']:.3f}）")
            if abs(noise_row["bands"][0]["detail_kept"] - 1.0) > BLUR_CALIB_NOISE_GAIN or \
                    noise_row["bands"][0]["alias"] < BLUR_CALIB_NOISE_MIN_ALIAS:
                failures.append(f"{view}: 較正（雑音）がエイリアスに数えられない（帯域 0 の残り "
                                f"{noise_row['bands'][0]['detail_kept']:.3f}・エイリアス {noise_row['bands'][0]['alias']:.3f}）")
        cases = {}
        displays = {"ref2x": reference_display}
        case_bands_by_name = {}
        for case in BLUR_CASES:
            png = os.path.join(capture_dir, case["name"], f"{view}.png")
            if not os.path.isfile(png) or Image.open(png).size != BLUR_SIZE:
                continue
            display = blur_load_display(png, 1)
            displays[case["name"]] = display
            bands = blur_bands(display @ LUMA)
            case_bands_by_name[case["name"]] = bands
            far_result = blur_compare(reference_bands, bands, far_masks)
            cliff_result = blur_compare(reference_bands, bands, cliff_masks) \
                if min(region["cliff_px_per_band"]) > 0 else None
            if far_result is None:
                failures.append(f"{view}: {case['name']} を比べられない")
                continue
            values = [far_result["fine_detail_loss"], far_result["fine_alias"]]
            if cliff_result is not None:
                values += [cliff_result["fine_detail_loss"], cliff_result["fine_alias"]]
            if not all(np.isfinite(values)):
                failures.append(f"{view}: {case['name']} の値に NaN・Inf がある")
            cases[case["name"]] = {"factor": case["factor"], "far": far_result, "cliff": cliff_result}
        entry["cases"] = cases
        # 霞が細部の見えを下げる量: 霞ありの帯域の量 ÷ 霞なしの帯域の量（同じ範囲・同じ取り分。正解との比ではない）
        if BLUR_BASELINE in case_bands_by_name and "haze-off" in case_bands_by_name:
            entry["haze_contrast"] = [
                float(np.sqrt(np.mean(case_bands_by_name[BLUR_BASELINE][k][far_masks[k]] ** 2) /
                              np.mean(case_bands_by_name["haze-off"][k][far_masks[k]] ** 2)))
                for k in range(len(BLUR_SIGMAS) - 1)]
        cy, cx = np.argwhere(cliff_masks[0] if region["cliff_px_per_band"][0] > 0 else far_masks[0]).mean(axis=0)
        crops_path = os.path.join(capture_dir, f"blur_{view}.png")
        order = ["ref2x", "base", BLUR_BASELINE, "finest-lod", "aa-none", "aa-taa", "mip-bias-m1", "aniso-1", "scale-075",
                 "haze-off"]
        entry["crops"] = blur_write_crops(crops_path, [(n, displays[n]) for n in order if n in displays], (cy, cx))
        entry["crops_png"] = crops_path
        missing_cases = [case["name"] for case in BLUR_CASES if case["name"] not in cases]
        if missing_cases:
            failures.append(f"{view}: 比べられなかった条件がある: {missing_cases}")
        for name, result in cases.items():
            cliff = result["cliff"]
            print(f"REFERENCE_CHECK blur_static view={view} case={name} "
                  f"far_loss={result['far']['fine_detail_loss']:.3f} far_alias={result['far']['fine_alias']:.3f} "
                  + (f"cliff_loss={cliff['fine_detail_loss']:.3f} cliff_alias={cliff['fine_alias']:.3f} "
                     if cliff else "")
                  + "kept_by_band=" + ",".join(f"{row['detail_kept']:.2f}" for row in result["far"]["bands"]))
        if blur_row is not None and noise_row is not None:
            print(f"REFERENCE_CHECK blur_static view={view} calibration blur_loss0={blur_row['bands'][0]['detail_loss']:.3f} "
                  f"blur_alias={blur_row['fine_alias']:.3f} noise_kept0={noise_row['bands'][0]['detail_kept']:.3f} "
                  f"noise_alias0={noise_row['bands'][0]['alias']:.3f} far_px={region['far_px']} cliff_px={region['cliff_px']}")
    # 条件ごとの効き（2 視点の平均。取り分だけを広げた組との差。base の差は既定の取り分の効き）
    effects = []
    for case in BLUR_CASES:
        if case["name"] == BLUR_BASELINE:
            continue
        deltas = []
        for entry in views:
            cases = entry.get("cases") or {}
            if case["name"] in cases and BLUR_BASELINE in cases:
                baseline = cases[BLUR_BASELINE]
                key = "cliff" if baseline["cliff"] is not None and cases[case["name"]]["cliff"] is not None else "far"
                deltas.append((cases[case["name"]][key]["fine_detail_loss"] - baseline[key]["fine_detail_loss"],
                               cases[case["name"]][key]["fine_alias"] - baseline[key]["fine_alias"]))
        if deltas:
            effects.append({"case": case["name"], "factor": case["factor"],
                            "detail_loss_delta": float(np.mean([d[0] for d in deltas])),
                            "alias_delta": float(np.mean([d[1] for d in deltas]))})
    report = {"mode": "blur_static", "verify": bool(verify), "inputs_current": inputs_all_current,
              "criteria": {"far_min_m": BLUR_FAR_MIN_M, "cliff_normal_y": BLUR_CLIFF_NORMAL_Y, "sigmas": list(BLUR_SIGMAS),
                           "fine_bands": list(BLUR_FINE_BANDS), "reference": BLUR_REFERENCE, "baseline": BLUR_BASELINE,
                           "reference_scale": BLUR_REFERENCE_SCALE},
              "groups": group_records, "views": views, "effects": effects, "failures": failures,
              "seconds": time.time() - started}
    apply_expect(report, expect, "blur_static", failures)
    report_path = os.path.join(capture_dir, "blur_static_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for effect in effects:
        print(f"REFERENCE_CHECK blur_static effect case={effect['case']} "
              f"detail_loss_delta={effect['detail_loss_delta']:+.3f} alias_delta={effect['alias_delta']:+.3f}")
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=blur_static views={len(BLUR_VIEWS)} cases={len(BLUR_CASES)} "
          f"inputs_current={int(inputs_all_current)} report={report_path}")
    return 0 if not failures else 1

def motion_png(group_dir, tag):
    """撮る名前の PNG（static-60 は --capture-views が撮った <視点>.png）"""
    name = f"{MOTION_VIEW}.png" if tag == MOTION_FINAL else f"{MOTION_VIEW}.{tag}.png"
    return os.path.join(group_dir, name)


def motion_phase_shift(a, b):
    """b を a に重ねるずれ（画素、(行, 列)）。中央の切り出しの輝度の位相相関の山を放物線で詰める"""
    crop_h, crop_w = MOTION_ALIGN_CROP
    h, w = a.shape
    top, left = (h - crop_h) // 2, (w - crop_w) // 2
    window = np.outer(np.hanning(crop_h), np.hanning(crop_w))
    pa = a[top:top + crop_h, left:left + crop_w]
    pb = b[top:top + crop_h, left:left + crop_w]
    fa = np.fft.fft2((pa - pa.mean()) * window)
    fb = np.fft.fft2((pb - pb.mean()) * window)
    cross = fb * np.conj(fa)
    cross /= np.maximum(np.abs(cross), 1e-12)
    corr = np.real(np.fft.ifft2(cross))
    py, px = np.unravel_index(int(np.argmax(corr)), corr.shape)

    def refine(c_minus, c_zero, c_plus):
        denom = c_minus - 2.0 * c_zero + c_plus
        return 0.0 if abs(denom) < 1e-12 else 0.5 * (c_minus - c_plus) / denom

    dy = py + refine(corr[(py - 1) % crop_h, px], corr[py, px], corr[(py + 1) % crop_h, px])
    dx = px + refine(corr[py, (px - 1) % crop_w], corr[py, px], corr[py, (px + 1) % crop_w])
    if dy > crop_h / 2:
        dy -= crop_h
    if dx > crop_w / 2:
        dx -= crop_w
    return float(dy), float(dx)


def motion_script_check(group_dir, size, require_resident):
    """台本の撮影のログ（段取りの順・エポックからのフレーム数・向き・撮った時点の常駐）と PNG を確かめる。(失敗の一覧, 記録)"""
    failures = []
    log_name = view_game_log(group_dir, MOTION_VIEW)
    path = os.path.join(group_dir, log_name) if log_name else ""
    if not log_name or not os.path.isfile(path):
        return [f"Game のログが無い: {log_name or '（metrics.json に game_log が無い）'}"], {}
    home = None
    loaded = done = False
    script_failures = []
    last_epoch = None
    last_pages = None
    capacity_window = []
    requests, saved, waits = {}, {}, []
    final_pages = None
    in_window = False
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            found = MOTION_START_YAW_RE.search(line)
            if found and home is None:
                home = (float(found.group(1)), float(found.group(2)))
            if "CAMERA_SCRIPT_LOADED " in line:
                loaded = True
            if "CAMERA_SCRIPT_DONE " in line:
                done = True
            if re.search(r"CAMERA_SCRIPT_(?:CAPTURE (?:failed|unavailable)|INVALID)", line):
                script_failures.append(line.strip()[-200:])
            found = MOTION_WAIT_RE.search(line)
            if found:
                waits.append({"state": found.group(1), "waited": int(found.group(2))})
            found = BLUR_PAGES_RE.search(line)
            if found:
                values = [int(v) for v in found.groups()]
                last_pages = {"resident": values[0], "uploading": values[1], "ready": values[2], "reading": values[3],
                              "wanted": values[4], "evicted": values[5]}
            found = BLUR_CAPACITY_RE.search(line)
            if found and in_window:
                capacity_window.append((int(found.group(1)), int(found.group(2))))
            found = MOTION_EPOCH_RE.search(line)
            if found:
                last_epoch = int(found.group(1))
            found = MOTION_REQUEST_RE.search(line)
            if found:
                in_window = True
                tag, frame = found.group(1), int(found.group(2))
                if tag in requests:
                    failures.append(f"{tag}: 撮影の要求が 2 回ある")
                requests[tag] = {"engine_frame": frame, "epoch_offset": None if last_epoch is None else frame - last_epoch,
                                 "position": found.group(3), "yaw": float(found.group(4)),
                                 "pitch": float(found.group(5))}
            found = MOTION_SAVED_RE.search(line)
            if found:
                saved[found.group(1)] = {"frame": int(found.group(2)), "requested_engine_frame": int(found.group(3)),
                                         "pages": dict(last_pages or {})}
            if re.search(rf"CAPTURE_VIEWS view={re.escape(MOTION_VIEW)} saved ", line):
                final_pages = dict(last_pages or {})
                in_window = False
    if not loaded:
        failures.append("台本を読み込んだ印（CAMERA_SCRIPT_LOADED）が無い")
    if not done:
        failures.append("台本を流し終えた印（CAMERA_SCRIPT_DONE）が無い")
    if script_failures:
        failures.append(f"台本の段取りの失敗の印がある: {script_failures}")
    if home is None:
        failures.append("基準の視点の向き（FREE_CAMERA_POSE phase=start）が無い")
    # エポックから撮るまでのフレーム数と、撮れたパケットの番号の対応（要求のフレームとの差が全部の撮影で同じ）
    frame_lags = set()
    for tag, offset in MOTION_TAG_OFFSETS.items():
        request = requests.get(tag)
        if request is None:
            failures.append(f"{tag}: 撮影の要求の行が無い")
            continue
        if request["epoch_offset"] != offset:
            failures.append(f"{tag}: エポックから {request['epoch_offset']} フレーム後に撮った（約束 {offset}）")
        if tag not in saved:
            failures.append(f"{tag}: 撮影の保存の行が無い")
        else:
            if saved[tag]["requested_engine_frame"] != request["engine_frame"]:
                failures.append(f"{tag}: 保存の行の要求のフレームが要求の行と違う")
            frame_lags.add(saved[tag]["frame"] - request["engine_frame"])
        png = motion_png(group_dir, tag)
        if not os.path.isfile(png):
            failures.append(f"{os.path.basename(png)} が無い")
        elif Image.open(png).size != size:
            failures.append(f"{os.path.basename(png)} の大きさ {Image.open(png).size} が {size} でない")
    if len(frame_lags) > 1:
        failures.append(f"撮れたパケットの番号と要求のフレームの差が撮影ごとに違う（{sorted(frame_lags)}。フレームが飛んだ）")
    # 向き: 振りと静止の同じフレーム数の撮影は同じ向き（同じ足し算の列）で、基準の向きに MOTION_YAW_TOLERANCE 以内。
    # motion-30-pre は motion-30 より MOTION_PRE_FRAMES フレーム分だけ手前の向き
    if home is not None:
        for k in MOTION_OFFSETS:
            motion, still = requests.get(f"motion-{k}"), requests.get(f"static-{k}")
            if motion is None or still is None:
                continue
            if motion["yaw"] != still["yaw"] or motion["pitch"] != still["pitch"]:
                failures.append(f"motion-{k} と static-{k} の向きが違う（{motion['yaw']} / {still['yaw']}）")
            if abs(motion["yaw"] - home[0]) > MOTION_YAW_TOLERANCE or abs(motion["pitch"] - home[1]) > MOTION_YAW_TOLERANCE:
                failures.append(f"motion-{k} の向き {motion['yaw']} が基準の視点 {home[0]} から外れている")
        pre, last = requests.get(MOTION_PRE_TAG), requests.get("motion-30")
        if pre is not None and last is not None:
            expected = -MOTION_PRE_FRAMES * MOTION_YAW_PER_FRAME
            if abs((pre["yaw"] - last["yaw"]) - expected) > MOTION_PRE_YAW_TOLERANCE:
                failures.append(f"{MOTION_PRE_TAG} と motion-30 の向きの差 {pre['yaw'] - last['yaw']:.6f}° が "
                                f"{expected:.3f}° でない（カメラが約束の速さで動いていない）")
    # 撮った時点の常駐: 進行中のページが 0 で、最初の撮影から --capture-views の撮影まで追い出しが無い。描画の溢れが無い
    evicted_values = set()
    for tag, record in saved.items():
        pages = record["pages"]
        in_flight = {k: pages.get(k) for k in ("wanted", "reading", "ready", "uploading")}
        record["resident_complete"] = bool(pages) and not any(in_flight.values())
        evicted_values.add(pages.get("evicted"))
        if require_resident and not record["resident_complete"]:
            failures.append(f"{tag}: 撮った時点でジオメトリのページが常駐しきっていない（{in_flight}）")
    if final_pages is not None:
        evicted_values.add(final_pages.get("evicted"))
    if require_resident and len(evicted_values) != 1:
        failures.append(f"最初の撮影から --capture-views の撮影までに追い出しがある（追い出しの数 {sorted(evicted_values, key=str)}）")
    if require_resident and any(w["state"] != "ready" for w in waits):
        failures.append(f"台本の wait-ready が落ち着かずに進んだ（{waits}）")
    if not capacity_window:
        failures.append("最初の撮影から --capture-views の撮影までに MEGA_CAPACITY の行が無い（描画の溢れを確かめられない）")
    elif any(any(values) for values in capacity_window):
        failures.append(f"撮影の間に描かれないクラスタがある（{[v for v in capacity_window if any(v)][:4]}）")
    return failures, {"home_yaw_pitch": home, "requests": requests, "saved": saved, "wait_ready": waits,
                      "frame_lags": sorted(frame_lags), "evicted_values": sorted(evicted_values, key=str),
                      "final_pages": final_pages, "capacity_lines": len(capacity_window)}


def run_blur_motion(capture_dir, verify, engine_dirs=None, reference_dir=None, expect=None):
    """遠景のぼけの動きの比較。撮影先・正解の場所・入力の照らし方は run_blur_static と同じ（カメラ台本もエンジンの実装に入る）"""
    import RefSplit
    started = time.time()
    failures = []
    group_records = {}
    input_hashes = set()
    exe_hashes = set()
    inputs_all_current = True
    engine_dirs = engine_dirs or [capture_dir]
    reference_dir = reference_dir or RefSplit.resolve_group_dir(engine_dirs, MOTION_REFERENCE)
    for group in MOTION_GROUPS:
        name = group["name"]
        group_dir = reference_dir if name == MOTION_REFERENCE else RefSplit.resolve_group_dir(engine_dirs, group["dir"])
        if not os.path.isdir(group_dir) or not os.path.isfile(os.path.join(group_dir, "metrics.json")):
            failures.append(f"{name}: 撮影の組が無い（{group_dir}）")
            inputs_all_current = False
            continue
        inputs_ok, input_failures, record = split_engine_check(group_dir)
        inputs_all_current = inputs_all_current and inputs_ok
        if verify and not inputs_ok:
            failures.extend(f"{name}: {failure}" for failure in input_failures)
        if record is not None:
            input_hashes.add(record.get("identity"))
            if record.get("game_exe_sha256"):
                exe_hashes.add(record["game_exe_sha256"])
            else:
                failures.append(f"{name}: inputs.json に撮影に使った Game.exe のハッシュが無い（--record-inputs --game-exe）")
        group_failures, info = blur_case_check(group_dir, tuple(group["args"]), tuple(group["logs"]), group["size"],
                                               group["resident"], views=(MOTION_VIEW,))
        args = plate_capture_args(group_dir) or []
        if MOTION_SCRIPT_ARG not in args:
            group_failures.append(f"カメラの台本の起動引数（{MOTION_SCRIPT_ARG}）が無い")
        script_failures, script_info = motion_script_check(group_dir, group["size"], group["resident"])
        failures.extend(f"{name}: {failure}" for failure in group_failures + script_failures)
        group_records[name] = {"inputs_current": inputs_ok, "dir": rel_dir(group_dir),
                               "game_exe_sha256": (record or {}).get("game_exe_sha256"), **info, "script": script_info}
    if len(input_hashes) > 1:
        failures.append(f"組ごとのエンジンの実装が揃っていない（{len(input_hashes)} 通り。同じ実装で撮った組だけを比べる）")
    if len(exe_hashes) > 1:
        failures.append(f"組ごとの Game.exe のハッシュが揃っていない（{len(exe_hashes)} 通り。同じビルドで撮った組だけを比べる）")

    tags = [f"static-{k}" for k in MOTION_OFFSETS] + [MOTION_FINAL] + [f"motion-{k}" for k in MOTION_OFFSETS]
    entry = {"view": MOTION_VIEW}
    results = {}
    alignment = {}
    effects = []
    needed = [os.path.join(reference_dir, f"{MOTION_VIEW}.{suffix}") for suffix in ("png", "aov.depth.pfm", "aov.normal.pfm")]
    if not all(os.path.isfile(p) for p in needed) or any(f.startswith(MOTION_REFERENCE + ":") for f in failures):
        failures.append(f"{MOTION_VIEW}: 正解の撮影（PNG・深度・法線・台本の撮影）が揃っていないので比べない")
    else:
        far_masks, cliff_masks, region = blur_regions(reference_dir, MOTION_VIEW)
        entry["region"] = region
        if min(region["far_px_per_band"]) < BLUR_MIN_REGION_PX:
            failures.append(f"{MOTION_VIEW}: 遠景の範囲の画素が {min(region['far_px_per_band'])} で {BLUR_MIN_REGION_PX} に足りない")
        use_cliff = min(region["cliff_px_per_band"]) > 0
        reference_displays = {tag: blur_load_display(motion_png(reference_dir, tag), BLUR_REFERENCE_SCALE) for tag in tags}
        reference_bands = {tag: blur_bands(display @ LUMA) for tag, display in reference_displays.items()}
        # 測り方の較正（SCN-WLR-FAR-BLUR-STATIC と同じ。止めて 60 フレーム後の正解で）
        final_luma = reference_displays[MOTION_FINAL] @ LUMA
        blurred_bands = blur_bands(blur_gaussian(final_luma, BLUR_CALIB_BLUR_SIGMA))
        noise_rms = float(np.sqrt(np.mean(reference_bands[MOTION_FINAL][0][far_masks[0]] ** 2)))
        rng = np.random.default_rng(BLUR_CALIB_SEED)
        noisy_bands = blur_bands(final_luma + rng.normal(0.0, noise_rms, final_luma.shape))
        blur_row = blur_compare(reference_bands[MOTION_FINAL], blurred_bands, far_masks)
        noise_row = blur_compare(reference_bands[MOTION_FINAL], noisy_bands, far_masks)
        entry["calibration"] = {"blur": blur_row, "noise": noise_row, "noise_rms": noise_rms}
        if blur_row is None or noise_row is None:
            failures.append(f"{MOTION_VIEW}: 較正を求められない（正解の帯域が空）")
        else:
            if blur_row["bands"][0]["detail_loss"] < BLUR_CALIB_BLUR_MIN_LOSS or \
                    blur_row["fine_alias"] > BLUR_CALIB_BLUR_MAX_ALIAS:
                failures.append(f"{MOTION_VIEW}: 較正（ぼかし）が細部の欠けに数えられない")
            if abs(noise_row["bands"][0]["detail_kept"] - 1.0) > BLUR_CALIB_NOISE_GAIN or \
                    noise_row["bands"][0]["alias"] < BLUR_CALIB_NOISE_MIN_ALIAS:
                failures.append(f"{MOTION_VIEW}: 較正（雑音）がエイリアスに数えられない")
        displays = {f"ref2x {MOTION_FINAL}": reference_displays[MOTION_FINAL]}
        # 撮った画のずれ（振りと静止の同じ名前どうし・motion-30-pre → motion-30）。正解は 2×2 を縮めた画で比べる
        half_fov = np.radians(MOTION_VERTICAL_FOV_DEG) / 2.0
        focal_px = (BLUR_SIZE[1] / 2.0) / np.tan(half_fov)
        home_pitch = ((group_records.get("fxaa") or {}).get("script") or {}).get("home_yaw_pitch") or (0.0, 0.0)
        expected_shift = focal_px * np.tan(np.radians(MOTION_PRE_FRAMES * MOTION_YAW_PER_FRAME)) * \
            np.cos(np.radians(home_pitch[1]))
        for group in MOTION_GROUPS:
            name = group["name"]
            group_dir = os.path.normpath(os.path.join(capture_dir, group["dir"]))
            scale = BLUR_REFERENCE_SCALE if name == MOTION_REFERENCE else 1
            lumas = {}
            for tag in tags + [MOTION_PRE_TAG]:
                png = motion_png(group_dir, tag)
                if os.path.isfile(png):
                    lumas[tag] = blur_load_display(png, scale) @ LUMA
            shifts = {}
            for k in MOTION_OFFSETS:
                a, b = lumas.get(f"static-{k}"), lumas.get(f"motion-{k}")
                if a is None or b is None:
                    continue
                dy, dx = motion_phase_shift(a, b)
                shifts[f"motion-{k}"] = [dy, dx]
                if np.hypot(dy, dx) >= MOTION_ALIGN_MAX_PX:
                    failures.append(f"{name}: motion-{k} が static-{k} から ({dy:.2f}, {dx:.2f}) 画素ずれている"
                                    f"（同じ向きで撮れていない）")
            if MOTION_PRE_TAG in lumas and "motion-30" in lumas:
                dy, dx = motion_phase_shift(lumas[MOTION_PRE_TAG], lumas["motion-30"])
                shifts[MOTION_PRE_TAG] = [dy, dx]
                if abs(abs(dx) - expected_shift) > MOTION_SHIFT_TOLERANCE_PX or abs(dy) > MOTION_SHIFT_TOLERANCE_PX:
                    failures.append(f"{name}: {MOTION_PRE_TAG} → motion-30 の画のずれ ({dy:.2f}, {dx:.2f}) が、"
                                    f"振りの角から求めた横 {expected_shift:.2f} 画素と合わない")
            alignment[name] = shifts
            if name not in MOTION_AA_GROUPS:
                continue
            group_results = {}
            for tag in tags:
                png = motion_png(group_dir, tag)
                if not os.path.isfile(png) or Image.open(png).size != BLUR_SIZE:
                    continue
                display = blur_load_display(png, 1)
                if tag in ("static-30", "motion-30", MOTION_FINAL, "motion-8", "motion-0"):
                    displays[f"{name} {tag}"] = display
                bands = blur_bands(display @ LUMA)
                far_result = blur_compare(reference_bands[tag], bands, far_masks)
                cliff_result = blur_compare(reference_bands[tag], bands, cliff_masks) if use_cliff else None
                if far_result is None:
                    failures.append(f"{name}: {tag} を比べられない")
                    continue
                values = [far_result["fine_detail_loss"], far_result["fine_alias"]]
                if cliff_result is not None:
                    values += [cliff_result["fine_detail_loss"], cliff_result["fine_alias"]]
                if not all(np.isfinite(values)):
                    failures.append(f"{name}: {tag} の値に NaN・Inf がある")
                group_results[tag] = {"far": far_result, "cliff": cliff_result}
                print(f"REFERENCE_CHECK blur_motion group={name} tag={tag} "
                      f"far_loss={far_result['fine_detail_loss']:.3f} far_alias={far_result['fine_alias']:.3f} "
                      + (f"cliff_loss={cliff_result['fine_detail_loss']:.3f} "
                         f"cliff_alias={cliff_result['fine_alias']:.3f} " if cliff_result else "")
                      + "kept_by_band=" + ",".join(f"{row['detail_kept']:.2f}" for row in far_result["bands"]))
            missing = [tag for tag in tags if tag not in group_results]
            if missing:
                failures.append(f"{name}: 比べられなかった撮影がある: {missing}")
            results[name] = group_results
            # 静止との差（同じエポックからのフレーム数どうし。motion-30 は止めて貯まりきった static-60 とも比べる）
            for key in ("far", "cliff"):
                pairs = [(f"motion-{k}", f"static-{k}") for k in MOTION_OFFSETS] + [("motion-30", MOTION_FINAL)]
                for motion_tag, static_tag in pairs:
                    m, s = group_results.get(motion_tag), group_results.get(static_tag)
                    if m is None or s is None or m[key] is None or s[key] is None:
                        continue
                    effects.append({"group": name, "region": key, "motion": motion_tag, "static": static_tag,
                                    "detail_loss_delta": m[key]["fine_detail_loss"] - s[key]["fine_detail_loss"],
                                    "alias_delta": m[key]["fine_alias"] - s[key]["fine_alias"]})
        cy, cx = np.argwhere(cliff_masks[0] if use_cliff else far_masks[0]).mean(axis=0)
        crops_path = os.path.join(capture_dir, f"blur_motion_{MOTION_VIEW}.png")
        order = [f"ref2x {MOTION_FINAL}", f"fxaa {MOTION_FINAL}", "fxaa motion-30", f"taa {MOTION_FINAL}",
                 "taa static-30", "taa motion-30", "taa motion-0", "taa motion-8", "base motion-30"]
        entry["crops"] = blur_write_crops(crops_path, [(n, displays[n]) for n in order if n in displays], (cy, cx))
        entry["crops_png"] = crops_path
        if blur_row is not None and noise_row is not None:
            print(f"REFERENCE_CHECK blur_motion calibration blur_loss0={blur_row['bands'][0]['detail_loss']:.3f} "
                  f"blur_alias={blur_row['fine_alias']:.3f} noise_kept0={noise_row['bands'][0]['detail_kept']:.3f} "
                  f"noise_alias0={noise_row['bands'][0]['alias']:.3f} far_px={region['far_px']} cliff_px={region['cliff_px']}")
        entry["expected_pre_shift_px"] = float(expected_shift)
    # 履歴による細部の消失: TAA の（振り − 静止）から、履歴を持たない FXAA の（振り − 静止）を引いた差
    history = []
    for key in ("far", "cliff"):
        for k in MOTION_OFFSETS:
            def find(group):
                for effect in effects:
                    if effect["group"] == group and effect["region"] == key and effect["motion"] == f"motion-{k}" \
                            and effect["static"] == f"static-{k}":
                        return effect
                return None
            taa, fxaa = find("taa"), find("fxaa")
            if taa and fxaa:
                history.append({"region": key, "offset": k,
                                "detail_loss": taa["detail_loss_delta"] - fxaa["detail_loss_delta"],
                                "alias": taa["alias_delta"] - fxaa["alias_delta"]})
    report = {"mode": "blur_motion", "verify": bool(verify), "inputs_current": inputs_all_current,
              "criteria": {"view": MOTION_VIEW, "script": MOTION_SCRIPT_ARG, "tag_offsets": MOTION_TAG_OFFSETS,
                           "yaw_per_frame": MOTION_YAW_PER_FRAME, "far_min_m": BLUR_FAR_MIN_M,
                           "cliff_normal_y": BLUR_CLIFF_NORMAL_Y, "sigmas": list(BLUR_SIGMAS),
                           "fine_bands": list(BLUR_FINE_BANDS), "align_max_px": MOTION_ALIGN_MAX_PX,
                           "shift_tolerance_px": MOTION_SHIFT_TOLERANCE_PX},
              "groups": group_records, "view": entry, "results": results, "alignment": alignment,
              "effects": effects, "history_effect": history, "failures": failures, "seconds": time.time() - started}
    apply_expect(report, expect, "blur_motion", failures)
    report_path = os.path.join(capture_dir, "blur_motion_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for name, shifts in alignment.items():
        print(f"REFERENCE_CHECK blur_motion align group={name} "
              + " ".join(f"{tag}=({v[0]:+.2f},{v[1]:+.2f})" for tag, v in shifts.items()))
    for effect in effects:
        print(f"REFERENCE_CHECK blur_motion effect group={effect['group']} region={effect['region']} "
              f"{effect['motion']}-{effect['static']} detail_loss_delta={effect['detail_loss_delta']:+.3f} "
              f"alias_delta={effect['alias_delta']:+.3f}")
    for row in history:
        print(f"REFERENCE_CHECK blur_motion history region={row['region']} offset={row['offset']} "
              f"detail_loss={row['detail_loss']:+.3f} alias={row['alias']:+.3f}")
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=blur_motion view={MOTION_VIEW} groups={len(MOTION_GROUPS)} "
          f"inputs_current={int(inputs_all_current)} report={report_path}")
    return 0 if not failures else 1


# ---- --gray（灰色の地形のエンジンと Cycles の比較）----

def gray_capture_args(case_dir):
    return plate_capture_args(case_dir)


def gray_camera_match(engine_camera, cycles_camera):
    """エンジンの aov.json のカメラと、Cycles が置いたカメラの記録の食い違いの理由の一覧"""
    reasons = []
    position = np.asarray(engine_camera["position"], dtype=np.float64) - np.asarray(cycles_camera["position"],
                                                                                     dtype=np.float64)
    if np.linalg.norm(position) > PROJECTION_CAMERA_POSITION_M:
        reasons.append(f"カメラの位置が {np.linalg.norm(position):.4g} m 違う")
    for key in ("forward", "up", "right"):
        delta = np.asarray(engine_camera[key], dtype=np.float64) - np.asarray(cycles_camera[key], dtype=np.float64)
        if np.linalg.norm(delta) > PROJECTION_CAMERA_DIRECTION:
            reasons.append(f"カメラの {key} が {np.linalg.norm(delta):.3g} 違う")
    if abs(float(engine_camera["vertical_fov_degrees"]) - float(cycles_camera["vertical_fov_degrees"])) > \
            PROJECTION_CAMERA_FOV_DEG:
        reasons.append("縦の画角が違う")
    return reasons


def gray_engine_capture(case_dir, case, views, verify, engine_check=None):
    """エンジンの撮影の組（共通の照明・地形だけ・最細の LOD・灰色の材質）を検査する。(結果, 視点 → HDR, 失敗の一覧)
    engine_check を与えると入力の照らし方をそれに替える（RefSplit.check_engine。記録の identity を files_sha256 に入れる）"""
    entry = {"dir": os.path.relpath(case_dir, REPO_ROOT).replace("\\", "/"), "case": case}
    failures = []
    hdrs = {}
    if not os.path.isdir(case_dir):
        return entry, hdrs, [f"エンジンの撮影の組が無い（{case_dir}）"]
    inputs_ok, input_failures, record = (engine_check or check_inputs)(case_dir)
    entry["inputs_current"] = inputs_ok
    entry["files_sha256"] = (record or {}).get("identity" if engine_check else "files_sha256")
    entry["engine_impl"] = record if engine_check else None
    entry["game_exe_sha256"] = (record or {}).get("game_exe_sha256")
    if verify and not inputs_ok:
        failures.extend(input_failures)
    game_args = gray_capture_args(case_dir)
    entry["game_args"] = game_args
    if game_args is None:
        return entry, hdrs, failures + ["metrics.json が無い"]
    wanted = list(GRAY_GAME_ARGS) + ([f"--common-light-case={case}"] if case != "both" else [])
    missing = [a for a in wanted if a not in game_args]
    if missing:
        failures.append(f"撮影の起動引数に {', '.join(missing)} が無い")
    if case == "both" and any(a.startswith("--common-light-case=") for a in game_args):
        failures.append("太陽と空の両方の撮影に --common-light-case がある")
    hide = aov_hide_names(game_args)
    if set(AOV_HIDE_NAMES) - hide:
        failures.append("撮影が --wasteland-hide=all でない")
    entry["views"] = {}
    for view in views:
        view_entry = {}
        entry["views"][view] = view_entry
        pfm = os.path.join(case_dir, f"{view}.hdr.pfm")
        meta_path = os.path.join(case_dir, f"{view}.hdr.json")
        if not os.path.isfile(pfm) or not os.path.isfile(meta_path):
            failures.append(f"{view}: HDR が無い")
            continue
        with open(meta_path, encoding="utf-8") as f:
            meta = json.load(f)
        if meta.get("units") != "cd/m2" or meta.get("source") != "scene_color_before_bloom":
            failures.append(f"{view}: hdr.json の単位・読み出し元が約束と違う")
        hdr = DT.read_pfm(pfm).astype(np.float64)
        if not np.isfinite(hdr).all() or (hdr < 0.0).any():
            failures.append(f"{view}: HDR に負・NaN・Inf がある")
        hdrs[view] = hdr
        log_name = view_game_log(case_dir, view)
        log_path = os.path.join(case_dir, log_name) if log_name else None
        if not log_path or not os.path.isfile(log_path):
            failures.append(f"{view}: Game のログが無い")
            continue
        with open(log_path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        for line in GRAY_LOG_LINES + (f"WASTELAND_COMMON_LIGHT view={view} case={case} ",):
            if line not in text:
                failures.append(f"{view}: Game のログに {line.strip()} が無い")
        found = re.search(r"WASTELAND_GRAY_MATERIAL albedo=([0-9.]+) roughness=([0-9.]+)", text)
        view_entry["gray_material_log"] = found.group(0) if found else None
        single_view = view if log_name == f"{view}.Game.log" else None
        pages = geometry_pages_at_capture(text.splitlines(), single_view).get(view)
        view_entry["geometry_pages_at_capture"] = pages
        if pages is None:
            failures.append(f"{view}: Game のログに撮影の保存の行か、その前の GEOMETRY_PAGES の行が無い")
        else:
            in_flight = {k: pages.get(k) for k in ("wanted", "reading", "ready", "uploading")}
            if any(in_flight.values()) or len(pages["settle_evicted"]) != 1:
                failures.append(f"{view}: 撮った時点でジオメトリのページが常駐しきっていない（進行中 {in_flight}、"
                                f"追い出しの数 {pages['settle_evicted']}）")
        dropped = [int(v) for v in re.findall(r"MEGA_CAPACITY draw_dropped=(\d+)", text)]
        view_entry["mega_draw_dropped_max"] = max(dropped) if dropped else None
        if dropped and max(dropped) > 0:
            failures.append(f"{view}: 描くクラスタが溢れた（draw_dropped 最大 {max(dropped)}）")
    return entry, hdrs, [f"{case}: {f}" for f in failures]


def gray_check_aov(capture_dir, view, light):
    """灰色の材質が GBuffer に掛かったこと（AOV のアルベド・粗さ）。(記録, 失敗の一覧)"""
    failures = []
    depth = read_pfm_any(os.path.join(capture_dir, f"{view}.aov.depth.pfm"))[..., 0]
    albedo = read_pfm_any(os.path.join(capture_dir, f"{view}.aov.albedo.pfm"))
    roughness = read_pfm_any(os.path.join(capture_dir, f"{view}.aov.roughness.pfm"))[..., 0]
    surface = np.isfinite(depth)
    target = float(light["gray_material"]["albedo"])
    albedo_ok = np.all(np.abs(albedo[surface] - target) <= GRAY_ALBEDO_TOLERANCE * target, axis=-1)
    rough_ok = np.abs(roughness[surface] - float(light["gray_material"]["roughness"])) <= GRAY_ROUGHNESS_TOLERANCE
    record = {
        "surface_pixels": int(surface.sum()),
        "albedo_median": float(np.median(albedo[surface][:, 1])),
        "albedo_within_ratio": float(albedo_ok.mean()) if surface.any() else 0.0,
        "roughness_within_ratio": float(rough_ok.mean()) if surface.any() else 0.0,
    }
    if record["albedo_within_ratio"] < GRAY_ALBEDO_RATIO:
        failures.append(f"{view}: GBuffer のアルベドが灰色 {target} の ±{GRAY_ALBEDO_TOLERANCE:.0%} に入る画素が "
                        f"{record['albedo_within_ratio']:.4f}（{GRAY_ALBEDO_RATIO} 未満。材質が掛かっていない）")
    if record["roughness_within_ratio"] < GRAY_ALBEDO_RATIO:
        failures.append(f"{view}: GBuffer の粗さが {light['gray_material']['roughness']} の画素が "
                        f"{record['roughness_within_ratio']:.4f}")
    return record, depth, failures


def check_cycles_gray_inputs(cycles_dir, engine_files_sha256):
    """Cycles の結果の入力の記録を今のファイルと照らす。(合格か, 理由の一覧, 記録)"""
    ok, reasons, record = check_blender_inputs(cycles_dir)
    if record is None:
        return False, [f"{GRAY_CYCLES_DIR}/inputs.json が無い（BlenderReference.py --gray を回していない）"], None
    for key, rel in (("gray_regions_sha256", GRAY_REGIONS_PATH), ("common_light_sha256", COMMON_LIGHT_PATH)):
        if sha256_file(os.path.join(REPO_ROOT, rel)) != record.get(key):
            reasons.append(f"{rel} が描いた後に変わった（古い結果）")
    if record.get("engine_capture_files_sha256") != engine_files_sha256:
        reasons.append("カメラと深度を借りたエンジンの撮影の入力が、今の撮影と違う")
    if record.get("kind") != "cycles_gray":
        reasons.append(f"記録の種類が cycles_gray でない（{record.get('kind')}）")
    return not reasons, reasons, record


def gray_check_plate(cycles_dir, case, light):
    result = {"case": case, "pass": False, "failures": []}
    pfm = os.path.join(cycles_dir, f"plate_{case}.pfm")
    meta_path = os.path.join(cycles_dir, f"plate_{case}.json")
    if not os.path.isfile(pfm) or not os.path.isfile(meta_path):
        result["failures"].append(f"plate_{case} が無い")
        return result
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    if sha256_file(pfm) != meta.get("pfm_sha256"):
        result["failures"].append(f"plate_{case}.pfm が記録のハッシュと違う")
    image = read_pfm_any(pfm).astype(np.float64)
    height, width = image.shape[:2]
    half_w = max(1, int(round(width * PLATE_WINDOW / 2.0)))
    half_h = max(1, int(round(height * PLATE_WINDOW / 2.0)))
    window = image[height // 2 - half_h:height // 2 + half_h, width // 2 - half_w:width // 2 + half_w]
    luminance = window @ LUMA
    mean_rgb = window.reshape(-1, 3).mean(axis=0)
    mean_y = float(luminance.mean())
    expected_rgb, inputs = plate_expected_rgb(light, case, GRAY_PLATE_VIEW)
    expected_y = float(expected_rgb @ LUMA)
    ratio = mean_y / expected_y
    cv = float(luminance.std() / mean_y) if mean_y > 0.0 else float("inf")
    result.update({"samples": meta.get("samples"), "device": meta.get("device"), "inputs": inputs,
                   "expected_luminance_cd_m2": expected_y, "measured_luminance_cd_m2": mean_y, "ratio": ratio,
                   "ratio_rgb": (mean_rgb / expected_rgb).tolist(), "luminance_cv": cv})
    if not abs(ratio - 1.0) <= PLATE_TOLERANCE:
        result["failures"].append(f"Cycles の板 {case} の輝度 {mean_y:.3f} が期待 {expected_y:.3f} と "
                                  f"{100.0 * (ratio - 1.0):+.2f} % 違う（±{100.0 * PLATE_TOLERANCE:.0f} %）")
    if not cv <= PLATE_UNIFORMITY:
        result["failures"].append(f"Cycles の板 {case} の窓の変動係数 {cv:.4f} が {PLATE_UNIFORMITY} を超える")
    result["pass"] = not result["failures"]
    return result


def gray_display(image, scale):
    """線形の値に scale を掛け、sRGB の 8 bit へ（見比べの画像だけに使う）"""
    x = np.clip(image * scale, 0.0, 1.0)
    srgb = np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1.0 / 2.4) - 0.055)
    return (srgb * 255.0 + 0.5).astype(np.uint8)


def write_gray_preview(path, engine, cycles, masks, valid, scale):
    """左上: エンジン、右上: Cycles（同じ露出）、左下: log2(エンジン ÷ Cycles)（青が暗い・赤が明るい、±1 段で飽和）、右下: 領域"""
    def half(image):
        return np.asarray(Image.fromarray(image).resize((image.shape[1] // 2, image.shape[0] // 2), Image.BOX))
    ratio = np.log2(np.maximum(engine @ LUMA, 1e-6) / np.maximum(cycles @ LUMA, 1e-6))
    t = np.clip(ratio, -1.0, 1.0)
    heat = np.zeros(engine.shape, dtype=np.float64)
    heat[..., 0] = np.where(t > 0, 1.0, 1.0 + t)
    heat[..., 1] = 1.0 - np.abs(t)
    heat[..., 2] = np.where(t < 0, 1.0, 1.0 - t)
    heat = np.where(valid[..., None], heat, 0.15)
    colors = {"sunlit_ground": (230, 200, 60), "shadow_ground": (60, 90, 200), "sunlit_cliff": (230, 110, 40),
              "shadow_cliff": (120, 50, 160), "far": (80, 170, 90)}
    regions = np.full(engine.shape, 30, dtype=np.uint8)
    for name, color in colors.items():
        regions[masks[name]] = color
    top = np.concatenate([half(gray_display(engine, scale)), half(gray_display(cycles, scale))], axis=1)
    bottom = np.concatenate([half((heat * 255.0).astype(np.uint8)), half(regions)], axis=1)
    Image.fromarray(np.concatenate([top, bottom], axis=0)).save(path)


def gray_region_means(image, mask):
    if not mask.any():
        return None
    return image[mask].reshape(-1, 3).mean(axis=0)


def check_gray_view(capture_dir, cycles_dir, view, engine_hdr, engine_depth, diag_hdrs, light, preview_prefix="gray",
                    out=None, engine_dir=None):
    """視点ごとの Cycles とエンジンの比較（--gray・--lit）。out に辞書を渡すと、領域の画素（masks）・Cycles の合成（cycles）・
    被覆（alpha）を入れて返す"""
    failures = []
    result = {"view": view}
    meta_path = os.path.join(cycles_dir, f"{view}.cycles.json")
    if not os.path.isfile(meta_path):
        return dict(result, failures=[f"{GRAY_CYCLES_DIR}/{view}.cycles.json が無い"], **{"pass": False})
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    result["cycles_material"] = meta.get("material")
    result["cycles_camera_shift_px"] = meta.get("camera_shift_px")
    for name, digest in (meta.get("outputs_sha256") or {}).items():
        path = os.path.join(cycles_dir, name)
        if not os.path.isfile(path) or sha256_file(path) != digest:
            failures.append(f"{GRAY_CYCLES_DIR}/{name} が無いか、記録のハッシュと違う")
    if failures:
        return dict(result, failures=failures, **{"pass": False})
    prefix = os.path.join(cycles_dir, view)
    cycles = read_pfm_any(prefix + ".cycles.hdr.pfm").astype(np.float64)
    seed_a = read_pfm_any(prefix + ".cycles.seed_a.pfm").astype(np.float64)
    seed_b = read_pfm_any(prefix + ".cycles.seed_b.pfm").astype(np.float64)
    parts = {g: read_pfm_any(prefix + f".cycles.{g}.pfm").astype(np.float64) for g in ("sun", "sky", "direct",
                                                                                        "indirect")}
    visibility = read_pfm_any(prefix + ".aux.visibility.pfm")[..., 0]
    n_dot_l = read_pfm_any(prefix + ".aux.n_dot_l.pfm")[..., 0]
    normal = read_pfm_any(prefix + ".aux.normal.pfm")
    alpha = read_pfm_any(prefix + ".aux.alpha.pfm")[..., 0]
    if cycles.shape != engine_hdr.shape:
        failures.append(f"Cycles の大きさ {cycles.shape} がエンジン {engine_hdr.shape} と違う")
        return dict(result, failures=failures, **{"pass": False})
    for name, image in (("合成", cycles), ("A", seed_a), ("B", seed_b)):
        if not np.isfinite(image).all() or (image < 0.0).any():
            failures.append(f"Cycles の{name}に負・NaN・Inf がある")

    with open(os.path.join(engine_dir or capture_dir, f"{view}.aov.json"), encoding="utf-8-sig") as f:
        engine_camera = json.load(f)["camera"]
    failures.extend(gray_camera_match(engine_camera, meta["camera_engine"]))

    # 空の分かれ方（投影がずれていないこと）
    engine_sky = ~np.isfinite(engine_depth)
    cycles_sky = alpha < 0.5
    sky_mismatch = float(np.mean(engine_sky != cycles_sky))
    result["sky_mismatch"] = sky_mismatch
    if sky_mismatch > GRAY_SKY_MISMATCH:
        failures.append(f"空と表面の分かれ方の食い違い {sky_mismatch:.4f} が {GRAY_SKY_MISMATCH} を超える")

    # Cycles の光の分け方の整合（直接 + 間接、太陽の組 + 空の組 が合成に合う）
    surface = (alpha > 0.5) & np.isfinite(engine_depth)
    combined_sum = float((cycles[surface] @ LUMA).sum())
    consistency = {
        "direct_plus_indirect": float(((parts["direct"] + parts["indirect"])[surface] @ LUMA).sum() / combined_sum),
        "sun_plus_sky": float(((parts["sun"] + parts["sky"])[surface] @ LUMA).sum() / combined_sum),
    }
    result["cycles_split_consistency"] = consistency
    for key, value in consistency.items():
        if abs(value - 1.0) > GRAY_SPLIT_TOLERANCE:
            failures.append(f"Cycles の {key} の和が合成の {value:.4f} 倍（±{GRAY_SPLIT_TOLERANCE}）")

    masks = GR.region_masks(engine_depth, normal[..., 1], n_dot_l, visibility, alpha > 0.5)
    stats, all_converged = GR.convergence(seed_a @ LUMA, seed_b @ LUMA, masks)
    result.update({"cycles": {k: meta.get(k) for k in ("renderer", "device", "tiles", "triangles", "spp_per_seed",
                                                       "stop_reason", "load_seconds", "render_seconds")},
                   "cycles_settings": meta.get("settings"), "all_converged": all_converged})
    engine_y = engine_hdr @ LUMA
    cycles_y = cycles @ LUMA
    regions = {}
    for name in GR.REGIONS:
        mask = masks[name]
        entry = {"label": GR.REGION_LABELS[name], "pixels": int(mask.sum()), "convergence": stats[name]}
        if stats[name]["converged"]:
            entry["status"] = "判定"
        elif stats[name]["pixels"] < GR.MIN_REGION_PX:
            entry["status"] = "未判定（画素が足りない）"
        else:
            entry["status"] = "未判定（収束しない）"
        if mask.any():
            e_rgb = gray_region_means(engine_hdr, mask)
            c_rgb = gray_region_means(cycles, mask)
            e_y = float(engine_y[mask].mean())
            c_y = float(cycles_y[mask].mean())
            entry.update({
                "engine_y": e_y, "cycles_y": c_y, "ratio": e_y / c_y if c_y > 0 else None,
                "engine_rgb": e_rgb.tolist(), "cycles_rgb": c_rgb.tolist(),
                "ratio_rgb": (e_rgb / np.maximum(c_rgb, 1e-30)).tolist(),
                "pixel_ratio_p10_p50_p90": np.percentile(engine_y[mask] / np.maximum(cycles_y[mask], 1e-6),
                                                         [10, 50, 90]).tolist(),
                "visibility_mean": float(visibility[mask].mean()),
                "cycles_share": {g: float((parts[g][mask] @ LUMA).mean() / c_y) for g in parts},
            })
            for case, hdr in diag_hdrs.items():
                if view in hdr:
                    e_case = float((hdr[view][mask] @ LUMA).mean())
                    c_case = float((parts[case][mask] @ LUMA).mean())
                    entry[f"engine_{case}_y"] = e_case
                    entry[f"cycles_{case}_y"] = c_case
                    entry[f"{case}_ratio"] = e_case / c_case if c_case > 0 else None
        regions[name] = entry
    result["regions"] = regions
    valid = np.zeros(engine_depth.shape, dtype=bool)
    for name in GR.REGIONS:
        valid |= masks[name]
    reference = cycles_y[valid]
    scale = 0.5 / float(np.percentile(reference, 90)) if reference.size else 1.0
    write_gray_preview(os.path.join(capture_dir, f"{preview_prefix}_{view}.png"), engine_hdr, cycles, masks, valid, scale)
    if out is not None:
        out.update({"masks": masks, "cycles": cycles, "alpha": alpha, "parts": parts})
    result.update({"pass": not failures, "failures": failures})
    return result


def apply_expect(report, expect, mode, failures):
    """--expect の仕様の各行を報告の値で判定し、report["expect"] に入れて、外れた行と許さない未判定を failures に足す（ExpectSpec.py）"""
    if expect:
        import ExpectSpec
        ExpectSpec.apply(sys.modules[__name__], report, expect, mode, failures)


def split_engine_check(capture_dir):
    import RefSplit
    return RefSplit.check_engine(sys.modules[__name__], capture_dir)


def split_view_dirs(engine_dirs, views):
    """視点ごとのエンジンの撮影先（RefSplit.resolve_view_dirs）。(視点 → 撮影先, 失敗の一覧)"""
    import RefSplit
    view_dirs = RefSplit.resolve_view_dirs(engine_dirs, views, ".hdr.pfm")
    return view_dirs, [f"{view}: どの撮影先（--engine）にもエンジンの HDR が無い" for view in views if view not in view_dirs]


def split_reference(reference_root, mode, verify):
    """参照の入力の照合（RefSplit.check_reference）。(合格か, 失敗の一覧, 記録)"""
    import RefSplit
    ok, reasons, record = RefSplit.check_reference(sys.modules[__name__], reference_root, mode)
    return ok, (reasons if (verify or record is None) else []), record


def rel_dir(path):
    return os.path.relpath(path, REPO_ROOT).replace("\\", "/")


def run_gray(capture_dir, views, verify, engine_dirs=None, reference_root=None, expect=None):
    """灰色の地形の比較。エンジンの撮影は engine_dirs（視点ごとに最初に見つかる撮影先。省くと capture_dir）、Cycles の参照は
    reference_root/cycles（省くと capture_dir）から読み、結果は capture_dir に書く。
    入力の照らし方は RefSplit.py: エンジンの撮影はエンジンの実装、Cycles は参照の入力で照らす"""
    import RefSplit
    failures = []
    light = load_common_light()
    views = views or list(GRAY_VIEWS)
    engine_dirs = engine_dirs or [capture_dir]
    reference_root = reference_root or capture_dir
    report = {"mode": "gray", "verify": bool(verify), "views": views, "region_criteria": GR.criteria(),
              "engine_dirs": [rel_dir(d) for d in engine_dirs], "reference": rel_dir(reference_root)}
    view_dirs, missing = split_view_dirs(engine_dirs, views)
    failures.extend(missing)

    # エンジンの撮影: 撮影先ごとに、太陽と空の両方（verify の撮影）と、原因の見立て用の太陽だけ・空だけ
    engine_hdrs = {}
    diag_hdrs = {case: {} for case in GRAY_ENGINE_CASES}
    report["engine"] = {}
    report["engine_diagnostic"] = {}
    engine_records = {}
    identities = set()
    engine_current = bool(view_dirs)
    for engine_dir in sorted(set(view_dirs.values())):
        dir_views = [v for v in views if view_dirs.get(v) == engine_dir]
        key = rel_dir(engine_dir)
        main_entry, hdrs, main_failures = gray_engine_capture(engine_dir, "both", dir_views, verify, split_engine_check)
        failures.extend(main_failures)
        engine_hdrs.update(hdrs)
        report["engine"][key] = main_entry
        engine_records[key] = main_entry.get("engine_impl")
        identities.add(main_entry.get("files_sha256"))
        engine_current = engine_current and bool(main_entry.get("inputs_current"))
        for case in GRAY_ENGINE_CASES:
            entry, case_hdrs, case_failures = gray_engine_capture(os.path.join(engine_dir, f"engine-{case}"), case,
                                                                  dir_views, verify, split_engine_check)
            failures.extend(case_failures)
            report["engine_diagnostic"][f"{key}/engine-{case}"] = entry
            engine_records[f"{key}/engine-{case}"] = entry.get("engine_impl")
            diag_hdrs[case].update(case_hdrs)
            engine_current = engine_current and bool(entry.get("inputs_current"))
            if entry.get("files_sha256") and entry.get("files_sha256") != main_entry.get("files_sha256"):
                failures.append(f"{key}/engine-{case} のエンジンの実装が太陽と空の両方の撮影と違う")
    if len(identities) > 1:
        failures.append(f"撮影先ごとのエンジンの実装が揃っていない（{len(identities)} 通り）")

    # Cycles の参照の入力（エンジンの実装には依らない）と、参照とエンジンの撮影の条件の揃い
    cycles_dir = os.path.join(reference_root, GRAY_CYCLES_DIR)
    cycles_ok, cycles_failures, reference = split_reference(reference_root, "gray", verify)
    report["cycles_inputs_current"] = cycles_ok
    report["reference_signature"] = (reference or {}).get("signature_sha256")
    failures.extend(cycles_failures)
    match_failures = RefSplit.check_reference_engine(reference, engine_records, [])
    report["reference_engine_match"] = not match_failures
    failures.extend(match_failures)

    # Cycles 側の尺度の較正（灰色の板）
    report["plates"] = []
    for case in PLATE_CASES:
        plate = gray_check_plate(cycles_dir, case, light)
        report["plates"].append(plate)
        failures.extend(plate["failures"])
        print(f"REFERENCE_CHECK gray plate case={case} pass={int(plate['pass'])} "
              f"expected={plate.get('expected_luminance_cd_m2')} measured={plate.get('measured_luminance_cd_m2')} "
              f"ratio={plate.get('ratio')} ratio_rgb={plate.get('ratio_rgb')}")

    report["view_results"] = []
    for view in views:
        if view not in engine_hdrs:
            failures.append(f"{view}: エンジンの HDR が無い")
            continue
        engine_dir = view_dirs[view]
        aov, depth, aov_failures = gray_check_aov(engine_dir, view, light)
        failures.extend(aov_failures)
        result = check_gray_view(capture_dir, cycles_dir, view, engine_hdrs[view], depth, diag_hdrs, light,
                                 engine_dir=engine_dir)
        result["engine_aov"] = aov
        result["engine_dir"] = rel_dir(engine_dir)
        report["view_results"].append(result)
        failures.extend(f"{view}: {f}" for f in result["failures"])
        cycles = result.get("cycles", {})
        print(f"REFERENCE_CHECK gray view={view} pass={int(result['pass'])} spp_per_seed={cycles.get('spp_per_seed')} "
              f"stop={cycles.get('stop_reason')} sky_mismatch={result.get('sky_mismatch')} "
              f"split={result.get('cycles_split_consistency')}")
        for name, entry in (result.get("regions") or {}).items():
            conv = entry["convergence"]
            print(f"REFERENCE_CHECK gray view={view} region={name} status={entry['status']} pixels={entry['pixels']} "
                  f"engine_y={entry.get('engine_y', float('nan')):.1f} cycles_y={entry.get('cycles_y', float('nan')):.1f} "
                  f"ratio={entry.get('ratio') or float('nan'):.3f} "
                  f"sun_ratio={entry.get('sun_ratio') or float('nan'):.3f} sky_ratio={entry.get('sky_ratio') or float('nan'):.3f} "
                  f"mean_diff={conv.get('mean_diff', float('nan')):.4f} pixel_rms={conv.get('pixel_rms', float('nan')):.3f}")

    apply_expect(report, expect, "gray", failures)
    report["failures"] = failures
    report_path = os.path.join(capture_dir, "gray_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=gray views={len(views)} "
          f"inputs_current={int(engine_current)} cycles_inputs_current={int(cycles_ok)} "
          f"reference={report['reference_signature']} report={report_path}")
    return 0 if not failures else 1


# ---- --albedo（地表の材質の照明の無いアルベド）----

def albedo_engine_capture(capture_dir, views, verify, engine_check=None):
    """エンジンの撮影（共通の照明・地形だけ・最細の LOD）の起動引数・ログの印・撮った時点の常駐。(記録, 失敗の一覧)
    engine_check は gray_engine_capture と同じ"""
    entry = {}
    failures = []
    inputs_ok, input_failures, record = (engine_check or check_inputs)(capture_dir)
    entry["inputs_current"] = inputs_ok
    entry["files_sha256"] = (record or {}).get("identity" if engine_check else "files_sha256")
    entry["engine_impl"] = record if engine_check else None
    entry["game_exe_sha256"] = (record or {}).get("game_exe_sha256")
    if verify and not inputs_ok:
        failures.extend(input_failures)
    game_args = plate_capture_args(capture_dir)
    entry["game_args"] = game_args
    if game_args is None:
        return entry, failures + ["metrics.json が無い"]
    missing = [a for a in ALBEDO_GAME_ARGS if a not in game_args]
    if missing:
        failures.append(f"撮影の起動引数に {', '.join(missing)} が無い")
    if set(AOV_HIDE_NAMES) - aov_hide_names(game_args):
        failures.append("撮影が --wasteland-hide=all でない")
    if any(a.startswith("--wasteland-gray") for a in game_args):
        failures.append("撮影が灰色の材質・板で撮られている")
    entry["views"] = {}
    for view in views:
        view_entry = {}
        entry["views"][view] = view_entry
        log_name = view_game_log(capture_dir, view)
        log_path = os.path.join(capture_dir, log_name) if log_name else None
        if not log_path or not os.path.isfile(log_path):
            failures.append(f"{view}: Game のログが無い")
            continue
        view_entry["game_log"] = log_name
        with open(log_path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        for line in ALBEDO_LOG_LINES:
            if line not in text:
                failures.append(f"{view}: Game のログに {line.strip()} が無い")
        single_view = view if log_name == f"{view}.Game.log" else None
        pages = geometry_pages_at_capture(text.splitlines(), single_view).get(view)
        view_entry["geometry_pages_at_capture"] = pages
        if pages is None:
            failures.append(f"{view}: Game のログに撮影の保存の行か、その前の GEOMETRY_PAGES の行が無い")
        else:
            in_flight = {k: pages.get(k) for k in ("wanted", "reading", "ready", "uploading")}
            if any(in_flight.values()) or len(pages["settle_evicted"]) != 1:
                failures.append(f"{view}: 撮った時点でジオメトリのページが常駐しきっていない（進行中 {in_flight}、"
                                f"追い出しの数 {pages['settle_evicted']}）")
        dropped = [int(v) for v in re.findall(r"MEGA_CAPACITY draw_dropped=(\d+)", text)]
        view_entry["mega_draw_dropped_max"] = max(dropped) if dropped else None
        if dropped and max(dropped) > 0:
            failures.append(f"{view}: 描くクラスタが溢れた（draw_dropped 最大 {max(dropped)}）")
    return entry, failures


def check_albedo_inputs(capture_dir, blender_dir, views, engine_files_sha256, mode="albedo"):
    """Blender の結果（--albedo・--normal）の入力の記録を今のファイルと撮影のログに照らす。(合格か, 理由の一覧, 記録)"""
    import TerrainMaterialReference as TM
    ok, reasons, record = check_blender_inputs(blender_dir)
    if record is None:
        return False, [f"{os.path.basename(blender_dir)}/inputs.json が無い（BlenderReference.py --{mode} を回していない）"], None
    if record.get("kind") != f"blender_{mode}":
        reasons.append(f"記録の種類が blender_{mode} でない（{record.get('kind')}）")
    if sha256_file(os.path.join(REPO_ROOT, TM_SCRIPT)) != record.get("material_script_sha256"):
        reasons.append(f"{TM_SCRIPT} が描いた後に変わった（古い結果）")
    if record.get("engine_capture_files_sha256") != engine_files_sha256:
        reasons.append("値の表とカメラを借りたエンジンの撮影の入力が、今の撮影と違う")
    material = record.get("material") or {}
    if sha256_file(os.path.join(REPO_ROOT, TM.OSL_SOURCE)) != material.get("osl_source_sha256"):
        reasons.append(f"{TM.OSL_SOURCE} が描いた後に変わった（古い結果）")
    osl_path = os.path.join(REPO_ROOT, material.get("osl", ""))
    if not os.path.isfile(osl_path) or sha256_file(osl_path) != material.get("osl_sha256"):
        reasons.append("描いた OSL（terrain_albedo.osl）が無いか、描いた後に変わった")
    table_path = os.path.join(REPO_ROOT, material.get("table", ""))
    if not os.path.isfile(table_path) or sha256_file(table_path) != material.get("table_sha256"):
        reasons.append("値の表（terrain_material.json）が無いか、描いた後に変わった")
        return not reasons, reasons, record
    with open(table_path, encoding="utf-8") as f:
        table = json.load(f)
    # 値の表が、今の撮影のログの材質の値・今の GLSL・詰め方の式と同じか
    logs = [os.path.join(capture_dir, view_game_log(capture_dir, v) or f"{v}.Game.log") for v in views]
    json_text, material_now, log_failures = TM.material_from_logs(logs)
    reasons.extend(log_failures)
    if json_text is not None and TM.sha256_text(json_text) != table.get("material_log_sha256"):
        reasons.append("値の表の材質の値が、今の撮影のログの WASTELAND_TERRAIN_MATERIAL と違う")
    if material_now is not None and TM.pack_params(material_now) != table.get("packed"):
        reasons.append("値の表の詰めた値が、今の詰め方の式と違う")
    if TM.shader_constants() != table.get("shader_constants"):
        reasons.append("値の表の GLSL の定数が、今のシェーダーと違う")
    for rel, digest in (table.get("shader_sha256") or {}).items():
        if sha256_file(os.path.join(REPO_ROOT, rel)) != digest:
            reasons.append(f"{rel} が描いた後に変わった（古い結果）")
    mips_dir = os.path.join(REPO_ROOT, record.get("mips_dir", ""))
    mips_manifest = os.path.join(mips_dir, "mips.json")
    if not os.path.isfile(mips_manifest) or sha256_file(mips_manifest) != material.get("mips_manifest_sha256"):
        reasons.append("ミップの段の一覧（mips.json）が無いか、描いた後に変わった")
    else:
        with open(mips_manifest, encoding="utf-8") as f:
            mips = json.load(f)
        for name, entry in mips.get("textures", {}).items():
            if sha256_file(os.path.join(REPO_ROOT, entry["source"])) != entry.get("source_sha256"):
                reasons.append(f"ミップの段の元の画像 {entry['source']} が作った後に変わった")
    return not reasons, reasons, record


def albedo_luma(image):
    return image.astype(np.float64) @ LUMA


def albedo_ssim_map(a, b):
    """窓（ALBEDO_WINDOW_PX 角）ごとの SSIM。(SSIM の画, 窓の左上の行と列)。窓は ALBEDO_WINDOW_STEP_PX おき"""
    size = ALBEDO_WINDOW_PX
    c1 = (0.01 * ALBEDO_SSIM_RANGE) ** 2
    c2 = (0.03 * ALBEDO_SSIM_RANGE) ** 2

    def window_mean(x):
        integral = np.pad(np.cumsum(np.cumsum(x, axis=0), axis=1), ((1, 0), (1, 0)))
        total = integral[size:, size:] - integral[:-size, size:] - integral[size:, :-size] + integral[:-size, :-size]
        return total / float(size * size)

    mu_a = window_mean(a)
    mu_b = window_mean(b)
    var_a = np.maximum(window_mean(a * a) - mu_a * mu_a, 0.0)
    var_b = np.maximum(window_mean(b * b) - mu_b * mu_b, 0.0)
    cov = window_mean(a * b) - mu_a * mu_b
    ssim = ((2.0 * mu_a * mu_b + c1) * (2.0 * cov + c2)) / ((mu_a ** 2 + mu_b ** 2 + c1) * (var_a + var_b + c2))
    return ssim


def albedo_window_valid(mask):
    """窓の全画素が mask の中か（albedo_ssim_map と同じ並び）"""
    size = ALBEDO_WINDOW_PX
    integral = np.pad(np.cumsum(np.cumsum(mask.astype(np.int64), axis=0), axis=1), ((1, 0), (1, 0)))
    count = integral[size:, size:] - integral[:-size, size:] - integral[size:, :-size] + integral[:-size, :-size]
    return count == size * size


def albedo_regions(engine_depth, blender_normal, valid):
    ny = blender_normal[..., 1]
    flat = ny >= ALBEDO_FLAT_NORMAL_Y
    return {
        "ground_near": valid & flat & (engine_depth < ALBEDO_NEAR_M),
        "ground_mid": valid & flat & (engine_depth >= ALBEDO_NEAR_M) & (engine_depth < ALBEDO_FAR_M),
        "ground_far": valid & flat & (engine_depth >= ALBEDO_FAR_M),
        "slope": valid & (ny >= ALBEDO_CLIFF_NORMAL_Y) & ~flat,
        "cliff": valid & (ny < ALBEDO_CLIFF_NORMAL_Y),
    }


def write_albedo_preview(path, engine, blender, masks, valid):
    """左上: エンジン、右上: Blender（同じ倍率）、左下: log2(Blender ÷ エンジン) の輝度（±0.25 段で飽和。赤が明るい）、右下: 領域"""
    def half(image):
        return np.asarray(Image.fromarray(image).resize((image.shape[1] // 2, image.shape[0] // 2), Image.BOX))
    ratio = np.log2(np.maximum(albedo_luma(blender), 1e-6) / np.maximum(albedo_luma(engine), 1e-6))
    t = np.clip(ratio / 0.25, -1.0, 1.0)
    heat = np.zeros(engine.shape, dtype=np.float64)
    heat[..., 0] = np.where(t > 0, 1.0, 1.0 + t)
    heat[..., 1] = 1.0 - np.abs(t)
    heat[..., 2] = np.where(t < 0, 1.0, 1.0 - t)
    heat = np.where(valid[..., None], heat, 0.15)
    colors = {"ground_near": (230, 200, 60), "ground_mid": (80, 170, 90), "ground_far": (60, 90, 200),
              "slope": (200, 120, 200), "cliff": (230, 110, 40)}
    regions = np.full(engine.shape, 30, dtype=np.uint8)
    for name, color in colors.items():
        regions[masks[name]] = color
    top = np.concatenate([half(gray_display(engine, 2.0)), half(gray_display(blender, 2.0))], axis=1)
    bottom = np.concatenate([half((heat * 255.0).astype(np.uint8)), half(regions)], axis=1)
    Image.fromarray(np.concatenate([top, bottom], axis=0)).save(path)


def check_albedo_view(capture_dir, blender_dir, view):
    failures = []
    result = {"view": view, "failures": failures}
    with open(os.path.join(capture_dir, f"{view}.aov.json"), encoding="utf-8") as f:
        aov_meta = json.load(f)
    meta_path = os.path.join(blender_dir, f"{view}.albedo.json")
    if not os.path.isfile(meta_path):
        failures.append("Blender のアルベドが無い")
        result["pass"] = False
        return result
    with open(meta_path, encoding="utf-8") as f:
        blender_meta = json.load(f)
    camera_failures = gray_camera_match(aov_meta["camera"], blender_meta["camera_engine"])
    if int(blender_meta["width"]) != int(aov_meta["width"]) or int(blender_meta["height"]) != int(aov_meta["height"]):
        camera_failures.append("画素数が違う")
    shifted = shifted_reference_failure(blender_meta)
    if shifted:
        camera_failures.append(shifted)
    failures.extend(camera_failures)
    engine = read_pfm_any(os.path.join(capture_dir, aov_meta["albedo"]["file"])).astype(np.float64)
    depth = read_pfm_any(os.path.join(capture_dir, aov_meta["depth"]["file"]))[..., 0].astype(np.float64)
    blender = read_pfm_any(os.path.join(blender_dir, blender_meta["albedo"]["file"])).astype(np.float64)
    normal = read_pfm_any(os.path.join(blender_dir, blender_meta["normal"]["file"])).astype(np.float64)
    alpha = read_pfm_any(os.path.join(blender_dir, blender_meta["alpha"]["file"]))[..., 0]
    engine_surface = np.isfinite(depth)
    blender_surface = alpha > 0.5
    result["sky_mismatch"] = float(np.mean(engine_surface != blender_surface))
    if result["sky_mismatch"] > GRAY_SKY_MISMATCH:
        failures.append(f"空と表面の分かれ方の食い違いが {result['sky_mismatch']:.4f}（{GRAY_SKY_MISMATCH} 超）")
    # 輪郭（空との境と深度の跳び）から離れた共通の表面
    edges = depth_edges(np.where(engine_surface, depth, np.inf), PROJECTION_JUMP_WEAK) | \
        (engine_surface != blender_surface)
    near_edge = edges.copy()
    for _ in range(ALBEDO_EDGE_MARGIN_PX):
        near_edge = dilate(near_edge)
    valid = engine_surface & blender_surface & ~near_edge
    masks = albedo_regions(depth, normal, valid)
    engine_luma = gaussian_blur(albedo_luma(engine), ALBEDO_PATTERN_BLUR_PX)
    blender_luma = gaussian_blur(albedo_luma(blender), ALBEDO_PATTERN_BLUR_PX)
    ssim = albedo_ssim_map(engine_luma, blender_luma)
    shift = ALBEDO_WINDOW_PX
    control = albedo_ssim_map(engine_luma[:, :-shift], blender_luma[:, shift:])
    step = ALBEDO_WINDOW_STEP_PX
    regions = {}
    discriminating = 0
    for name in ALBEDO_REGIONS:
        mask = masks[name]
        entry = {"pixels": int(mask.sum())}
        regions[name] = entry
        if entry["pixels"] < ALBEDO_MIN_REGION_PX:
            entry["status"] = "not_judged"
            entry["reason"] = f"画素が {ALBEDO_MIN_REGION_PX} 未満"
            continue
        engine_mean = engine[mask].mean(axis=0)
        blender_mean = blender[mask].mean(axis=0)
        ratio = blender_mean / np.maximum(engine_mean, 1e-9)
        entry.update({
            "engine_mean_rgb": engine_mean.tolist(),
            "blender_mean_rgb": blender_mean.tolist(),
            "ratio_rgb": ratio.tolist(),
            "engine_y": float(engine_mean @ LUMA),
            "blender_y": float(blender_mean @ LUMA),
            "median_depth_m": float(np.median(depth[mask])),
        })
        mean_ok = bool(np.all(np.abs(ratio - 1.0) <= ALBEDO_MEAN_TOLERANCE))
        entry["mean_pass"] = mean_ok
        if not mean_ok:
            failures.append(f"{name}: 平均の比 {np.round(ratio, 4).tolist()} が 1 ± {ALBEDO_MEAN_TOLERANCE} の外")
        windows = albedo_window_valid(mask)[::step, ::step]
        values = ssim[::step, ::step][windows]
        control_windows = (albedo_window_valid(mask[:, :-shift]) & albedo_window_valid(mask[:, shift:]))[::step, ::step]
        control_values = control[::step, ::step][control_windows]
        entry["windows"] = int(values.size)
        entry["control_windows"] = int(control_values.size)
        if values.size < ALBEDO_MIN_WINDOWS:
            entry["pattern_status"] = "not_judged"
            entry["status"] = "judged_mean_only"
            continue
        entry["ssim_mean"] = float(values.mean())
        entry["ssim_p10"] = float(np.percentile(values, 10))
        entry["control_ssim_mean"] = float(control_values.mean()) if control_values.size else None
        entry["discriminating"] = bool(control_values.size and
                                       entry["control_ssim_mean"] < ALBEDO_SSIM_MIN - ALBEDO_CONTROL_MARGIN)
        discriminating += int(entry["discriminating"])
        pattern_ok = entry["ssim_mean"] >= ALBEDO_SSIM_MIN
        entry["pattern_pass"] = pattern_ok
        entry["pattern_status"] = "judged"
        entry["status"] = "judged"
        if not pattern_ok:
            failures.append(f"{name}: 模様の SSIM の平均 {entry['ssim_mean']:.3f} が {ALBEDO_SSIM_MIN} 未満")
    if discriminating == 0:
        failures.append("模様の比べ方が見分けられる領域（ずらした対照の SSIM が低い領域）が無い")
    result["regions"] = regions
    result["valid_pixels"] = int(valid.sum())
    result["blender"] = {k: blender_meta.get(k) for k in ("renderer", "samples", "pixel_filter", "triangles",
                                                         "render_seconds", "surface_pixels")}
    preview = os.path.join(capture_dir, f"albedo_{view}.png")
    write_albedo_preview(preview, engine, blender, masks, valid)
    result["preview"] = os.path.relpath(preview, REPO_ROOT).replace("\\", "/")
    result["pass"] = not failures
    return result


def run_albedo(capture_dir, views, verify):
    failures = []
    views = views or list(ALBEDO_VIEWS)
    report = {"mode": "albedo", "verify": bool(verify), "views": views, "criteria": {
        "regions": {"flat_normal_y": ALBEDO_FLAT_NORMAL_Y, "cliff_normal_y": ALBEDO_CLIFF_NORMAL_Y,
                    "near_m": ALBEDO_NEAR_M, "far_m": ALBEDO_FAR_M, "edge_margin_px": ALBEDO_EDGE_MARGIN_PX,
                    "min_pixels": ALBEDO_MIN_REGION_PX},
        "mean_tolerance": ALBEDO_MEAN_TOLERANCE,
        "pattern": {"blur_px": ALBEDO_PATTERN_BLUR_PX, "window_px": ALBEDO_WINDOW_PX, "step_px": ALBEDO_WINDOW_STEP_PX,
                    "ssim_range": ALBEDO_SSIM_RANGE, "ssim_min": ALBEDO_SSIM_MIN,
                    "control_shift_px": ALBEDO_WINDOW_PX, "control_margin": ALBEDO_CONTROL_MARGIN,
                    "min_windows": ALBEDO_MIN_WINDOWS},
    }}
    engine_entry, engine_failures = albedo_engine_capture(capture_dir, views, verify)
    failures.extend(engine_failures)
    report["engine"] = engine_entry
    blender_dir = os.path.join(capture_dir, ALBEDO_RESULT_DIR)
    inputs_ok, input_failures, record = check_albedo_inputs(capture_dir, blender_dir, views,
                                                            engine_entry.get("files_sha256"))
    report["blender_inputs_current"] = inputs_ok
    if verify and not inputs_ok:
        failures.extend(input_failures)
    if record is None:
        failures.extend(input_failures)
    report["view_results"] = []
    for view in views:
        result = check_albedo_view(capture_dir, blender_dir, view)
        report["view_results"].append(result)
        failures.extend(f"{view}: {f}" for f in result["failures"])
        print(f"REFERENCE_CHECK albedo view={view} pass={int(result['pass'])} sky_mismatch={result.get('sky_mismatch')} "
              f"valid_pixels={result.get('valid_pixels')}")
        for name, entry in (result.get("regions") or {}).items():
            ratio = entry.get("ratio_rgb")
            print(f"REFERENCE_CHECK albedo view={view} region={name} status={entry.get('status')} pixels={entry['pixels']} "
                  f"ratio_rgb={np.round(ratio, 4).tolist() if ratio else None} "
                  f"ssim={entry.get('ssim_mean', float('nan')):.3f} control={entry.get('control_ssim_mean') or float('nan'):.3f} "
                  f"windows={entry.get('windows')} discriminating={int(bool(entry.get('discriminating')))}")
    report["failures"] = failures
    report_path = os.path.join(capture_dir, "albedo_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=albedo views={len(views)} "
          f"inputs_current={int(bool(engine_entry.get('inputs_current')))} blender_inputs_current={int(inputs_ok)} "
          f"report={report_path}")
    return 0 if not failures else 1


# ---- --normal（地表の材質の照明の無い法線と粗さ）----

def normal_angle_deg(a, b):
    """2 つの法線の画のなす角（度）。どちらも正規化して測る"""
    a = a / np.maximum(np.linalg.norm(a, axis=-1, keepdims=True), 1e-12)
    b = b / np.maximum(np.linalg.norm(b, axis=-1, keepdims=True), 1e-12)
    return np.degrees(np.arccos(np.clip(np.sum(a * b, axis=-1), -1.0, 1.0)))


def depth_subpixel_shift(engine_depth, blender_depth):
    """エンジンの深度と Blender の深度（どちらも上の行が 0 行目、空は inf）から、エンジンの画素 (x, y) が Blender の画の
    (x + right, y + down) を見ているときの小数のずれ（右・下が正の画素）と深度の相対の偏りを求める。
    (エンジン − Blender) / Blender ≈ 偏り + right · ∂x Blender / Blender + down · ∂y Blender / Blender を、輪郭から離れ
    勾配の小さい画素で最小二乗で解く。比べる値（アルベド・法線・粗さ）は使わない"""
    ok = np.isfinite(engine_depth) & np.isfinite(blender_depth) & (engine_depth > 0) & (blender_depth > 0)
    ok &= ~dilate(dilate(depth_edges(np.where(ok, engine_depth, np.inf), PROJECTION_JUMP_WEAK)))
    safe = np.where(ok, blender_depth, 1.0)
    grad_y, grad_x = np.gradient(safe)
    ax = grad_x / safe
    ay = grad_y / safe
    use = ok & (np.abs(ax) < SUBPIXEL_FIT_MAX_RELATIVE_GRADIENT) & (np.abs(ay) < SUBPIXEL_FIT_MAX_RELATIVE_GRADIENT)
    # 勾配は上下左右の隣も表面の画素で測る
    use[1:-1, 1:-1] &= ok[:-2, 1:-1] & ok[2:, 1:-1] & ok[1:-1, :-2] & ok[1:-1, 2:]
    use[0] = use[-1] = False
    use[:, 0] = use[:, -1] = False
    count = int(use.sum())
    if count < 3:
        return {"right_px": 0.0, "down_px": 0.0, "depth_bias": 0.0, "pixels": count}
    relative = (engine_depth[use] - blender_depth[use]) / blender_depth[use]
    design = np.stack([np.ones(count), ax[use], ay[use]], axis=1)
    solution, *_ = np.linalg.lstsq(design, relative, rcond=None)
    return {"right_px": float(solution[1]), "down_px": float(solution[2]), "depth_bias": float(solution[0]),
            "pixels": count}


def reference_shift_px(meta):
    """Blender の描画の記録のカメラのずらし量（右・上が正の画素。記録に無ければ 0。深度の描画はずらしを持たない）"""
    return [float(v) for v in ((meta or {}).get("camera_shift_px") or [0.0, 0.0])]


def shifted_reference_failure(meta):
    """画素ごとの比較の参照がずらしありで描いた旧参照なら、その失敗の文（SHIFTED_REFERENCE_SCOPE の移行）。ずらし 0 なら None"""
    shift = reference_shift_px(meta)
    if any(v != 0.0 for v in shift):
        return (f"カメラを ({shift[0]:+.3f}, {shift[1]:+.3f}) 画素ずらして描いた旧参照（撮影のカメラのずれを補っていた）。"
                "画素ごとの比較はずらし 0 で描き直すまで古い")
    return None


def write_normal_preview(path, engine_normal, blender_normal, angle, roughness_diff, valid):
    """左上: エンジンの法線、右上: Blender の法線（どちらも 0.5 + 0.5 n）、左下: なす角（0〜NORMAL_P95_MAX_DEG 度を黒〜白、
    超えた所は赤）、右下: 粗さの差 Blender − エンジン（±0.05 で飽和。赤が Blender の方が粗い）"""
    def half(image):
        return np.asarray(Image.fromarray(image).resize((image.shape[1] // 2, image.shape[0] // 2), Image.BOX))

    def encode(normal):
        return (np.clip(np.where(valid[..., None], normal * 0.5 + 0.5, 0.15), 0.0, 1.0) * 255.0).astype(np.uint8)

    t = np.clip(angle / NORMAL_P95_MAX_DEG, 0.0, 1.0)
    heat = np.stack([t, t, t], axis=-1)
    heat[angle > NORMAL_P95_MAX_DEG] = (1.0, 0.1, 0.1)
    heat = np.where(valid[..., None], heat, 0.15)
    d = np.clip(roughness_diff / 0.05, -1.0, 1.0)
    rough = np.zeros(heat.shape, dtype=np.float64)
    rough[..., 0] = np.where(d > 0, 1.0, 1.0 + d)
    rough[..., 1] = 1.0 - np.abs(d)
    rough[..., 2] = np.where(d < 0, 1.0, 1.0 - d)
    rough = np.where(valid[..., None], rough, 0.15)
    top = np.concatenate([half(encode(engine_normal)), half(encode(blender_normal))], axis=1)
    bottom = np.concatenate([half((heat * 255.0).astype(np.uint8)), half((rough * 255.0).astype(np.uint8))], axis=1)
    Image.fromarray(np.concatenate([top, bottom], axis=0)).save(path)


def check_normal_view(capture_dir, blender_dir, view):
    failures = []
    result = {"view": view, "failures": failures}
    with open(os.path.join(capture_dir, f"{view}.aov.json"), encoding="utf-8") as f:
        aov_meta = json.load(f)
    meta_path = os.path.join(blender_dir, f"{view}.normal.json")
    if not os.path.isfile(meta_path):
        failures.append("Blender の法線と粗さが無い")
        result["pass"] = False
        return result
    with open(meta_path, encoding="utf-8") as f:
        blender_meta = json.load(f)
    camera_failures = gray_camera_match(aov_meta["camera"], blender_meta["camera_engine"])
    if int(blender_meta["width"]) != int(aov_meta["width"]) or int(blender_meta["height"]) != int(aov_meta["height"]):
        camera_failures.append("画素数が違う")
    shifted = shifted_reference_failure(blender_meta)
    if shifted:
        camera_failures.append(shifted)
    failures.extend(camera_failures)
    for key, meaning in (("normal", "world_normal_y_up"), ("roughness", "perceptual_roughness")):
        if aov_meta.get(key, {}).get("meaning") != meaning:
            failures.append(f"エンジンの AOV の {key} の意味が {meaning} でない（{aov_meta.get(key, {}).get('meaning')}）")
    engine_normal = read_pfm_any(os.path.join(capture_dir, aov_meta["normal"]["file"])).astype(np.float64)
    engine_rough = read_pfm_any(os.path.join(capture_dir, aov_meta["roughness"]["file"]))[..., 0].astype(np.float64)
    depth = read_pfm_any(os.path.join(capture_dir, aov_meta["depth"]["file"]))[..., 0].astype(np.float64)
    shaded = read_pfm_any(os.path.join(blender_dir, blender_meta["shaded_normal"]["file"])).astype(np.float64)
    base = read_pfm_any(os.path.join(blender_dir, blender_meta["base_normal"]["file"])).astype(np.float64)
    rough = read_pfm_any(os.path.join(blender_dir, blender_meta["roughness"]["file"])).astype(np.float64)
    vertex_normal = read_pfm_any(os.path.join(blender_dir, blender_meta["normal"]["file"])).astype(np.float64)
    alpha = read_pfm_any(os.path.join(blender_dir, blender_meta["alpha"]["file"]))[..., 0]
    engine_surface = np.isfinite(depth)
    blender_surface = alpha > 0.5
    result["sky_mismatch"] = float(np.mean(engine_surface != blender_surface))
    if result["sky_mismatch"] > GRAY_SKY_MISMATCH:
        failures.append(f"空と表面の分かれ方の食い違いが {result['sky_mismatch']:.4f}（{GRAY_SKY_MISMATCH} 超）")
    edges = depth_edges(np.where(engine_surface, depth, np.inf), PROJECTION_JUMP_WEAK) | \
        (engine_surface != blender_surface)
    near_edge = edges.copy()
    for _ in range(ALBEDO_EDGE_MARGIN_PX):
        near_edge = dilate(near_edge)
    valid = engine_surface & blender_surface & ~near_edge
    if "depth" in blender_meta:
        blender_depth = read_pfm_any(os.path.join(blender_dir, blender_meta["depth"]["file"]))[..., 0].astype(np.float64)
        subpixel = depth_subpixel_shift(depth, blender_depth)
        subpixel["camera_shift_px"] = blender_meta.get("camera_shift_px")
        result["subpixel"] = subpixel
        if np.hypot(subpixel["right_px"], subpixel["down_px"]) > NORMAL_SUBPIXEL_MAX_PX:
            failures.append(f"エンジンと Blender の画素の小数のずれの残りが ({subpixel['right_px']:+.3f}, "
                            f"{subpixel['down_px']:+.3f}) 画素（{NORMAL_SUBPIXEL_MAX_PX} 超）")
    else:
        failures.append("Blender の深度が無い（標本の位置を確かめられない）")
    masks = albedo_regions(depth, vertex_normal, valid)
    neighbour_change = np.zeros(depth.shape)
    horizontal = normal_angle_deg(vertex_normal[:, 1:], vertex_normal[:, :-1])
    vertical = normal_angle_deg(vertex_normal[1:], vertex_normal[:-1])
    neighbour_change[:, 1:] = np.maximum(neighbour_change[:, 1:], horizontal)
    neighbour_change[:, :-1] = np.maximum(neighbour_change[:, :-1], horizontal)
    neighbour_change[1:] = np.maximum(neighbour_change[1:], vertical)
    neighbour_change[:-1] = np.maximum(neighbour_change[:-1], vertical)
    resolved = neighbour_change < NORMAL_GEOMETRY_RESOLVED_DEG
    angle = normal_angle_deg(engine_normal, shaded)
    control_angle = normal_angle_deg(engine_normal, base)
    rough_diff = rough[..., 0] - engine_rough
    control_rough_diff = rough[..., 1] - engine_rough
    regions = {}
    normal_discriminating = 0
    rough_discriminating = 0
    for name in ALBEDO_REGIONS:
        mask = masks[name]
        entry = {"pixels": int(mask.sum())}
        regions[name] = entry
        if entry["pixels"] < ALBEDO_MIN_REGION_PX:
            entry["status"] = "not_judged"
            entry["reason"] = f"画素が {ALBEDO_MIN_REGION_PX} 未満"
            continue
        near = mask & (depth < NORMAL_MAX_DEPTH_M)
        far = mask & (depth >= NORMAL_MAX_DEPTH_M)
        unresolved = near & ~resolved
        entry.update({
            "status": "judged",
            "median_depth_m": float(np.median(depth[mask])),
            "normal_pixels": int(near.sum()),
            "beyond_depth_pixels": int(far.sum()),
            "resolved_geometry_pixels": int((near & resolved).sum()),
            "unresolved_geometry_pixels": int(unresolved.sum()),
            "engine_roughness_mean": float(engine_rough[mask].mean()),
            "blender_roughness_mean": float(rough[..., 0][mask].mean()),
            "roughness_mean_diff": float(rough_diff[mask].mean()),
            "roughness_abs_diff_p95": float(np.percentile(np.abs(rough_diff[mask]), 95)),
            "control_roughness_mean_diff": float(control_rough_diff[mask].mean()),
            "lost_variance_mean": float(rough[..., 2][mask].mean()),
        })
        if far.any():
            entry["beyond_depth_angle_median_deg"] = float(np.median(angle[far]))
            entry["beyond_depth_angle_p95_deg"] = float(np.percentile(angle[far], 95))
            entry["beyond_depth_control_angle_p95_deg"] = float(np.percentile(control_angle[far], 95))
        if (near & resolved).any():
            entry["resolved_angle_median_deg"] = float(np.median(angle[near & resolved]))
            entry["resolved_angle_p95_deg"] = float(np.percentile(angle[near & resolved], 95))
        if unresolved.any():
            entry["unresolved_angle_median_deg"] = float(np.median(angle[unresolved]))
            entry["unresolved_angle_p95_deg"] = float(np.percentile(angle[unresolved], 95))
            entry["unresolved_control_angle_p95_deg"] = float(np.percentile(control_angle[unresolved], 95))
        entry["roughness_pass"] = bool(abs(entry["roughness_mean_diff"]) <= ROUGHNESS_MEAN_TOLERANCE)
        if not entry["roughness_pass"]:
            failures.append(f"{name}: 粗さの平均の差 {entry['roughness_mean_diff']:+.4f} が ±{ROUGHNESS_MEAN_TOLERANCE} の外")
        entry["roughness_discriminating"] = bool(abs(entry["control_roughness_mean_diff"]) > ROUGHNESS_MEAN_TOLERANCE)
        rough_discriminating += int(entry["roughness_discriminating"])
        if entry["normal_pixels"] < ALBEDO_MIN_REGION_PX:
            entry["normal_status"] = "not_judged"
            entry["normal_reason"] = f"{NORMAL_MAX_DEPTH_M:.0f} m 未満の画素が {ALBEDO_MIN_REGION_PX} 未満"
            continue
        a = angle[near]
        c = control_angle[near]
        entry.update({
            "normal_status": "judged",
            "angle_median_deg": float(np.median(a)),
            "angle_p95_deg": float(np.percentile(a, 95)),
            "angle_mean_deg": float(a.mean()),
            "control_angle_median_deg": float(np.median(c)),
            "control_angle_p95_deg": float(np.percentile(c, 95)),
        })
        entry["normal_pass"] = bool(entry["angle_median_deg"] <= NORMAL_MEDIAN_MAX_DEG and
                                    entry["angle_p95_deg"] <= NORMAL_P95_MAX_DEG)
        if not entry["normal_pass"]:
            failures.append(f"{name}: 法線のなす角の中央値 {entry['angle_median_deg']:.2f}°・95 百分位 "
                            f"{entry['angle_p95_deg']:.2f}° が上限（{NORMAL_MEDIAN_MAX_DEG}°・{NORMAL_P95_MAX_DEG}°）を超える")
        entry["normal_discriminating"] = bool(entry["control_angle_p95_deg"] > NORMAL_P95_MAX_DEG)
        normal_discriminating += int(entry["normal_discriminating"])
    if normal_discriminating == 0:
        failures.append("法線の比べ方が見分けられる領域（傾きを足さない対照の 95 百分位が上限を超える領域）が無い")
    if rough_discriminating == 0:
        failures.append("粗さの比べ方が見分けられる領域（遠くの分を足さない対照の平均の差が許容を超える領域）が無い")
    result["regions"] = regions
    result["valid_pixels"] = int(valid.sum())
    result["blender"] = {k: blender_meta.get(k) for k in ("renderer", "samples", "pixel_filter", "triangles",
                                                         "render_seconds", "pass_render_seconds", "surface_pixels")}
    preview = os.path.join(capture_dir, f"normal_{view}.png")
    write_normal_preview(preview, engine_normal, shaded, angle, rough_diff, valid)
    result["preview"] = os.path.relpath(preview, REPO_ROOT).replace("\\", "/")
    result["pass"] = not failures
    return result


def run_normal(capture_dir, views, verify):
    failures = []
    views = views or list(ALBEDO_VIEWS)
    report = {"mode": "normal", "verify": bool(verify), "views": views, "criteria": {
        "regions": {"flat_normal_y": ALBEDO_FLAT_NORMAL_Y, "cliff_normal_y": ALBEDO_CLIFF_NORMAL_Y,
                    "near_m": ALBEDO_NEAR_M, "far_m": ALBEDO_FAR_M, "edge_margin_px": ALBEDO_EDGE_MARGIN_PX,
                    "min_pixels": ALBEDO_MIN_REGION_PX},
        "normal_angle_median_max_deg": NORMAL_MEDIAN_MAX_DEG,
        "normal_angle_p95_max_deg": NORMAL_P95_MAX_DEG,
        "normal_max_depth_m": NORMAL_MAX_DEPTH_M,
        "normal_geometry_resolved_deg_breakdown_only": NORMAL_GEOMETRY_RESOLVED_DEG,
        "subpixel_residual_max_px": NORMAL_SUBPIXEL_MAX_PX,
        "roughness_mean_tolerance": ROUGHNESS_MEAN_TOLERANCE,
        "controls": {"normal": "base_normal_without_perturbation", "roughness": "roughness_without_lost_variance"},
    }}
    engine_entry, engine_failures = albedo_engine_capture(capture_dir, views, verify)
    failures.extend(engine_failures)
    report["engine"] = engine_entry
    blender_dir = os.path.join(capture_dir, NORMAL_RESULT_DIR)
    inputs_ok, input_failures, record = check_albedo_inputs(capture_dir, blender_dir, views,
                                                            engine_entry.get("files_sha256"), mode="normal")
    report["blender_inputs_current"] = inputs_ok
    if verify and not inputs_ok:
        failures.extend(input_failures)
    if record is None:
        failures.extend(input_failures)
    report["view_results"] = []
    for view in views:
        result = check_normal_view(capture_dir, blender_dir, view)
        report["view_results"].append(result)
        failures.extend(f"{view}: {f}" for f in result["failures"])
        subpixel = result.get("subpixel") or {}
        print(f"REFERENCE_CHECK normal view={view} pass={int(result['pass'])} sky_mismatch={result.get('sky_mismatch')} "
              f"valid_pixels={result.get('valid_pixels')} camera_shift_px={subpixel.get('camera_shift_px')} "
              f"subpixel_residual_right={subpixel.get('right_px', float('nan')):+.3f} "
              f"subpixel_residual_down={subpixel.get('down_px', float('nan')):+.3f}")
        for name, entry in (result.get("regions") or {}).items():
            if entry.get("status") != "judged":
                print(f"REFERENCE_CHECK normal view={view} region={name} status={entry.get('status')} "
                      f"pixels={entry['pixels']}")
                continue
            if entry.get("normal_status") == "judged":
                normal_text = (f"normal_pixels={entry['normal_pixels']} "
                               f"angle_median={entry['angle_median_deg']:.2f} angle_p95={entry['angle_p95_deg']:.2f} "
                               f"control_median={entry['control_angle_median_deg']:.2f} "
                               f"control_p95={entry['control_angle_p95_deg']:.2f} "
                               f"discriminating_normal={int(entry['normal_discriminating'])}")
            else:
                normal_text = f"normal_pixels={entry['normal_pixels']} normal_status=not_judged"
            beyond = entry.get("beyond_depth_angle_p95_deg")
            resolved_p95 = entry.get("resolved_angle_p95_deg")
            unresolved_p95 = entry.get("unresolved_angle_p95_deg")
            print(f"REFERENCE_CHECK normal view={view} region={name} status=judged pixels={entry['pixels']} "
                  f"depth_m={entry['median_depth_m']:.0f} {normal_text} "
                  f"beyond_{NORMAL_MAX_DEPTH_M:.0f}m_pixels={entry['beyond_depth_pixels']} "
                  f"beyond_angle_p95={beyond if beyond is None else round(beyond, 2)} "
                  f"resolved_pixels={entry['resolved_geometry_pixels']} "
                  f"resolved_angle_p95={resolved_p95 if resolved_p95 is None else round(resolved_p95, 2)} "
                  f"unresolved_pixels={entry['unresolved_geometry_pixels']} "
                  f"unresolved_angle_p95={unresolved_p95 if unresolved_p95 is None else round(unresolved_p95, 2)} "
                  f"rough_engine={entry['engine_roughness_mean']:.4f} rough_blender={entry['blender_roughness_mean']:.4f} "
                  f"rough_diff={entry['roughness_mean_diff']:+.4f} control_rough_diff={entry['control_roughness_mean_diff']:+.4f} "
                  f"lost_variance={entry['lost_variance_mean']:.3f} "
                  f"discriminating_rough={int(entry['roughness_discriminating'])}")
    report["failures"] = failures
    report_path = os.path.join(capture_dir, "normal_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=normal views={len(views)} "
          f"inputs_current={int(bool(engine_entry.get('inputs_current')))} blender_inputs_current={int(inputs_ok)} "
          f"report={report_path}")
    return 0 if not failures else 1


# ---- --lit（実材質の地形のエンジンと Cycles の比較）----

def check_cycles_lit_inputs(capture_dir, cycles_dir, engine_files_sha256, views):
    """Cycles の実材質の描画の入力の記録を今のファイルと照らす。(合格か, 理由の一覧, 記録)"""
    ok, reasons, record = check_blender_inputs(cycles_dir)
    if record is None:
        return False, [f"{GRAY_CYCLES_DIR}/inputs.json が無い（BlenderReference.py --lit を回していない）"], None
    if record.get("kind") != "cycles_lit":
        reasons.append(f"記録の種類が cycles_lit でない（{record.get('kind')}）")
    for key, rel in (("gray_regions_sha256", GRAY_REGIONS_PATH), ("common_light_sha256", COMMON_LIGHT_PATH)):
        if sha256_file(os.path.join(REPO_ROOT, rel)) != record.get(key):
            reasons.append(f"{rel} が描いた後に変わった（古い結果）")
    if record.get("engine_capture_files_sha256") != engine_files_sha256:
        reasons.append("カメラと深度を借りたエンジンの撮影の入力が、今の撮影と違う")
    osl_path = os.path.join(REPO_ROOT, record.get("material_osl", ""))
    if not os.path.isfile(osl_path) or sha256_file(osl_path) != record.get("material_osl_sha256"):
        reasons.append("当たりの材質の OSL が無いか、描いた後に変わった")
    for view in views:
        view_record = (record.get("views") or {}).get(view)
        if view_record is None:
            reasons.append(f"{view}: Cycles の記録に視点が無い")
            continue
        for key, digest in (view_record.get("screen_material_sha256") or {}).items():
            path = os.path.join(capture_dir, LIT_MATERIAL_DIR, f"{view}.{key}.pfm")
            if not os.path.isfile(path) or sha256_file(path) != digest:
                reasons.append(f"{view}: 画素ごとの材質 {key} が Cycles で描いた後に変わった")
        if not view_record.get("screen_material_sha256"):
            reasons.append(f"{view}: 画素ごとの材質のハッシュの記録が無い")
    return not reasons, reasons, record


def lit_engine_hdr(capture_dir, view):
    """エンジンの撮影の HDR と記録。(HDR, hdr.json, 失敗の一覧)"""
    pfm = os.path.join(capture_dir, f"{view}.hdr.pfm")
    meta_path = os.path.join(capture_dir, f"{view}.hdr.json")
    if not os.path.isfile(pfm) or not os.path.isfile(meta_path):
        return None, None, [f"{view}: HDR が無い"]
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    failures = []
    if meta.get("units") != "cd/m2" or meta.get("source") != "scene_color_before_bloom":
        failures.append(f"{view}: hdr.json の単位・読み出し元が約束と違う")
    hdr = DT.read_pfm(pfm).astype(np.float64)
    if not np.isfinite(hdr).all() or (hdr < 0.0).any():
        failures.append(f"{view}: HDR に負・NaN・Inf がある")
    return hdr, meta, failures


def write_lit_comparison(path, engine_hdr, cycles, alpha, meta, light):
    """エンジン｜Cycles を、エンジンの撮影の表示変換（DisplayTransform.py。同じプリエクスポージャ・トーンマップ・
    グレーディング・ビネット）で並べる。Cycles の空（透明の背景）は共通の照明の一様な空の放射輝度で埋める"""
    params = DT.params_from_capture(meta)
    sky = np.asarray(light["sky_radiance_rgb"], dtype=np.float64)
    coverage = np.clip(alpha, 0.0, 1.0)[..., None]
    cycles_sky = cycles + (1.0 - coverage) * sky
    pre_exposure = float(meta["pre_exposure"])
    panels = []
    for image in (engine_hdr, cycles_sky):
        display, _ = DT.display_transform(DT.expose(image.astype(np.float32), pre_exposure), params)
        panels.append(display)
    Image.fromarray(np.concatenate(panels, axis=1), "RGB").save(path)
    return {"operator": params["operator"], "grading_mode": params["grading_mode"], "pre_exposure": pre_exposure,
            "cycles_sky_fill_rgb": sky.tolist()}, panels


def lit_photo_region_values(stats):
    """CompareReference.measure の領域の値に、露出に依らない量（崖 ÷ 地面の輝度・R/G・B/G）を足す"""
    out = {}
    for name in ("cliff", "ground"):
        entry = dict(stats[name])
        if entry["rgb"] is not None and entry["rgb"][1] > 0.0:
            entry["r_over_g"] = entry["rgb"][0] / entry["rgb"][1]
            entry["b_over_g"] = entry["rgb"][2] / entry["rgb"][1]
        out[name] = entry
    cliff, ground = stats["cliff"]["luminance"], stats["ground"]["luminance"]
    out["cliff_over_ground"] = cliff / ground if cliff is not None and ground else None
    return out


def lit_photo_comparison(capture_dir, view, panels, font):
    """写真｜エンジン｜Cycles を並べた比較画像 lit_photo_<視点>.png と、写真の崖・地面の枠（CompareReference.py の PHOTOS）の
    表示の値（sRGB を線形に戻した平均）。写真の現像（露出・ホワイトバランス・トーンカーブ）は分からないので、値の一致は見ず、
    露出に依らない崖 ÷ 地面の輝度の比と色の比（R/G・B/G）を並べる。(記録, 一覧の行, 失敗の一覧)"""
    import CompareReference as CR
    from PIL import ImageDraw
    entry = next((p for p in CR.PHOTOS if p["view"] == view), None)
    if entry is None:
        return None, None, [f"{view}: 対応する写真が CompareReference.py の PHOTOS に無い"]
    photo_path = os.path.join(CR.PHOTO_DIR, entry["id"] + ".jpg")
    if not os.path.isfile(photo_path):
        return None, None, [f"{view}: 写真が無い（{photo_path}）"]
    photo_panel, (letterbox, band), map_y = CR.fit_photo(Image.open(photo_path).convert("RGB"))
    boxes = {name: (box[0], map_y(box[1]), box[2], map_y(box[3])) for name, box in entry.items()
             if name in CR.REGION_COLORS}
    valid = np.ones((CR.PANEL_SIZE[1], CR.PANEL_SIZE[0]), dtype=bool)
    if letterbox:
        valid[:band] = False
        valid[CR.PANEL_SIZE[1] - band:] = False
    images = {"photo": photo_panel}
    for name, display in zip(("engine", "cycles"), panels):
        images[name] = Image.fromarray(display, "RGB").resize(CR.PANEL_SIZE, Image.LANCZOS)
    record = {"photo": entry["id"], "boxes": boxes, "values": {}}
    for name, image in images.items():
        stats, _ = CR.measure(image, boxes, valid if name == "photo" else np.ones_like(valid))
        record["values"][name] = lit_photo_region_values(stats)

    labels = {"photo": f"写真 {entry['id']}", "engine": "エンジン", "cycles": "Cycles"}
    text_height = 96
    width, height = CR.PANEL_SIZE
    sheet = Image.new("RGB", (width * 3, height + text_height), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for index, (name, image) in enumerate(images.items()):
        panel = image.copy()
        CR.draw_boxes(panel, boxes)
        sheet.paste(panel, (index * width, 0))
        values = record["values"][name]
        ratio = values["cliff_over_ground"]
        line = (f"{labels[name]}　" + "　".join(CR.format_region(label, values[key])
                                                for key, label in (("cliff", "崖"), ("ground", "地面"))))
        draw.text((index * width + 8, height + 6), line[:80], fill=(235, 235, 235), font=font)
        draw.text((index * width + 8, height + 34),
                  f"崖 ÷ 地面 {ratio:.2f}" if ratio is not None else "崖 ÷ 地面 －", fill=(235, 235, 235), font=font)
    draw.text((8, height + 62), "表示の値（sRGB を線形に戻した平均）。枠: 橙 = 崖、緑 = 地面。写真は照明・天気・現像が違うので、"
              "差の向きと露出に依らない比だけを見る", fill=(255, 210, 120), font=font)
    file_name = f"lit_photo_{view}.png"
    sheet.save(os.path.join(capture_dir, file_name))
    record["file"] = file_name
    return record, (f"{view}（左: 写真 {entry['id']}　中: エンジン　右: Cycles）", images), []


def write_lit_photo_overview(path, rows, font):
    """写真｜エンジン｜Cycles の組を縦に積んだ一覧"""
    import CompareReference as CR
    from PIL import ImageDraw
    width, height = CR.OVERVIEW_PANEL_SIZE
    row_height = CR.OVERVIEW_LABEL_HEIGHT + height
    sheet = Image.new("RGB", (width * 3, row_height * len(rows)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for index, (label, images) in enumerate(rows):
        top = index * row_height
        draw.text((10, top + 6), label, fill=(235, 235, 235), font=font)
        for column, image in enumerate(images.values()):
            sheet.paste(image.resize(CR.OVERVIEW_PANEL_SIZE, Image.LANCZOS),
                        (column * width, top + CR.OVERVIEW_LABEL_HEIGHT))
    sheet.save(path)


def parse_require_judged(text):
    """--require-judged の <視点>:<領域>,... を [(視点, 領域)] にする。(組の一覧, 誤りの一覧)"""
    pairs, errors = [], []
    for item in (s.strip() for s in (text or "").split(",")):
        if not item:
            continue
        view, _, region = item.partition(":")
        if not view or region not in GR.REGIONS:
            errors.append(f"--require-judged の {item} が <視点>:<領域>（領域は {','.join(GR.REGIONS)}）でない")
            continue
        pairs.append((view, region))
    return pairs, errors


def lit_crop_judge(crop_root, view, regions, engine_dir, engine_hdr, engine_depth, engine_records, engine_logs, verify):
    """本描画で未判定の領域を、影の切り出しの参照（crop_root = <参照>/LIT_CROP_DIR）で判定する（--require-judged）。
    (領域の名前 → 値, 失敗の一覧, 記録)"""
    import RefSplit
    failures = []
    ok, reasons, reference = split_reference(crop_root, "lit", verify)
    failures.extend(reasons)
    record = {"reference": rel_dir(crop_root), "inputs_current": ok,
              "reference_signature": (reference or {}).get("signature_sha256")}
    failures.extend(RefSplit.check_reference_engine(reference, engine_records, engine_logs,
                                                    RefSplit.load_reference_table(sys.modules[__name__], crop_root)))
    cycles_dir = os.path.join(crop_root, GRAY_CYCLES_DIR)
    material_dir = os.path.join(crop_root, LIT_MATERIAL_DIR)
    meta_path = os.path.join(cycles_dir, f"{view}.cycles.json")
    if not os.path.isfile(meta_path):
        return {}, failures + [f"{rel_dir(meta_path)} が無い（BlenderReference.py --lit --crop-regions で描いていない）"], record
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    crop = meta.get("crop") or {}
    record.update({"crop": crop, "spp_per_seed": meta.get("spp_per_seed"), "stop_reason": meta.get("stop_reason"),
                   "settings": meta.get("settings"), "render_seconds": meta.get("render_seconds"),
                   "load_seconds": meta.get("load_seconds"), "consistency": meta.get("consistency")})
    missing = [r for r in regions if r not in (crop.get("regions") or [])]
    if missing:
        failures.append(f"切り出しの描画が領域 {missing} を名指していない（{crop.get('regions')}）")
    for name, digest in (meta.get("outputs_sha256") or {}).items():
        path = os.path.join(cycles_dir, name)
        if not os.path.isfile(path) or sha256_file(path) != digest:
            failures.append(f"{rel_dir(cycles_dir)}/{name} が無いか、記録のハッシュと違う")
    if any(v != 0.0 for v in reference_shift_px(meta)):
        failures.append(f"切り出しの Cycles がカメラを {reference_shift_px(meta)} 画素ずらしている（ずらし 0 で描く）")
    with open(os.path.join(engine_dir, f"{view}.aov.json"), encoding="utf-8-sig") as f:
        failures.extend(gray_camera_match(json.load(f)["camera"], meta["camera_engine"]))
    # 画素ごとの材質（ずらし 0）の標本の位置がエンジンの画素の中心に揃っていること
    material_inputs = os.path.join(material_dir, "inputs.json")
    material_view = {}
    if os.path.isfile(material_inputs):
        with open(material_inputs, encoding="utf-8") as f:
            material_view = (json.load(f).get("views") or {}).get(view) or {}
    if any(v != 0.0 for v in reference_shift_px(material_view)) or not material_view:
        failures.append(f"切り出しの画素ごとの材質の記録が無いか、カメラをずらしている（{material_view.get('camera_shift_px')}）")
    material_depth = os.path.join(material_dir, f"{view}.depth.pfm")
    if os.path.isfile(material_depth):
        subpixel = depth_subpixel_shift(engine_depth.astype(np.float64),
                                        read_pfm_any(material_depth)[..., 0].astype(np.float64))
        record["material_subpixel"] = subpixel
        if math.hypot(subpixel["right_px"], subpixel["down_px"]) > NORMAL_SUBPIXEL_MAX_PX:
            failures.append(f"切り出しの画素ごとの材質の標本の位置がエンジンの画素から ({subpixel['right_px']:+.3f}, "
                            f"{subpixel['down_px']:+.3f}) 画素ずれている（{NORMAL_SUBPIXEL_MAX_PX} 画素以下）")
    else:
        failures.append(f"{rel_dir(material_depth)} が無い")
    if failures:
        return {}, failures, record

    prefix = os.path.join(cycles_dir, view)
    cycles = read_pfm_any(prefix + ".cycles.hdr.pfm").astype(np.float64)
    seed_a = read_pfm_any(prefix + ".cycles.seed_a.pfm").astype(np.float64)
    seed_b = read_pfm_any(prefix + ".cycles.seed_b.pfm").astype(np.float64)
    parts = {g: read_pfm_any(prefix + f".cycles.{g}.pfm").astype(np.float64) for g in LIT_CROP_PARTS}
    visibility = read_pfm_any(prefix + ".aux.visibility.pfm")[..., 0]
    n_dot_l = read_pfm_any(prefix + ".aux.n_dot_l.pfm")[..., 0]
    normal = read_pfm_any(prefix + ".aux.normal.pfm")
    alpha = read_pfm_any(prefix + ".aux.alpha.pfm")[..., 0]
    if cycles.shape != engine_hdr.shape:
        return {}, [f"切り出しの Cycles の大きさ {cycles.shape} がエンジン {engine_hdr.shape} と違う"], record
    for name, image in [("合成", cycles), ("A", seed_a), ("B", seed_b)] + list(parts.items()):
        if not np.isfinite(image).all() or (image < 0.0).any():
            failures.append(f"切り出しの Cycles の {name} に負・NaN・Inf がある")
    # 領域は本描画と同じ定義（エンジンの深度と、切り出しで描いた太陽の見え方・法線）を、描いた枠の中に限る
    x0, y0, x1, y1 = (int(v) for v in crop["border_px"])
    inside = np.zeros(engine_depth.shape, dtype=bool)
    inside[y0:y1 + 1, x0:x1 + 1] = True
    masks = {name: m & inside for name, m in
             GR.region_masks(engine_depth, normal[..., 1], n_dot_l, visibility, alpha > 0.5).items()}
    stats, _ = GR.convergence(seed_a @ LUMA, seed_b @ LUMA, masks)
    spp = int(meta.get("spp_per_seed") or 0)
    engine_y = engine_hdr @ LUMA
    cycles_y = cycles @ LUMA
    entries = {}
    for name in regions:
        mask = masks[name]
        entry = {"label": GR.REGION_LABELS[name], "pixels": int(mask.sum()), "convergence": stats[name],
                 "cycles_source": f"{rel_dir(cycles_dir)}（切り出し）"}
        if stats[name]["converged"] and spp >= LIT_MIN_SPP_PER_SEED:
            entry["status"] = "判定"
        elif stats[name]["pixels"] < GR.MIN_REGION_PX:
            entry["status"] = "未判定（画素が足りない）"
        elif spp < LIT_MIN_SPP_PER_SEED:
            entry["status"] = "未判定（サンプルが下限に届かない）"
        else:
            entry["status"] = "未判定（収束しない）"
        if mask.any():
            e_rgb = gray_region_means(engine_hdr, mask)
            c_rgb = gray_region_means(cycles, mask)
            e_y = float(engine_y[mask].mean())
            c_y = float(cycles_y[mask].mean())
            sums = {g: float((parts[g][mask] @ LUMA).mean()) for g in parts}
            split = {
                "sun_direct_plus_sky_direct_plus_indirect": (sums["sun_direct"] + sums["sky_direct"] + sums["indirect"]) / c_y,
                "direct_plus_indirect": (sums["direct"] + sums["indirect"]) / c_y,
                "sun_plus_sky": (sums["sun"] + sums["sky"]) / c_y,
            } if c_y > 0 else {}
            entry.update({
                "engine_y": e_y, "cycles_y": c_y, "ratio": e_y / c_y if c_y > 0 else None,
                "engine_rgb": e_rgb.tolist(), "cycles_rgb": c_rgb.tolist(),
                "ratio_rgb": (e_rgb / np.maximum(c_rgb, 1e-30)).tolist(),
                "pixel_ratio_p10_p50_p90": np.percentile(engine_y[mask] / np.maximum(cycles_y[mask], 1e-6),
                                                         [10, 50, 90]).tolist(),
                "visibility_mean": float(visibility[mask].mean()),
                "cycles_y_parts": sums,
                "cycles_share": {g: sums[g] / c_y for g in parts} if c_y > 0 else {},
                "cycles_split_consistency": split,
            })
            for key, value in split.items():
                if abs(value - 1.0) > GRAY_SPLIT_TOLERANCE:
                    failures.append(f"{name}: 切り出しの Cycles の {key} の和が合成の {value:.4f} 倍（±{GRAY_SPLIT_TOLERANCE}）")
        entries[name] = entry
    return entries, failures, record


def run_lit(capture_dir, views, verify, engine_dirs=None, reference_root=None, expect=None, require_judged=None):
    """実材質の地形の比較。撮影先・参照の場所・入力の照らし方は run_gray と同じ（参照は <参照>/cycles と <参照>/material）"""
    import RefSplit
    failures = []
    light = load_common_light()
    views = views or list(LIT_VIEWS)
    engine_dirs = engine_dirs or [capture_dir]
    reference_root = reference_root or capture_dir
    report = {"mode": "lit", "verify": bool(verify), "views": views, "region_criteria": GR.criteria(),
              "engine_dirs": [rel_dir(d) for d in engine_dirs], "reference": rel_dir(reference_root)}
    from CompareLight import load_font
    font = load_font(18)
    photo_rows = []
    view_dirs, missing = split_view_dirs(engine_dirs, views)
    failures.extend(missing)

    # エンジンの撮影（共通の照明・地形だけ・最細の LOD・実材質）。撮影先ごとにエンジンの実装で照らす
    report["engine"] = {}
    engine_records = {}
    identities = set()
    engine_current = bool(view_dirs)
    for engine_dir in sorted(set(view_dirs.values())):
        dir_views = [v for v in views if view_dirs.get(v) == engine_dir]
        engine_entry, engine_failures = albedo_engine_capture(engine_dir, dir_views, verify, split_engine_check)
        failures.extend(engine_failures)
        report["engine"][rel_dir(engine_dir)] = engine_entry
        engine_records[rel_dir(engine_dir)] = engine_entry.get("engine_impl")
        identities.add(engine_entry.get("files_sha256"))
        engine_current = engine_current and bool(engine_entry.get("inputs_current"))
    if len(identities) > 1:
        failures.append(f"撮影先ごとのエンジンの実装が揃っていない（{len(identities)} 通り）")

    # 参照の入力（地形のメッシュ・材質の値の表・照明・カメラ・Blender の台本と出力。エンジンの実装には依らない）と、
    # 参照とエンジンの撮影の条件の揃い（照明・撮影のログの材質の値・今のシェーダーの定数）
    material_dir = os.path.join(reference_root, LIT_MATERIAL_DIR)
    cycles_dir = os.path.join(reference_root, GRAY_CYCLES_DIR)
    cycles_ok, cycles_failures, reference = split_reference(reference_root, "lit", verify)
    report["cycles_inputs_current"] = cycles_ok
    report["reference_signature"] = (reference or {}).get("signature_sha256")
    failures.extend(cycles_failures)
    engine_logs = [os.path.join(view_dirs[v], view_game_log(view_dirs[v], v) or f"{v}.Game.log") for v in views
                   if v in view_dirs]
    match_failures = RefSplit.check_reference_engine(reference, engine_records, engine_logs,
                                                     RefSplit.load_reference_table(sys.modules[__name__], reference_root))
    report["reference_engine_match"] = not match_failures
    failures.extend(match_failures)
    material_path = os.path.join(material_dir, "inputs.json")
    material_record = None
    if os.path.isfile(material_path):
        with open(material_path, encoding="utf-8") as f:
            material_record = json.load(f)

    report["view_results"] = []
    for view in views:
        if view not in view_dirs:
            continue
        engine_dir = view_dirs[view]
        engine_hdr, meta, hdr_failures = lit_engine_hdr(engine_dir, view)
        failures.extend(hdr_failures)
        if engine_hdr is None:
            continue
        log_name = view_game_log(engine_dir, view)
        if log_name:
            with open(os.path.join(engine_dir, log_name), encoding="utf-8", errors="replace") as f:
                if f"WASTELAND_COMMON_LIGHT view={view} case=both " not in f.read():
                    failures.append(f"{view}: Game のログに WASTELAND_COMMON_LIGHT view={view} case=both が無い")
        engine_depth = read_pfm_any(os.path.join(engine_dir, f"{view}.aov.depth.pfm"))[..., 0]
        engine_albedo = read_pfm_any(os.path.join(engine_dir, f"{view}.aov.albedo.pfm")).astype(np.float64)

        # 画素ごとの材質の標本の位置が、エンジンの画素の中心に揃っていること（--normal と同じ許容）。ずらしありの旧参照
        # （SHIFTED_REFERENCE_SCOPE）は、記録のカメラ（ずらし 0）からの残りで照らす: Blender のカメラを (右, 上) へずらすと、
        # エンジンの画素 (x, y) は Blender の画の (x − 右, y + 上) を見る
        material_view = (material_record or {}).get("views", {}).get(view, {})
        prefix = os.path.join(material_dir, view)
        subpixel = None
        if os.path.isfile(prefix + ".depth.pfm"):
            subpixel = depth_subpixel_shift(engine_depth.astype(np.float64),
                                            read_pfm_any(prefix + ".depth.pfm")[..., 0].astype(np.float64))
            shift = reference_shift_px(material_view)
            subpixel["camera_shift_px"] = material_view.get("camera_shift_px")
            subpixel["record_camera_right_px"] = subpixel["right_px"] + shift[0]
            subpixel["record_camera_down_px"] = subpixel["down_px"] - shift[1]
            if any(v != 0.0 for v in shift):
                subpixel["reference_scope"] = SHIFTED_REFERENCE_SCOPE
            if math.hypot(subpixel["record_camera_right_px"], subpixel["record_camera_down_px"]) > NORMAL_SUBPIXEL_MAX_PX:
                failures.append(f"{view}: 画素ごとの材質の標本の位置が、記録のカメラから見てエンジンの画素から "
                                f"({subpixel['record_camera_right_px']:+.3f}, {subpixel['record_camera_down_px']:+.3f}) "
                                f"画素ずれている（{NORMAL_SUBPIXEL_MAX_PX} 画素以下。材質のずらし {shift}）")
        else:
            failures.append(f"{view}: 画素ごとの材質の深度 {LIT_MATERIAL_DIR}/{view}.depth.pfm が無い")

        out = {}
        result = check_gray_view(capture_dir, cycles_dir, view, engine_hdr, engine_depth, {}, light,
                                 preview_prefix="lit_regions", out=out, engine_dir=engine_dir)
        result["material_subpixel"] = subpixel
        if int((result.get("cycles") or {}).get("spp_per_seed") or 0) < LIT_MIN_SPP_PER_SEED:
            for entry in (result.get("regions") or {}).values():
                if entry["status"] == "判定":
                    entry["status"] = "未判定（サンプルが下限に届かない）"
        if result.get("cycles_camera_shift_px") != material_view.get("camera_shift_px"):
            result["failures"].append("Cycles のカメラのずらしが画素ごとの材質を描いたときと違う")
            result["pass"] = False
        report["view_results"].append(result)
        failures.extend(f"{view}: {f}" for f in result["failures"])
        if "masks" not in out:
            continue

        # 領域ごとの材質（エンジンの GBuffer のアルベドと、再現した材質のアルベド・AO）
        blender_albedo = read_pfm_any(prefix + ".albedo.pfm").astype(np.float64)
        occlusion = read_pfm_any(prefix + ".occlusion.pfm")[..., 0].astype(np.float64)
        # エンジンだけが掛ける材質の AO の見込み（lighting.frag: 直接光 × mix(1, AO, 0.3)、環境光 × AO）を Cycles の
        # 太陽の組・空の組へ掛けた値。太陽の組の照り返しにも直接光の係数を掛ける近似（原因の見立て用で、合否に使わない）
        ao_model = (out["parts"]["sun"] * (0.7 + 0.3 * occlusion)[..., None] +
                    out["parts"]["sky"] * occlusion[..., None]) @ LUMA
        engine_y = engine_hdr @ LUMA
        for name, entry in result["regions"].items():
            mask = out["masks"][name]
            if not mask.any():
                continue
            entry["engine_albedo_rgb"] = engine_albedo[mask].mean(axis=0).tolist()
            entry["blender_albedo_rgb"] = blender_albedo[mask].mean(axis=0).tolist()
            entry["material_ao_mean"] = float(occlusion[mask].mean())
            entry["cycles_with_engine_ao_y"] = float(ao_model[mask].mean())
            entry["ratio_to_cycles_with_engine_ao"] = float(engine_y[mask].mean() / max(ao_model[mask].mean(), 1e-30))

        # 表示変換を掛けた比較画像（エンジン｜Cycles）
        result["comparison"], panels = write_lit_comparison(os.path.join(capture_dir, f"lit_{view}.png"), engine_hdr,
                                                            out["cycles"], out["alpha"], meta, light)
        result["comparison"]["file"] = f"lit_{view}.png"
        result["comparison"]["regions_file"] = f"lit_regions_{view}.png"

        # 写真｜エンジン｜Cycles（表示の値。写真とは照明・天気・現像が違う）
        photo_record, photo_row, photo_failures = lit_photo_comparison(capture_dir, view, panels, font)
        failures.extend(photo_failures)
        result["photo_comparison"] = photo_record
        if photo_row is not None:
            photo_rows.append(photo_row)
            values = photo_record["values"]
            print(f"REFERENCE_CHECK lit_photo view={view} photo={photo_record['photo']} " + " ".join(
                f"{name}_cliff_over_ground={values[name]['cliff_over_ground'] or float('nan'):.3f} "
                f"{name}_ground_y={values[name]['ground']['luminance'] or float('nan'):.4f} "
                f"{name}_cliff_y={values[name]['cliff']['luminance'] or float('nan'):.4f}"
                for name in ("photo", "engine", "cycles")))

        cycles = result.get("cycles", {})
        print(f"REFERENCE_CHECK lit view={view} pass={int(result['pass'])} spp_per_seed={cycles.get('spp_per_seed')} "
              f"stop={cycles.get('stop_reason')} sky_mismatch={result.get('sky_mismatch')} "
              f"split={result.get('cycles_split_consistency')} "
              f"material_subpixel=({(subpixel or {}).get('right_px', float('nan')):+.3f},"
              f"{(subpixel or {}).get('down_px', float('nan')):+.3f})")
        for name, entry in result["regions"].items():
            conv = entry["convergence"]
            print(f"REFERENCE_CHECK lit view={view} region={name} status={entry['status']} pixels={entry['pixels']} "
                  f"engine_y={entry.get('engine_y', float('nan')):.1f} cycles_y={entry.get('cycles_y', float('nan')):.1f} "
                  f"ratio={entry.get('ratio') or float('nan'):.3f} "
                  f"ratio_rgb={np.round(entry['ratio_rgb'], 3).tolist() if entry.get('ratio_rgb') else None} "
                  f"material_ao={entry.get('material_ao_mean', float('nan')):.3f} "
                  f"ratio_with_engine_ao={entry.get('ratio_to_cycles_with_engine_ao', float('nan')):.3f} "
                  f"mean_diff={conv.get('mean_diff', float('nan')):.4f} pixel_rms={conv.get('pixel_rms', float('nan')):.3f}")

    if photo_rows:
        write_lit_photo_overview(os.path.join(capture_dir, "lit_photo_overview.png"), photo_rows, font)
        report["photo_overview"] = "lit_photo_overview.png"

    # --require-judged: 名指しの領域が判定済みであること。本描画で未判定なら影の切り出しの参照で判定する
    if require_judged is not None:
        pairs, errors = parse_require_judged(require_judged)
        failures.extend(errors)
        report["require_judged"] = {"pairs": [f"{v}:{r}" for v, r in pairs], "crop": {}}
        results_by_view = {r["view"]: r for r in report["view_results"]}
        for view in sorted({v for v, _ in pairs}):
            wanted = [r for v, r in pairs if v == view]
            result = results_by_view.get(view)
            if result is None or not result.get("regions"):
                failures.append(f"{view}: --require-judged の視点を比べていない（--views に入れる）")
                continue
            pending = [r for r in wanted if result["regions"][r]["status"] != "判定"]
            if not pending:
                continue
            engine_dir = view_dirs[view]
            engine_hdr, _meta, _ = lit_engine_hdr(engine_dir, view)
            engine_depth = read_pfm_any(os.path.join(engine_dir, f"{view}.aov.depth.pfm"))[..., 0]
            entries, crop_failures, crop_record = lit_crop_judge(
                os.path.join(reference_root, LIT_CROP_DIR), view, pending, engine_dir, engine_hdr, engine_depth,
                engine_records, engine_logs, verify)
            report["require_judged"]["crop"][view] = crop_record
            failures.extend(f"{view}: {f}" for f in crop_failures)
            for name in pending:
                main = result["regions"][name]
                entry = entries.get(name)
                if entry is None:
                    failures.append(f"{view}:{name} が未判定（本描画は {main['status']}、切り出しの参照で判定できない）")
                    continue
                entry["main"] = {k: main.get(k) for k in ("status", "pixels", "convergence", "cycles_y", "ratio")}
                result["regions"][name] = entry
                conv = entry["convergence"]
                print(f"REFERENCE_CHECK lit_crop view={view} region={name} status={entry['status']} pixels={entry['pixels']} "
                      f"engine_y={entry.get('engine_y', float('nan')):.1f} cycles_y={entry.get('cycles_y', float('nan')):.1f} "
                      f"ratio={entry.get('ratio') or float('nan'):.3f} "
                      f"visibility={entry.get('visibility_mean', float('nan')):.3f} "
                      f"share={json.dumps({k: round(v, 4) for k, v in entry.get('cycles_share', {}).items()})} "
                      f"split={json.dumps({k: round(v, 5) for k, v in entry.get('cycles_split_consistency', {}).items()})} "
                      f"mean_diff={conv.get('mean_diff', float('nan')):.4f} pixel_rms={conv.get('pixel_rms', float('nan')):.3f}")
                if entry["status"] != "判定":
                    failures.append(f"{view}:{name} が未判定（切り出しの参照でも {entry['status']}）")
            print(f"REFERENCE_CHECK lit_crop view={view} reference={crop_record.get('reference_signature')} "
                  f"inputs_current={int(bool(crop_record.get('inputs_current')))} spp_per_seed={crop_record.get('spp_per_seed')} "
                  f"stop={crop_record.get('stop_reason')} border={(crop_record.get('crop') or {}).get('border_px')}")
        for view, region in pairs:
            entry = ((results_by_view.get(view) or {}).get("regions") or {}).get(region)
            if entry is not None and entry["status"] == "判定":
                print(f"REFERENCE_CHECK require_judged {view}:{region} judged=1 source={entry.get('cycles_source', '本描画')}")
    apply_expect(report, expect, "lit", failures)
    report["failures"] = failures
    report_path = os.path.join(capture_dir, "lit_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=lit views={len(views)} "
          f"inputs_current={int(engine_current)} cycles_inputs_current={int(cycles_ok)} "
          f"reference={report['reference_signature']} report={report_path}")
    return 0 if not failures else 1


# ---- --tonemap（表示変換の候補を同じ HDR に掛けて比べる）----

def tonemap_candidate_params(candidate, capture_params):
    """候補の表示変換の値。トーンマップとグレーディングだけを差し替え、コントラストの支点と VignettePass は撮影の値のまま"""
    _name, _label, operator, mode, contrast, saturation, temperature = candidate
    params = dict(capture_params)
    params.update({"operator": operator, "bypass": False, "grading_mode": mode, "color_filter": [1.0, 1.0, 1.0, 1.0],
                   "contrast": contrast, "saturation": saturation, "temperature": temperature, "brightness": 0.0,
                   "look_lut_intensity": 0.0, "tone_map_vignette": [0.0, 0.8, 0.5]})
    return params


def tonemap_current_mismatch(capture_params):
    """撮影の表示変換の記録と候補 current の値の食い違い（無ければ空）"""
    expected = tonemap_candidate_params(TONEMAP_CANDIDATES[0], capture_params)
    mismatch = []
    for key in ("operator", "grading_mode", "bypass"):
        if capture_params[key] != expected[key]:
            mismatch.append(f"{key}={capture_params[key]}")
    for key in ("contrast", "saturation", "temperature", "brightness"):
        if abs(capture_params[key] - expected[key]) > TONEMAP_PARAM_TOLERANCE:
            mismatch.append(f"{key}={capture_params[key]}")
    if any(abs(v - 1.0) > TONEMAP_PARAM_TOLERANCE for v in capture_params["color_filter"]):
        mismatch.append(f"color_filter={capture_params['color_filter']}")
    if capture_params["tone_map_vignette"][0] > TONEMAP_PARAM_TOLERANCE:
        mismatch.append(f"tone_map_vignette={capture_params['tone_map_vignette']}")
    return mismatch


def tonemap_downsample(image, size):
    """(高さ, 幅, チャンネル数) の線形の値を、面積の平均で size（幅, 高さ）へ縮める"""
    channels = [np.asarray(Image.fromarray(np.ascontiguousarray(image[..., c], dtype=np.float32), "F")
                           .resize(size, Image.BOX)) for c in range(image.shape[2])]
    return np.stack(channels, axis=-1).astype(np.float32)


def tonemap_render(hdr, pre_exposure, params, vignette, lut):
    """放射輝度（(H, W, 3) か (N, 1, 3)）に表示変換を掛けた 8 bit の値（DisplayTransform.display_transform と同じ順。
    VignettePass の係数 vignette は hdr と同じ形の先頭 2 軸で渡す）"""
    toned = DT.tone_mapping_pass(DT.expose(hdr, pre_exposure), params, lut)
    if params["vignette_pass"] and params["vignette_enabled"]:
        toned = DT.to_half(toned * vignette)
    encoded = DT.encode_srgb(toned) * np.float32(255.0)
    return np.clip(np.rint(encoded), 0, 255).astype(np.uint8)


def tonemap_luma(image):
    """8 bit の sRGB の画素の輝度を、sRGB の符号化値（0〜1）で返す"""
    from CompareLight import srgb_to_linear
    linear_y = srgb_to_linear(image.astype(np.float64)) @ LUMA
    return DT.encode_srgb(linear_y.astype(np.float32)).astype(np.float64)


def tonemap_masks(terrain, boxes):
    """地形の画素と写真の崖・地面の枠から、崖・地面の領域"""
    import CompareReference as CR
    return {name: terrain & CR.box_mask(terrain.shape, boxes[name]) for name in ("cliff", "ground")}


def tonemap_measure(image, masks):
    """崖・地面の枠の地形の画素の表示の値: 符号化した輝度の分布、彩度、白飛び・黒つぶれ、崖 ÷ 地面の輝度の比、色の比"""
    from CompareLight import srgb_to_linear
    union = masks["cliff"] | masks["ground"]
    out = {"pixels": int(union.sum())}
    if not union.any():
        return out
    linear = srgb_to_linear(image.astype(np.float64))
    luma = tonemap_luma(image)
    p05, p50, p95 = (float(v) for v in np.percentile(luma[union], [5, 50, 95]))
    encoded = image.astype(np.float64) / 255.0
    peak, low = encoded.max(axis=-1), encoded.min(axis=-1)
    lit = union & (peak > TONEMAP_SATURATION_MIN_VALUE)
    out.update({
        "p05": p05, "p50": p50, "p95": p95, "spread": p95 - p05,
        "saturation": float(((peak - low) / np.maximum(peak, 1e-6))[lit].mean()) if lit.any() else None,
        "clip": float((image >= TONEMAP_CLIP_LEVEL).any(axis=-1)[union].mean()),
        "crush": float((image <= TONEMAP_CRUSH_LEVEL).all(axis=-1)[union].mean()),
    })
    for name in ("cliff", "ground"):
        mask = masks[name]
        if not mask.any():
            continue
        rgb = linear[mask].mean(axis=0)
        out[f"{name}_y"] = float(rgb @ LUMA)
        out[f"{name}_rgb"] = [float(v) for v in rgb]
        if rgb[1] > 0.0:
            out[f"{name}_r_over_g"] = float(rgb[0] / rgb[1])
            out[f"{name}_b_over_g"] = float(rgb[2] / rgb[1])
    if out.get("cliff_y") and out.get("ground_y"):
        out["cliff_over_ground"] = out["cliff_y"] / out["ground_y"]
        out["log_cliff_over_ground"] = math.log(out["cliff_over_ground"])
    return out


def tonemap_match_exposure(hdr, pre_exposure, params, vignette, mask, target_p50, lut):
    """枠の中の明るさの中央値（符号化した輝度）を写真に合わせる露出の段 s（プリエクスポージャ × 2^s）を二分で探す。
    範囲の端に張り付いたら端の値を返す"""
    pixels = hdr[mask][:, None, :]
    factors = vignette[mask][:, None, :]
    low, high = TONEMAP_MATCH_RANGE_EV
    for _ in range(TONEMAP_MATCH_STEPS):
        middle = 0.5 * (low + high)
        image = tonemap_render(pixels, pre_exposure * 2.0 ** middle, params, factors, lut)
        if float(np.median(tonemap_luma(image))) < target_p50:
            low = middle
        else:
            high = middle
    return 0.5 * (low + high)


def tonemap_missing_metrics(metrics):
    """点数と表に使う量のうち、無いか有限でないもの"""
    keys = [m for m, _ in TONEMAP_SCORE_METRICS] + ["p05", "p95", "crush"]
    return [k for k in keys if metrics.get(k) is None or not math.isfinite(metrics[k])]


def tonemap_scores(view_results, source, exposure):
    """候補ごとの写真との差の点数。量ごとに視点の平均の差の絶対値を求め、候補の間の平均で割って量の数で平均する
    （1 が候補の平均、小さいほど写真に近い）"""
    metrics = [m for m, _ in TONEMAP_SCORE_METRICS if not (exposure == "matched" and m == "p50")]
    names = [c[0] for c in TONEMAP_CANDIDATES]
    diffs = {name: {} for name in names}
    for name in names:
        for metric in metrics:
            values = []
            for result in view_results:
                photo = result["photo"]["metrics"].get(metric)
                value = result.get(source, {}).get(exposure, {}).get(name, {}).get("metrics", {}).get(metric)
                if photo is not None and value is not None:
                    values.append(abs(value - photo))
            diffs[name][metric] = float(np.mean(values)) if values else None
    score = {name: [] for name in names}
    for metric in metrics:
        present = [diffs[name][metric] for name in names if diffs[name][metric] is not None]
        scale = float(np.mean(present)) if present else 0.0
        if scale <= 0.0:
            continue
        for name in names:
            if diffs[name][metric] is not None:
                score[name].append(diffs[name][metric] / scale)
    score = {name: float(np.mean(values)) if values else None for name, values in score.items()}
    rank = sorted((name for name in names if score[name] is not None), key=lambda name: score[name])
    return {"metrics": metrics, "mean_abs_diff": diffs, "score": score, "rank": rank}


def tonemap_cell_text(name, metrics, exposure_ev=None):
    text = (f"{name}　中央 {metrics.get('p50', float('nan')):.2f}　幅 {metrics.get('spread', float('nan')):.2f}　"
            f"彩 {metrics.get('saturation') or float('nan'):.2f}　崖/地 {metrics.get('cliff_over_ground') or float('nan'):.2f}")
    if exposure_ev is not None:
        text += f"　{exposure_ev:+.2f} 段"
    return text


def write_tonemap_sheet(path, rows, boxes, font, panel_size, note=None):
    """比較画像。rows は (行の見出し, [(列の見出し, 8 bit の画像), ...]) の並び"""
    import CompareReference as CR
    from PIL import ImageDraw
    width, height = panel_size
    columns = max(len(cells) for _, cells in rows)
    row_height = TONEMAP_LABEL_HEIGHT * 2 + height
    footer = TONEMAP_LABEL_HEIGHT if note else 0
    sheet = Image.new("RGB", (width * columns, row_height * len(rows) + footer), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for index, (title, cells) in enumerate(rows):
        top = index * row_height
        draw.text((8, top + 6), title, fill=(255, 210, 120), font=font)
        for column, (label, image) in enumerate(cells):
            panel = Image.fromarray(image, "RGB").resize(panel_size, Image.LANCZOS)
            if boxes is not None:
                CR.draw_boxes(panel, boxes)
            sheet.paste(panel, (column * width, top + TONEMAP_LABEL_HEIGHT * 2))
            draw.text((column * width + 6, top + TONEMAP_LABEL_HEIGHT + 6), label[:60], fill=(235, 235, 235), font=font)
    if note:
        draw.text((8, row_height * len(rows) + 6), note, fill=(255, 210, 120), font=font)
    sheet.save(path)


def run_tonemap(capture_dir, lit_dir, views, verify):
    import CompareReference as CR
    from CompareLight import load_font
    failures = []
    views = views or list(TONEMAP_VIEWS)
    light = load_common_light()
    lut = DT.load_aces_lut()
    font = load_font(15)
    size = CR.PANEL_SIZE
    names = [c[0] for c in TONEMAP_CANDIDATES]
    report = {"mode": "tonemap", "verify": bool(verify), "views_requested": views, "lit_capture": lit_dir,
              "candidates": [{"name": c[0], "label": c[1], "operator": c[2], "grading_mode": c[3], "contrast": c[4],
                              "saturation": c[5], "temperature": c[6]} for c in TONEMAP_CANDIDATES],
              "panel_size": list(size), "views": []}

    # エンジンの撮影（既定の荒野の表示）
    inputs_ok, input_failures, record = check_inputs(capture_dir)
    report["inputs_current"] = inputs_ok
    report["inputs_record"] = record
    if verify and not inputs_ok:
        failures.extend(input_failures)
    game_args = plate_capture_args(capture_dir)
    report["game_args"] = game_args
    if game_args is None:
        failures.append("metrics.json が無い（CaptureScene.ps1 -Hdr の出力先でない）")
    else:
        forbidden = [a for a in game_args if a.startswith(TONEMAP_FORBIDDEN_ARG_PREFIXES)]
        if forbidden:
            failures.append(f"撮影が既定の荒野の表示でない（起動引数 {', '.join(forbidden)}）")

    # Cycles の描画と、その露出を決めたエンジンの撮影（SCN-WLR-CYCLES-LIT）
    lit_ok, lit_failures, lit_record = check_inputs(lit_dir)
    cycles_dir = os.path.join(lit_dir, GRAY_CYCLES_DIR)
    cycles_ok, cycles_failures, cycles_record = check_cycles_lit_inputs(lit_dir, cycles_dir,
                                                                        (lit_record or {}).get("files_sha256"), views)
    report["lit_inputs_current"] = lit_ok
    report["cycles_inputs_current"] = cycles_ok
    if verify and not lit_ok:
        failures.extend(f"Cycles の撮影: {f}" for f in lit_failures)
    if (verify and not cycles_ok) or cycles_record is None:
        failures.extend(f"Cycles: {f}" for f in cycles_failures)

    overview_rows = []
    for view in views:
        entry = {"view": view}
        report["views"].append(entry)
        hdr_result = check_hdr_view(capture_dir, view)
        entry["hdr_png_match"] = dict(hdr_result.get("png_match", {}), **{"pass": hdr_result["pass"]})
        failures.extend(f"{view}: {f}" for f in hdr_result["failures"])
        engine_hdr, meta, hdr_failures = lit_engine_hdr(capture_dir, view)
        failures.extend(hdr_failures)
        lit_hdr, lit_meta, lit_hdr_failures = lit_engine_hdr(lit_dir, view)
        failures.extend(f"Cycles の撮影: {f}" for f in lit_hdr_failures)
        if engine_hdr is None or lit_meta is None:
            continue
        try:
            capture_params = DT.params_from_capture(meta)
        except DT.UnsupportedDisplay as error:
            failures.append(f"{view}: {error}")
            continue
        mismatch = tonemap_current_mismatch(capture_params)
        entry["capture_display"] = {k: capture_params[k] for k in ("operator", "grading_mode", "contrast", "contrast_pivot",
                                                                    "saturation", "temperature", "vignette")}
        if mismatch:
            failures.append(f"{view}: 撮影の表示変換が候補 current と違う（{', '.join(mismatch)}）")

        prefix = os.path.join(cycles_dir, view)
        if not (os.path.isfile(prefix + ".cycles.hdr.pfm") and os.path.isfile(prefix + ".aux.alpha.pfm")):
            failures.append(f"{view}: Cycles の描画 {GRAY_CYCLES_DIR}/{view}.cycles.hdr.pfm か被覆が無い")
            continue
        cycles = read_pfm_any(prefix + ".cycles.hdr.pfm").astype(np.float32)
        alpha = read_pfm_any(prefix + ".aux.alpha.pfm")[..., :1].astype(np.float32)
        if cycles.shape != engine_hdr.shape or not np.isfinite(cycles).all():
            failures.append(f"{view}: Cycles の描画の大きさがエンジンと違うか、有限でない値がある")
            continue
        coverage = np.clip(alpha, 0.0, 1.0)
        cycles_sky = cycles + (1.0 - coverage) * np.asarray(light["sky_radiance_rgb"], dtype=np.float32)

        # 写真（崖・地面の枠と地形の画素）
        photo_entry = next((p for p in CR.PHOTOS if p["view"] == view), None)
        photo_path = os.path.join(CR.PHOTO_DIR, photo_entry["id"] + ".jpg") if photo_entry else ""
        if not os.path.isfile(photo_path):
            failures.append(f"{view}: 対応する写真が無い")
            continue
        photo_panel, (letterbox, band), map_y = CR.fit_photo(Image.open(photo_path).convert("RGB"))
        boxes = {name: (box[0], map_y(box[1]), box[2], map_y(box[3])) for name, box in photo_entry.items()
                 if name in CR.REGION_COLORS}
        valid = np.ones((size[1], size[0]), dtype=bool)
        if letterbox:
            valid[:band] = False
            valid[size[1] - band:] = False
        photo = np.asarray(photo_panel)
        rows_index = np.arange(size[1])[:, None]
        photo_metrics = tonemap_measure(photo, tonemap_masks((rows_index >= CR.find_terrain_top(photo)[None, :]) & valid,
                                                             boxes))
        entry["photo"] = {"id": photo_entry["id"], "boxes": boxes, "metrics": photo_metrics}
        missing = tonemap_missing_metrics(photo_metrics)
        if missing:
            failures.append(f"{view}: 写真の枠の値が測れない（{', '.join(missing)}）")
            continue

        vignette = DT.vignette_factor(size[1], size[0], *capture_params["vignette"])
        sources = {
            "engine": ("エンジン", tonemap_downsample(engine_hdr, size), float(meta["pre_exposure"]), None),
            "cycles": ("Cycles", tonemap_downsample(cycles_sky, size), float(lit_meta["pre_exposure"]),
                       tonemap_downsample(coverage, size)[..., 0] > 0.5),
        }
        sheet_rows = []
        overview_cells = {}
        for source, (source_label, hdr, pre_exposure, terrain) in sources.items():
            if terrain is None:
                # エンジンの地形の画素は、候補 current を撮影の露出で掛けた絵の輪郭から 1 度だけ決める（候補の間で同じ画素を比べる）
                current = tonemap_render(hdr, pre_exposure, tonemap_candidate_params(TONEMAP_CANDIDATES[0], capture_params),
                                         vignette, lut)
                terrain = rows_index >= CR.find_terrain_top(current)[None, :]
            masks = tonemap_masks(terrain, boxes)
            union = masks["cliff"] | masks["ground"]
            source_entry = {"pre_exposure": pre_exposure, "as_is": {}, "matched": {}}
            entry[source] = source_entry
            cells = {"as_is": [(f"写真 {photo_entry['id']}", photo)], "matched": [(f"写真 {photo_entry['id']}", photo)]}
            for candidate in TONEMAP_CANDIDATES:
                name = candidate[0]
                params = tonemap_candidate_params(candidate, capture_params)
                image = tonemap_render(hdr, pre_exposure, params, vignette, lut)
                metrics = tonemap_measure(image, masks)
                source_entry["as_is"][name] = {"metrics": metrics}
                cells["as_is"].append((tonemap_cell_text(name, metrics), image))
                if union.any():
                    ev = tonemap_match_exposure(hdr, pre_exposure, params, vignette, union, photo_metrics["p50"], lut)
                    matched = tonemap_render(hdr, pre_exposure * 2.0 ** ev, params, vignette, lut)
                    matched_metrics = tonemap_measure(matched, masks)
                    source_entry["matched"][name] = {"exposure_ev": ev, "metrics": matched_metrics}
                    cells["matched"].append((tonemap_cell_text(name, matched_metrics, ev), matched))
                for exposure in ("as_is", "matched"):
                    result = source_entry[exposure].get(name)
                    missing = tonemap_missing_metrics(result["metrics"]) if result else ["枠の中に地形の画素が無い"]
                    if missing:
                        failures.append(f"{view}: {source_label}・{name}・{exposure} の値が測れない（{', '.join(missing)}）")
                        continue
                    m = result["metrics"]
                    print(f"REFERENCE_CHECK tonemap view={view} source={source} exposure={exposure} candidate={name} "
                          f"ev={result.get('exposure_ev', 0.0):+.3f} p05={m['p05']:.3f} p50={m['p50']:.3f} p95={m['p95']:.3f} "
                          f"spread={m['spread']:.3f} saturation={m['saturation']:.3f} clip={m['clip']:.4f} "
                          f"crush={m['crush']:.4f} cliff_over_ground={m['cliff_over_ground']:.3f} "
                          f"ground_rg={m['ground_r_over_g']:.3f} ground_bg={m['ground_b_over_g']:.3f}")
            sheet_rows.append((f"{source_label}・撮影の露出（{pre_exposure:.3g}）", cells["as_is"]))
            sheet_rows.append((f"{source_label}・写真の枠の明るさの中央値に露出を合わせた", cells["matched"]))
            # 一覧の枠は小さいので、見出しは候補の名前と中央値・彩度だけにする
            short = [cells["as_is"][0]] + [
                (f"{name} 中 {source_entry['as_is'][name]['metrics'].get('p50', float('nan')):.2f} "
                 f"彩 {source_entry['as_is'][name]['metrics'].get('saturation') or float('nan'):.2f}", image)
                for name, (_, image) in zip(names, cells["as_is"][1:])]
            overview_cells[source] = (f"{view}　{source_label}・撮影の露出", short)
        m = photo_metrics
        print(f"REFERENCE_CHECK tonemap view={view} source=photo id={photo_entry['id']} p05={m['p05']:.3f} p50={m['p50']:.3f} "
              f"p95={m['p95']:.3f} spread={m['spread']:.3f} saturation={m['saturation']:.3f} clip={m['clip']:.4f} "
              f"crush={m['crush']:.4f} cliff_over_ground={m['cliff_over_ground']:.3f} "
              f"ground_rg={m['ground_r_over_g']:.3f} ground_bg={m['ground_b_over_g']:.3f}")
        file_name = f"tonemap_{view}.png"
        write_tonemap_sheet(os.path.join(capture_dir, file_name), sheet_rows, boxes, font, TONEMAP_SHEET_PANEL,
                            note="中央 = 枠の中の明るさの中央値、幅 = p95 − p05（符号化した輝度）、彩 = HSV の S の平均、"
                                 "崖/地 = 崖 ÷ 地面の輝度。写真は照明・天気・現像が違う")
        entry["sheet"] = file_name
        overview_rows.extend(overview_cells[s] for s in ("engine", "cycles") if s in overview_cells)

    # 写真との差の点数（エンジン・Cycles × 撮影の露出・合わせた露出）
    measured = [e for e in report["views"] if "engine" in e and "cycles" in e]
    report["scores"] = {f"{source}_{exposure}": tonemap_scores(measured, source, exposure)
                        for source in ("engine", "cycles") for exposure in ("as_is", "matched")}
    for key, score in report["scores"].items():
        print(f"REFERENCE_CHECK tonemap_score set={key} views={len(measured)} " +
              " ".join(f"{name}={score['score'][name]:.3f}" if score["score"][name] is not None else f"{name}=nan"
                       for name in names) + f" rank={','.join(score['rank'])}")
    if len(measured) != len(views):
        failures.append(f"比べられた視点が {len(measured)}/{len(views)}")
    if overview_rows:
        write_tonemap_sheet(os.path.join(capture_dir, "tonemap_overview.png"), overview_rows, None, font,
                            TONEMAP_OVERVIEW_PANEL, note="列: 写真、" + "、".join(names) + "（撮影の露出のまま）")
        report["overview"] = "tonemap_overview.png"

    report["failures"] = failures
    report_path = os.path.join(capture_dir, "tonemap_check.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    status = "pass" if not failures else "fail"
    print(f"REFERENCE_CHECK result={status} mode=tonemap views={len(measured)} inputs_current={int(inputs_ok)} "
          f"lit_inputs_current={int(lit_ok)} cycles_inputs_current={int(cycles_ok)} report={report_path}")
    return 0 if not failures else 1


def main():
    # 結果の行を保存して読むので、端末の符号に関わらず UTF-8 で書く
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="荒野の正解の絵の比較で、保存した撮影の結果を検査する")
    parser.add_argument("--capture", default="",
                        help="撮影の出力先（CaptureScene.ps1 の -OutDir）。--gap-list で省くと GAP_LIST_DIR")
    parser.add_argument("--record-inputs", action="store_true", help="入力のハッシュを inputs.json に書く")
    parser.add_argument("--kind", default="engine", choices=sorted(INPUT_SETS), help="--record-inputs の入力の種類")
    parser.add_argument("--game-exe", default="", help="--record-inputs で中身のハッシュを書く、撮影に使った Game.exe")
    parser.add_argument("--hdr", action="store_true", help="ブルーム前の HDR の撮影を検査する")
    parser.add_argument("--display", action="store_true", help="表示変換の再現（DisplayTransform.py）を照合の撮影と照らす")
    parser.add_argument("--plate", action="store_true", help="共通の照明の灰色の板の明るさを、入力から求めた期待と照らす")
    parser.add_argument("--aov", action="store_true", help="GBuffer の生の値（AOV）の撮影を検査する")
    parser.add_argument("--camera-offset", action="store_true",
                        help="撮影の画と記録のカメラのずれを、補正なしで描いた Blender の深度で検査する（CameraOffset.py）")
    parser.add_argument("--projection", action="store_true",
                        help="Blender の深度（BlenderReference.py）とエンジンの深度の投影を照らす")
    parser.add_argument("--atmos-dump", action="store_true",
                        help="空と霞の値の書き出し（--atmosphere-dump）が揃い、描画に使われた値と合うことを検査する")
    parser.add_argument("--atmos-ref", action="store_true",
                        help="空と霞の書き出しを、AtmosphereReference.py の独立な正解と比べた表を作る")
    parser.add_argument("--blur-static", action="store_true",
                        help="遠景のぼけを、2 倍の解像度の正解と条件ごとの撮影の帯域ごとの差で切り分けた表を作る")
    parser.add_argument("--blur-motion", action="store_true",
                        help="遠景のぼけを、カメラを振りながら撮った画と止めて撮った画で、TAA の履歴による細部の消失に切り分けた表を作る")
    parser.add_argument("--gray", action="store_true",
                        help="灰色の地形のエンジンと Cycles の描画を、領域ごとの線形の平均の比で比べた表を作る")
    parser.add_argument("--albedo", action="store_true",
                        help="地表の材質の照明の無いアルベドを、エンジンの AOV と Blender の再現で照らす")
    parser.add_argument("--normal", action="store_true",
                        help="地表の材質の照明の無い法線と粗さを、エンジンの AOV と Blender の再現で照らす")
    parser.add_argument("--lit", action="store_true",
                        help="実材質の地形のエンジンと Cycles の描画を、領域ごとの線形の平均の表と表示変換を掛けた比較画像で比べる")
    parser.add_argument("--tonemap", action="store_true",
                        help="表示変換の候補を、エンジンと Cycles の同じ HDR に掛けて並べ、手本の写真と比べる")
    parser.add_argument("--grading", action="store_true",
                        help="荒野のグレーディングが中立で、HDR に neutral_plain を掛けた画が撮影の PNG と合うことを検査し、地面の R/G と白飛びの前後を記録する")
    parser.add_argument("--gap-list", action="store_true",
                        help="比較の結果が揃い今の入力と合うことを検査し、差を分類した一覧（WastelandGapList.md の表）と一覧の画像を作る")
    parser.add_argument("--require-atmos", action="store_true",
                        help="--gap-list で、大気の正解の比較（SCN-WLR-ATMOS-REF）の古さと B の直し待ちの項目を許さない")
    parser.add_argument("--lit-capture", default=TONEMAP_LIT_DIR,
                        help="--tonemap で Cycles の HDR を借りる SCN-WLR-CYCLES-LIT の出力先")
    parser.add_argument("--engine", action="append", default=[],
                        help="--gray・--lit・--blur-static・--blur-motion のエンジンの撮影先（複数回指定でき、視点・組ごとに最初に"
                             "見つかる撮影先を読む。省くと --capture）")
    parser.add_argument("--reference", default="",
                        help="保存した参照の場所（--gray・--lit は cycles/・material/ を持つ所、--blur-* は正解の ref2x の組。"
                             "省くと --capture・--engine から探す）")
    parser.add_argument("--record-reference", choices=("gray", "lit"),
                        help="--reference（省くと --capture）の Cycles の参照の入力の記録 cycles/reference.json を書く")
    parser.add_argument("--record-engine", action="store_true",
                        help="保存した撮影の inputs.json にエンジンの実装の記録を書き足す（--game-exe が撮影に使った exe と同じ中身のときだけ）")
    parser.add_argument("--ref-split-selftest", action="store_true",
                        help="保存の写しで、参照の入力とエンジンの実装の署名の正と負を確かめる")
    parser.add_argument("--regression", action="store_true",
                        help="回帰の撮影の after（--capture）を before（--reference）と並べる（CaptureRegression.ps1 が呼ぶ）")
    parser.add_argument("--expect", default="",
                        help="--gray・--lit・--blur-static・--blur-motion で、領域・成分ごとの合否の閾値の仕様（Scripts/Terrain/ExpectSpecs/ の JSON）"
                             "の各行も判定する")
    parser.add_argument("--expect-selftest", action="store_true",
                        help="保存した撮影で、今の値で通る仕様が通り、外れる行・許さない未判定・仕様の誤りで落ちることを確かめる")
    parser.add_argument("--require-judged", default=None,
                        help="--lit: 判定済みでなければ落とす領域（<視点>:<領域>,...）。本描画で未判定なら <参照>/"
                             f"{LIT_CROP_DIR} の切り出しの参照で判定する")
    parser.add_argument("--views", default="", help="検査する視点（カンマ区切り。省くと metrics.json の全視点）")
    parser.add_argument("--verify", action="store_true", help="入力のハッシュを今のファイルと照らし、古い結果を拒否する")
    args = parser.parse_args()
    if args.expect_selftest:
        import ExpectSpec
        return ExpectSpec.run_selftest(sys.modules[__name__], args.verify)
    expect = os.path.normpath(args.expect if os.path.isabs(args.expect) else os.path.join(REPO_ROOT, args.expect))         if args.expect else None
    if expect and not (args.gray or args.lit or args.blur_static or args.blur_motion):
        print("REFERENCE_CHECK result=fail reason=expect_unsupported（--expect は --gray・--lit・--blur-static・--blur-motion と使う）")
        return 1
    if args.require_judged is not None and not args.lit:
        print("REFERENCE_CHECK result=fail reason=require_judged_unsupported（--require-judged は --lit と使う）")
        return 1
    if args.ref_split_selftest:
        import RefSplit
        return RefSplit.run_selftest(sys.modules[__name__], args.verify)
    if args.gap_list:
        import GapList
        capture = args.capture or GAP_LIST_DIR
        capture_dir = os.path.normpath(capture if os.path.isabs(capture) else os.path.join(REPO_ROOT, capture))
        return GapList.run(sys.modules[__name__], capture_dir, args.verify, args.require_atmos)
    if args.grading:
        import GradeNeutral
        absolute = lambda d: os.path.normpath(d if os.path.isabs(d) else os.path.join(REPO_ROOT, d))
        views = [v.strip() for v in args.views.split(",") if v.strip()]
        return GradeNeutral.run(sys.modules[__name__], [absolute(d) for d in args.engine],
                                absolute(args.capture) if args.capture else "", views, args.verify)
    if args.camera_offset:
        import CameraOffset
        capture = args.capture or (args.engine[0] if args.engine else "")
        capture_dir = os.path.normpath(capture if os.path.isabs(capture) else os.path.join(REPO_ROOT, capture))
        if not capture or not os.path.isdir(capture_dir):
            print(f"REFERENCE_CHECK result=fail reason=capture_dir_missing（--engine に撮影先を指定する） path={capture}")
            return 1
        views = [v.strip() for v in args.views.split(",") if v.strip()]
        return CameraOffset.run(sys.modules[__name__], capture_dir, views, args.verify)
    if not args.capture:
        print("REFERENCE_CHECK result=fail reason=capture_missing（--capture を指定する）")
        return 1
    capture_dir = args.capture if os.path.isabs(args.capture) else os.path.join(REPO_ROOT, args.capture)
    capture_dir = os.path.normpath(capture_dir)
    if not os.path.isdir(capture_dir):
        print(f"REFERENCE_CHECK result=fail reason=capture_dir_missing path={capture_dir}")
        return 1
    engine_dirs = [os.path.normpath(d if os.path.isabs(d) else os.path.join(REPO_ROOT, d)) for d in args.engine]
    reference = os.path.normpath(args.reference if os.path.isabs(args.reference) else os.path.join(REPO_ROOT, args.reference)) \
        if args.reference else None
    if args.regression:
        import Regression
        return Regression.run(sys.modules[__name__], capture_dir, reference, args.verify)
    if args.record_reference:
        import RefSplit
        ok, detail = RefSplit.write_reference(sys.modules[__name__], reference or capture_dir, args.record_reference)
        for line in ([] if ok else detail):
            print(f"REFERENCE_CHECK failure: {line}")
        print(f"REFERENCE_CHECK record_reference mode={args.record_reference} result={'pass' if ok else 'fail'} "
              f"signature={detail[0] if ok else 'none'}")
        return 0 if ok else 1
    if args.record_engine:
        import RefSplit
        game_exe = args.game_exe if os.path.isabs(args.game_exe) else os.path.join(REPO_ROOT, args.game_exe)
        ok, detail = RefSplit.write_engine_record(sys.modules[__name__], capture_dir, game_exe, True)
        print(f"REFERENCE_CHECK record_engine result={'pass' if ok else 'fail'} detail={detail}")
        return 0 if ok else 1
    if args.record_inputs:
        game_exe = args.game_exe
        if game_exe and not os.path.isabs(game_exe):
            game_exe = os.path.join(REPO_ROOT, game_exe)
        return record_inputs(capture_dir, args.kind, game_exe or None)
    if args.hdr:
        views = [v.strip() for v in args.views.split(",") if v.strip()]
        return run_hdr(capture_dir, views, args.verify)
    if args.display:
        return run_display(capture_dir, args.verify)
    if args.plate:
        return run_plate(capture_dir, args.verify)
    if args.aov:
        views = [v.strip() for v in args.views.split(",") if v.strip()]
        return run_aov(capture_dir, views, args.verify)
    if args.projection:
        views = [v.strip() for v in args.views.split(",") if v.strip()]
        return run_projection(capture_dir, views, args.verify)
    if args.atmos_dump:
        views = [v.strip() for v in args.views.split(",") if v.strip()]
        return run_atmos_dump(capture_dir, views, args.verify)
    if args.atmos_ref:
        views = [v.strip() for v in args.views.split(",") if v.strip()]
        return run_atmos_ref(capture_dir, views, args.verify)
    if args.blur_static:
        return run_blur_static(capture_dir, args.verify, engine_dirs or None, reference, expect)
    if args.blur_motion:
        return run_blur_motion(capture_dir, args.verify, engine_dirs or None, reference, expect)
    if args.gray:
        views = [v.strip() for v in args.views.split(",") if v.strip()]
        return run_gray(capture_dir, views, args.verify, engine_dirs or None, reference, expect)
    if args.albedo:
        views = [v.strip() for v in args.views.split(",") if v.strip()]
        return run_albedo(capture_dir, views, args.verify)
    if args.normal:
        views = [v.strip() for v in args.views.split(",") if v.strip()]
        return run_normal(capture_dir, views, args.verify)
    if args.lit:
        views = [v.strip() for v in args.views.split(",") if v.strip()]
        return run_lit(capture_dir, views, args.verify, engine_dirs or None, reference, expect, args.require_judged)
    if args.tonemap:
        views = [v.strip() for v in args.views.split(",") if v.strip()]
        lit_dir = args.lit_capture if os.path.isabs(args.lit_capture) else os.path.join(REPO_ROOT, args.lit_capture)
        return run_tonemap(capture_dir, os.path.normpath(lit_dir), views, args.verify)
    print("REFERENCE_CHECK result=fail reason=no_mode（--record-inputs か --hdr か --display か --plate か --aov か "
          "--projection か --atmos-dump か --atmos-ref か --blur-static か --blur-motion か --gray か --albedo か --normal か "
          "--lit か --tonemap か --grading を指定する）")
    return 1


if __name__ == "__main__":
    sys.exit(main())
