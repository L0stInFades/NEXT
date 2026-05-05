struct MeshVertex {
    float4 position : SV_POSITION;
    float3 color : COLOR0;
};

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void main(out vertices MeshVertex vertices[3], out indices uint3 triangles[1]) {
    SetMeshOutputCounts(3, 1);

    vertices[0].position = float4(-0.55f, -0.45f, 0.0f, 1.0f);
    vertices[0].color = float3(0.15f, 0.82f, 0.58f);

    vertices[1].position = float4(0.0f, 0.55f, 0.0f, 1.0f);
    vertices[1].color = float3(1.0f, 0.64f, 0.18f);

    vertices[2].position = float4(0.55f, -0.45f, 0.0f, 1.0f);
    vertices[2].color = float3(0.35f, 0.55f, 1.0f);

    triangles[0] = uint3(0, 1, 2);
}
