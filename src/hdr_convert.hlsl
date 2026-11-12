// scRGB (linear Rec.709, 1.0 = 80 nits, rgba16f) to HDR10 (Rec.2020, ST.2084 PQ, rgb10a2): the
// game's HDR picture, as DLSS Frame Generation needs it (hdrswap.cpp). One full-screen triangle.

Texture2D<float4> src : register(t0);

float4 vs(uint id : SV_VertexID) : SV_Position {
    const float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float3 Pq(float3 y) {  // y: 1 = 10000 nits
    const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    const float3 p = pow(saturate(y), m1);
    return pow((c1 + c2 * p) / (1 + c3 * p), m2);
}

float4 ps(float4 pos : SV_Position) : SV_Target {
    const float3 c = src.Load(int3(pos.xy, 0)).rgb;
    const float3x3 toBt2020 = {0.6274040, 0.3292820, 0.0433136,
                               0.0690970, 0.9195400, 0.0113612,
                               0.0163916, 0.0880132, 0.8955950};
    const float3 bt2020 = max(mul(toBt2020, c), 0);  // also clears NaN
    return float4(Pq(bt2020 * (80.0 / 10000.0)), 1);
}
