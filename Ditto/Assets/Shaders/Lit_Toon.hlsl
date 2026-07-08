










[[vk::binding(0, 0)]] cbuffer FrameUniforms : register(b0, space0)
{
    float4x4 view;
    float4x4 projection;
    float3   viewPos;        float _pad0;
    float3   lightColor;     float _pad1;
    float3   lightDir;       float lightIntensity;
};

[[vk::binding(0, 1)]] StructuredBuffer<float4x4> ModelMatrices  : register(t0, space1);
[[vk::binding(1, 1)]] StructuredBuffer<float4>   InstanceColors : register(t1, space1);
[[vk::binding(2, 1)]] Texture2D MainTex : register(t2, space1);
[[vk::binding(3, 1)]] SamplerState MainTexSampler : register(s3, space1);

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD1;
    float4 color    : COLOR;
};

VSOutput VSMain(float3 aPos : POSITION, float3 aNormal : NORMAL, float2 aUv : TEXCOORD0, uint instanceID : SV_InstanceID)
{
    float4x4 model = ModelMatrices[instanceID];

    VSOutput o;
    float4 world = mul(model, float4(aPos, 1.0));
    o.position = mul(projection, mul(view, world));
    o.worldPos = world.xyz;
    o.normal   = normalize(mul((float3x3)model, aNormal));
    o.uv        = aUv;
    o.color    = InstanceColors[instanceID];
    return o;
}

float4 PSMain(VSOutput i) : SV_Target
{
    const float ambientStrength  = 0.3;
    const float diffuseStrength  = 0.5;
    const float specularStrength = 0.2;
    const float shininess        = 32.0;

    float3 ambient = ambientStrength * lightColor;
    float3 diffuse = diffuseStrength * max(0.0, dot(i.normal, -lightDir)) * lightColor;

    float3 viewDir    = normalize(viewPos - i.worldPos);
    float3 reflectDir = reflect(lightDir, i.normal);
    float  spec       = pow(max(0.0, dot(viewDir, reflectDir)), shininess);
    float3 specular   = specularStrength * spec * lightColor;

    float3 lighting = (ambient + diffuse + specular) * lightIntensity;
    float4 albedo = MainTex.Sample(MainTexSampler, i.uv) * i.color;
    return float4(lighting * albedo.rgb, albedo.a);
}
