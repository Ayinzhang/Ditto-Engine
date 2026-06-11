Shader "Ditto/Lit_Toon"
{
    Properties
    {
        _Color ("Color", Color) = (1, 1, 1, 1)
        _MainTex ("Main Texture", 2D) = "white" {}
    }

    SubShader
    {
        Tags { "RenderType" = "Opaque" }
        Pass
        {
            Lighting LitToon

            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag

            struct appdata
            {
                float4 vertex   : POSITION;
                float3 normal   : NORMAL;
                float2 uv       : TEXCOORD0;
            };

            struct v2f
            {
                float4 pos      : SV_Position;
                float3 worldPos : TEXCOORD0;
                float3 normal   : TEXCOORD1;
                float2 uv       : TEXCOORD2;
            };

            v2f vert(appdata v)
            {
                v2f o;
                o.pos = ObjectToClipPos(v.vertex);
                o.worldPos = mul(ObjectToWorld(), v.vertex).xyz;
                o.normal = ObjectToWorldNormal(v.normal);
                o.uv = v.uv;
                return o;
            }

            fixed4 frag(v2f i) : SV_Target
            {
                float3 normal = normalize(i.normal);
                float3 lightDir = WorldSpaceLightDir(i.worldPos);
                float ndotl = max(0.0, dot(normal, lightDir));
                float toon = ndotl > 0.5 ? 1.0 : 0.45;

                fixed4 albedo = tex2D(_MainTex, i.uv) * _Color;
                fixed3 lit = albedo.rgb * LightColor0.rgb * toon;
                return fixed4(lit, albedo.a);
            }
            ENDCG
        }
    }

    Fallback "Ditto/Lit_Toon"
}
