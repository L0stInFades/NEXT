RWTexture3D<float4> VoxelAlbedo : register(u0, space0);
RWTexture3D<float4> VoxelNormal : register(u1, space0);
RWTexture3D<float4> VoxelEmission : register(u2, space0);

[numthreads(4, 4, 4)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint width = 0;
    uint height = 0;
    uint depth = 0;
    VoxelAlbedo.GetDimensions(width, height, depth);

    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height || dispatchThreadId.z >= depth) {
        return;
    }

    const float3 uvw = (float3(dispatchThreadId) + 0.5f) / float3(width, height, depth);
    const float horizon = saturate(1.0f - abs(uvw.y * 2.0f - 1.0f));
    const float altitude = saturate(uvw.y);
    const float3 albedo = lerp(float3(0.10f, 0.085f, 0.065f), float3(0.16f, 0.19f, 0.23f), altitude);
    const float3 normal = normalize(float3(uvw.x - 0.5f, 0.35f + altitude, uvw.z - 0.5f));
    const float3 emission = float3(0.010f, 0.014f, 0.020f) * altitude +
                            float3(0.020f, 0.015f, 0.010f) * horizon;

    VoxelAlbedo[dispatchThreadId] = float4(albedo, 1.0f);
    VoxelNormal[dispatchThreadId] = float4(normal * 0.5f + 0.5f, 1.0f);
    VoxelEmission[dispatchThreadId] = float4(emission, 1.0f);
}
