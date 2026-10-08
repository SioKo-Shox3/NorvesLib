// 太陽の VSM（--shadow-method=vsm）を読むパラメータの構造体。Common/VirtualShadowMap.glsl の評価が使う。
// uniform block のメンバとして宣言する側が、この include を先に取り込む。

#ifndef VIRTUAL_SHADOW_MAP_PARAMS_GLSL
#define VIRTUAL_SHADOW_MAP_PARAMS_GLSL

// std140。Rendering/VirtualShadowMapSample.h の GPUVsmSampleParams と同じ並び
struct VsmSampleParams
{
    vec4 lightRight;
    vec4 lightUp;
    // 光の進む向き。ライト空間の深度は dot(world, lightDirection)
    vec4 lightDirection;
    // xyz: カメラの位置、w: 影の最大の距離（m）
    vec4 cameraPosition;
    // x: 深度の原点（ライト空間の深度）、y: 1 / (2 × 深度の範囲)、z: 2 × 深度の範囲（m）、w: 予約（0）
    vec4 depth;
    // x: 画面上の 1 画素の大きさ / カメラからの距離（2 tan(fovY / 2) / 画面の高さ）、
    // y: 太陽の角半径の tan（物理の半影の半幅 = 受け手と遮る物の深度の差 × この値。CSM の DIRECTIONAL_LIGHT_TAN_ANGULAR_RADIUS と同じ）、
    // z: ブロッカーの探索と PCF の半径の上限（ワールドの長さ m。段に依らない。印付けの隣のページへの印の範囲もこれに合わせる）
    vec4 pixel;
    // xyz: カメラの前方（単位ベクトル）。影の距離の範囲・薄めは CSM と同じくこの前方への距離で測る
    vec4 view;
    // x: 影の最小の距離、y: 影の最大の距離、z: 奥の薄めの幅（m）。CSM の最初の分割・最後の分割・最後のカスケードの幅の 10% と同じ
    vec4 range;
    // x: 1 なら有効、y: スライス（段）の数、z: 物理ページの数
    uvec4 control;
    // 段を選ぶ距離のしきい値（太陽のクリップマップのもの。16 個。k 番目が [k / 4][k % 4]）。
    // ページの一辺・texel・範囲の原点などスライスごとの値は、スライスの表（Common/VirtualShadowMapSlice.glsl）にある
    vec4 thresholds[4];
};

#endif // VIRTUAL_SHADOW_MAP_PARAMS_GLSL
