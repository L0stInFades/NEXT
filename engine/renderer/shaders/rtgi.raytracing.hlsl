RWTexture2D<float4> RTGIOutput : register(u0, space0);
RaytracingAccelerationStructure SceneTLAS : register(t0, space0);

cbuffer RTGIConstants : register(b0, space0) {
    uint FrameIndex;
    uint RaysPerPixel;
    float RayLength;
    float TemporalWeight;
    float3 CameraPosition;
    float CameraTanHalfFovY;
    float3 CameraForward;
    float CameraAspect;
    float3 CameraRight;
    float CameraPad0;
    float3 CameraUp;
    float CameraPad1;
};

struct RayPayload {
    float3 radiance;
    uint hit;
};

[shader("raygeneration")]
void RayGen() {
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 dimensions = DispatchRaysDimensions().xy;
    const float2 uv = (float2(pixel) + 0.5f) / max(float2(dimensions), float2(1.0f, 1.0f));

    const float2 ndc = uv * 2.0f - 1.0f;
    const float2 viewPlane = float2(
        ndc.x * CameraAspect * CameraTanHalfFovY,
        -ndc.y * CameraTanHalfFovY);
    RayDesc ray;
    ray.Origin = CameraPosition;
    ray.Direction = normalize(CameraForward + CameraRight * viewPlane.x + CameraUp * viewPlane.y);
    ray.TMin = 0.01f;
    ray.TMax = max(RayLength, 0.01f);

    RayPayload payload;
    payload.radiance = float3(0.0f, 0.0f, 0.0f);
    payload.hit = 0u;
    TraceRay(SceneTLAS, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);

    const float horizon = saturate(1.0f - uv.y);
    const float temporalPhase = frac((float)FrameIndex * 0.03125f);
    const float rayBudget = saturate((float)max(RaysPerPixel, 1u) / 4.0f);
    const float reach = saturate(RayLength / 50.0f);

    const float3 skyBounce = lerp(float3(0.018f, 0.025f, 0.034f), float3(0.040f, 0.050f, 0.065f), horizon);
    const float3 groundBounce = float3(0.024f, 0.021f, 0.017f) * (1.0f - horizon);
    const float dither = (frac(sin(dot(float2(pixel), float2(12.9898f, 78.233f)) + temporalPhase) * 43758.5453f) - 0.5f) * 0.002f;

    const float3 tracedBounce = payload.hit != 0u ? payload.radiance : float3(0.0f, 0.0f, 0.0f);
    const float3 indirect = max((skyBounce + groundBounce + tracedBounce) * lerp(0.65f, 1.0f, rayBudget) * reach + dither.xxx, 0.0f);
    RTGIOutput[pixel] = float4(lerp(indirect, indirect * 0.85f, saturate(TemporalWeight)), 1.0f);
}

[shader("miss")]
void Miss(inout RayPayload payload) {
    payload.radiance = float3(0.0f, 0.0f, 0.0f);
    payload.hit = 0u;
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attributes) {
    const float edgeFactor = saturate(min(min(attributes.barycentrics.x, attributes.barycentrics.y), 1.0f - attributes.barycentrics.x - attributes.barycentrics.y) * 8.0f);
    payload.radiance = lerp(float3(0.018f, 0.032f, 0.026f), float3(0.055f, 0.070f, 0.052f), edgeFactor);
    payload.hit = 1u;
}
