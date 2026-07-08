













[[vk::binding(0, 0)]] cbuffer FrameUniforms : register(b0, space0)
{
    float4x4 view;
    float4x4 projection;
    float3   viewPos;        float _pad0;
    float3   lightColor;     float _pad1;
    float3   lightDir;       float lightIntensity;
    float4   timeVec;
    float4   sinTime;
    float4   cosTime;
    float4   deltaTime;
    float4   screenParams;   
};

[[vk::binding(2, 1)]] Texture2D    UITexture : register(t2, space1);
[[vk::binding(3, 1)]] SamplerState UISampler : register(s3, space1);

[[vk::binding(0, 1)]] StructuredBuffer<float4> UIRects   : register(t0, space1); 
struct UIExtra
{
    float4 uvRect;
    float4 color;
};
[[vk::binding(1, 1)]] StructuredBuffer<UIExtra> UIExtras : register(t1, space1);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR;
};

VSOutput VSMain(float2 aPos : POSITION, uint instanceID : SV_InstanceID)
{
    float4 rect   = UIRects[instanceID];
    UIExtra extra = UIExtras[instanceID];

    float2 pixelPos = rect.xy + aPos * rect.zw;
    float2 screen = max(screenParams.xy, float2(1.0, 1.0));

    VSOutput o;
    o.position = float4(
        pixelPos.x / screen.x * 2.0 - 1.0,
        1.0 - pixelPos.y / screen.y * 2.0,
        0.0, 1.0);
    o.uv = lerp(extra.uvRect.xy, extra.uvRect.zw, aPos);
    o.color = extra.color;
    return o;
}

float4 PSMain(VSOutput i) : SV_Target
{
    return UITexture.Sample(UISampler, i.uv) * i.color;
}
