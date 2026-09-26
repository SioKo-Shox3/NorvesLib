// レイトレーシングシーンのinstance mask。FramePacket.hのRayTracingInstanceMask*と一致させる。
#ifndef RAY_TRACING_INSTANCE_MASK_GLSL
#define RAY_TRACING_INSTANCE_MASK_GLSL

// 影を落とす不透明物体。影・DDGI・RTGIの光線はこのbitだけを調べる。
const uint RayTracingInstanceMaskShadowCaster = 0x01u;
// 影を落とさない不透明物体。全bitで調べるパストレーサーだけが当たる。
const uint RayTracingInstanceMaskNonShadowCaster = 0x02u;

#endif
