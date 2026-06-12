// UI shader (HLSL). Screen-space instanced quads for the in-game UI
// (UIImage / UIText / UIButton). One source -> DXC -> SPIR-V (Vulkan) ->
// SPIRV-Cross -> GLSL (OpenGL), same pipeline as the scene shaders.
//
// Resource layout:
//   set 0 binding 0: frame-global uniform block (screenParams used for ortho)
//   set 0 binding 2/3: UI texture + sampler (white 1x1, image, or font atlas)
//   set 1 binding 0..2: per-quad instance data (rect / uvRect / color)
//
// Quad geometry is a unit quad (0,0)-(1,1); the rect instance data places it
// in pixels with a TOP-LEFT origin. NDC mapping y = 1 - 2*py/h keeps the UI
// upright both when rendering to the backbuffer (game mode) and to the
// editor's offscreen RT (displayed V-flipped).

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
    float4   screenParams;   // width, height, 1+1/w, 1+1/h
};

[[vk::binding(2, 0)]] Texture2D    UITexture : register(t0, space0);
[[vk::binding(3, 0)]] SamplerState UISampler : register(s0, space0);

[[vk::binding(0, 1)]] StructuredBuffer<float4> UIRects   : register(t0, space1); // x, y, w, h (pixels, top-left)
[[vk::binding(1, 1)]] StructuredBuffer<float4> UIUVRects : register(t1, space1); // u0, v0, u1, v1
[[vk::binding(2, 1)]] StructuredBuffer<float4> UIColors  : register(t2, space1);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR;
};

VSOutput VSMain(float2 aPos : POSITION, uint instanceID : SV_InstanceID)
{
    float4 rect   = UIRects[instanceID];
    float4 uvRect = UIUVRects[instanceID];

    float2 pixelPos = rect.xy + aPos * rect.zw;
    float2 screen = max(screenParams.xy, float2(1.0, 1.0));

    VSOutput o;
    o.position = float4(
        pixelPos.x / screen.x * 2.0 - 1.0,
        1.0 - pixelPos.y / screen.y * 2.0,
        0.0, 1.0);
    o.uv = lerp(uvRect.xy, uvRect.zw, aPos);
    o.color = UIColors[instanceID];
    return o;
}

float4 PSMain(VSOutput i) : SV_Target
{
    return UITexture.Sample(UISampler, i.uv) * i.color;
}
