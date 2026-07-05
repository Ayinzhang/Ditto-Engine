Shader "Ditto/Test/UnlitSmoke"
{
    Properties
    {
        _Color ("Color", Color) = (1, 0.05, 0.02, 1)
    }
    SubShader
    {
        Tags { "RenderType" = "Opaque" "Queue" = "Geometry" }
        Pass
        {
            CGPROGRAM
            struct appdata { float4 vertex : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
            struct v2f { float4 pos : SV_Position; };
            v2f vert(appdata v) { v2f o; o.pos = ObjectToClipPos(v.vertex); return o; }
            fixed4 frag(v2f i) : SV_Target { return _Color; }
            ENDCG
        }
    }
}
