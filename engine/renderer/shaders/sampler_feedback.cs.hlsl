Texture2D<float4> SourceTexture : register(t0, space0);
FeedbackTexture2D<SAMPLER_FEEDBACK_MIN_MIP> FeedbackMap : register(u0, space0);
SamplerState LinearClampSampler : register(s0, space0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint width = 0;
    uint height = 0;
    SourceTexture.GetDimensions(width, height);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height) {
        return;
    }

    const float2 dimensions = max(float2((float)width, (float)height), float2(1.0f, 1.0f));
    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / dimensions;
    FeedbackMap.WriteSamplerFeedbackLevel(SourceTexture, LinearClampSampler, uv, 0.0f);
}
