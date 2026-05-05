struct VSOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID) {
    float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f)
    };

    float2 uvs[3] = {
        float2(0.0f, 1.0f),
        float2(0.0f, -1.0f),
        float2(2.0f, 1.0f)
    };

    VSOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.texcoord = uvs[vertexId];
    return output;
}
