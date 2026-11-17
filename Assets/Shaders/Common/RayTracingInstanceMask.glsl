// レイトレーシングシーンのinstance mask。FramePacket.hのRayTracingInstanceMask*と一致させる。
#ifndef RAY_TRACING_INSTANCE_MASK_GLSL
#define RAY_TRACING_INSTANCE_MASK_GLSL

// 影を落とす不透明物体。DDGI・RTGIの光線はこのbitを調べ、RTの影の光線は0x04も調べる。
const uint RayTracingInstanceMaskShadowCaster = 0x01u;
// 影を落とさない不透明物体。全bitで調べるパストレーサーと、シーンが頼んだとき（SetRTGINonShadowCasterOcclusion）の
// RTGIの拡散の光線だけが当たる。
const uint RayTracingInstanceMaskNonShadowCaster = 0x02u;
// 空の遮蔽のマップが受け持つ樹冠（影を落とす物）。RTの影の光線（0x01|0x04）と全bitで調べるパストレーサーだけが当たり、
// DDGI・RTGIの光線（0x01、拡散の0x03）には当たらない。0x02を立てないのは、
// SetRTGINonShadowCasterOcclusionの拡散の光線（0x03）にも当てないため。
const uint RayTracingInstanceMaskSkyMapProxy = 0x04u;

#endif
