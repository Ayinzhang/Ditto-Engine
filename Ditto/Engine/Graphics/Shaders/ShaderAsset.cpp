#include "ShaderAsset.h"
#include "../../Core/Logger.h"
#include "../../Resources/AssetPath.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace fs = std::filesystem;

namespace Ditto
{
    static std::string ReadText(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    static std::string Trim(std::string s)
    {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

    static bool HasExtension(const std::string& name)
    {
        return fs::path(name).has_extension();
    }

    static std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    fs::path ResolveShaderPath(const std::string& shaderName, const fs::path& preferredRoot)
    {
        std::string name = shaderName.empty() ? "Lit_Toon" : shaderName;
        Logger::Get().Info("[ShaderAsset] Resolving shader: " + name);

        std::vector<std::string> candidates;
        candidates.push_back(name);
        if (!HasExtension(name))
        {
            candidates.push_back(name + ".shader");
            candidates.push_back(name + ".hlsl");
        }

        for (const std::string& candidate : candidates)
        {
            fs::path direct(candidate);
            if (direct.is_absolute() && fs::exists(direct))
            {
                Logger::Get().Info("[ShaderAsset] Found (absolute): " + direct.string());
                return direct;
            }

            fs::path resolved = AssetPath::ResolveAssetPath(candidate, preferredRoot);
            Logger::Get().Info("[ShaderAsset] Trying: " + resolved.string());
            if (fs::exists(resolved))
            {
                Logger::Get().Info("[ShaderAsset] Found (asset): " + resolved.string());
                return resolved;
            }

            resolved = AssetPath::ResolveTypedAssetPath(candidate, "Shaders", nullptr, preferredRoot);
            Logger::Get().Info("[ShaderAsset] Trying: " + resolved.string());
            if (fs::exists(resolved))
            {
                Logger::Get().Info("[ShaderAsset] Found (asset/Shaders): " + resolved.string());
                return resolved;
            }
        }

        fs::path fallback = AssetPath::ResolveTypedAssetPath(name, "Shaders", ".shader", preferredRoot);
        Logger::Get().Warning("[ShaderAsset] Not found, using fallback: " + fallback.string());
        return fallback;
    }

    static void ParseProperties(const std::string& source, ShaderAsset& asset)
    {
        size_t props = source.find("Properties");
        if (props == std::string::npos) return;
        size_t open = source.find('{', props);
        if (open == std::string::npos) return;

        int depth = 1;
        size_t close = open + 1;
        for (; close < source.size(); ++close)
        {
            if (source[close] == '{') ++depth;
            else if (source[close] == '}' && --depth == 0) break;
        }
        if (close >= source.size()) return;

        std::string block = source.substr(open + 1, close - open - 1);
        std::regex colorRe(R"SHADER(([_A-Za-z]\w*)\s*\(\s*"([^"]*)"\s*,\s*Color\s*\)\s*=\s*\(\s*([-+0-9.eE]+)\s*,\s*([-+0-9.eE]+)\s*,\s*([-+0-9.eE]+)\s*,\s*([-+0-9.eE]+)\s*\))SHADER");
        std::regex floatRe(R"SHADER(([_A-Za-z]\w*)\s*\(\s*"([^"]*)"\s*,\s*Float\s*\)\s*=\s*([-+0-9.eE]+))SHADER");
        std::regex rangeRe(R"SHADER(([_A-Za-z]\w*)\s*\(\s*"([^"]*)"\s*,\s*Range\s*\(\s*([-+0-9.eE]+)\s*,\s*([-+0-9.eE]+)\s*\)\s*\)\s*=\s*([-+0-9.eE]+))SHADER");
        std::regex textureRe(R"SHADER(([_A-Za-z]\w*)\s*\(\s*"([^"]*)"\s*,\s*2D\s*\)\s*=\s*"([^"]*)"\s*\{\s*\})SHADER");

        for (std::sregex_iterator it(block.begin(), block.end(), textureRe), end; it != end; ++it)
        {
            ShaderProperty p;
            p.name = (*it)[1].str();
            p.displayName = (*it)[2].str();
            p.type = ShaderPropertyType::Texture2D;
            p.textureDefault = (*it)[3].str();
            asset.properties.push_back(p);
        }
        for (std::sregex_iterator it(block.begin(), block.end(), colorRe), end; it != end; ++it)
        {
            ShaderProperty p;
            p.name = (*it)[1].str();
            p.displayName = (*it)[2].str();
            p.type = ShaderPropertyType::Color;
            p.colorDefault = glm::vec4(std::stof((*it)[3].str()), std::stof((*it)[4].str()),
                std::stof((*it)[5].str()), std::stof((*it)[6].str()));
            asset.properties.push_back(p);
        }
        for (std::sregex_iterator it(block.begin(), block.end(), rangeRe), end; it != end; ++it)
        {
            ShaderProperty p;
            p.name = (*it)[1].str();
            p.displayName = (*it)[2].str();
            p.type = ShaderPropertyType::Range;
            p.rangeMin = std::stof((*it)[3].str());
            p.rangeMax = std::stof((*it)[4].str());
            p.floatDefault = std::stof((*it)[5].str());
            asset.properties.push_back(p);
        }
        for (std::sregex_iterator it(block.begin(), block.end(), floatRe), end; it != end; ++it)
        {
            ShaderProperty p;
            p.name = (*it)[1].str();
            p.displayName = (*it)[2].str();
            p.type = ShaderPropertyType::Float;
            p.floatDefault = std::stof((*it)[3].str());
            asset.properties.push_back(p);
        }
    }

    static int UnityQueueBase(const std::string& name)
    {
        const std::string lower = ToLower(name);
        if (lower == "background") return 1000;
        if (lower == "geometry") return 2000;
        if (lower == "alphatest") return 2450;
        if (lower == "transparent") return 3000;
        if (lower == "overlay") return 4000;
        try { return std::stoi(name); }
        catch (...) { return 2000; }
    }

    static int ParseUnityQueue(const std::string& value)
    {
        std::regex queueRe(R"(^\s*([A-Za-z]+|[-+]?\d+)\s*([+-])?\s*(\d+)?\s*$)");
        std::smatch match;
        if (!std::regex_match(value, match, queueRe))
            return 2000;

        int queue = UnityQueueBase(match[1].str());
        if (match[2].matched && match[3].matched)
        {
            const int offset = std::stoi(match[3].str());
            queue += match[2].str() == "-" ? -offset : offset;
        }
        return queue;
    }

    static void ApplyRenderTypeDefaults(PipelineState& state)
    {
        const std::string type = ToLower(state.renderType);
        if (type == "transparent")
        {
            state.renderQueue = 3000;
            state.depthWrite = false;
            state.blend = true;
        }
        else if (type == "transparentcutout")
        {
            state.renderQueue = 2450;
            state.depthWrite = true;
            state.blend = false;
        }
        else
        {
            state.renderQueue = 2000;
            state.depthWrite = true;
            state.blend = false;
        }
    }

    static std::unordered_map<std::string, std::string> ParseTagsBlock(const std::string& source)
    {
        std::unordered_map<std::string, std::string> tags;
        size_t tagsPos = source.find("Tags");
        if (tagsPos == std::string::npos) return tags;
        size_t open = source.find('{', tagsPos);
        if (open == std::string::npos) return tags;
        size_t close = source.find('}', open + 1);
        if (close == std::string::npos) return tags;

        std::string block = source.substr(open + 1, close - open - 1);
        std::regex tagRe(R"TAG("([^"]+)"\s*=\s*"([^"]+)")TAG");
        for (std::sregex_iterator it(block.begin(), block.end(), tagRe), end; it != end; ++it)
            tags[(*it)[1].str()] = (*it)[2].str();
        return tags;
    }

    static void ParseRenderState(const std::string& source, ShaderAsset& asset)
    {
        PipelineState state;
        auto tags = ParseTagsBlock(source);
        auto renderTypeIt = tags.find("RenderType");
        if (renderTypeIt != tags.end())
        {
            state.renderType = renderTypeIt->second;
            ApplyRenderTypeDefaults(state);
        }

        auto queueIt = tags.find("Queue");
        if (queueIt != tags.end())
            state.renderQueue = ParseUnityQueue(queueIt->second);

        std::regex zwriteRe(R"(\bZWrite\s+(On|Off)\b)", std::regex_constants::icase);
        std::regex ztestRe(R"(\bZTest\s+(Less|LEqual|Always|Off)\b)", std::regex_constants::icase);
        std::regex blendRe(R"(\bBlend\s+(\w+)(?:\s+\w+)?\b)", std::regex_constants::icase);
        std::regex cullRe(R"(\bCull\s+(Off|Back|Front)\b)", std::regex_constants::icase);
        std::smatch match;

        if (std::regex_search(source, match, zwriteRe))
            state.depthWrite = ToLower(match[1].str()) == "on";
        if (std::regex_search(source, match, ztestRe))
        {
            const std::string ztest = ToLower(match[1].str());
            state.depthTest = ztest != "off";
            state.depthFunc = ztest == "lequal" ? DepthFunc::LessEqual : DepthFunc::Less;
        }
        if (std::regex_search(source, match, blendRe))
            state.blend = ToLower(match[1].str()) != "off";
        if (std::regex_search(source, match, cullRe))
        {
            const std::string cull = ToLower(match[1].str());
            state.cull = cull == "back" ? CullMode::Back : cull == "front" ? CullMode::Front : CullMode::Off;
        }

        asset.pipelineState = state;
    }

    static std::string ExtractProgramBlock(const std::string& source)
    {
        size_t begin = source.find("HLSLPROGRAM");
        size_t markerLen = 11;
        if (begin == std::string::npos)
        {
            begin = source.find("CGPROGRAM");
            markerLen = 9;
        }
        if (begin == std::string::npos) return {};

        begin += markerLen;
        size_t end = source.find("ENDHLSL", begin);
        if (end == std::string::npos) end = source.find("ENDCG", begin);
        if (end == std::string::npos) return {};
        return Trim(source.substr(begin, end - begin));
    }

    static std::string ExtractPragmaEntry(const std::string& program, const std::string& stage, const std::string& fallback)
    {
        std::regex pragmaRe("#\\s*pragma\\s+" + stage + "\\s+([_A-Za-z]\\w*)");
        std::smatch match;
        if (std::regex_search(program, match, pragmaRe))
            return match[1].str();
        return fallback;
    }

    static std::string StripEntryPragmas(const std::string& program)
    {
        std::stringstream in(program);
        std::stringstream out;
        std::string line;
        std::regex entryPragma(R"(^\s*#\s*pragma\s+(vertex|fragment)\s+[_A-Za-z]\w*\s*$)");
        while (std::getline(in, line))
        {
            if (std::regex_match(line, entryPragma))
                continue;
            out << line << "\n";
        }
        return out.str();
    }

    static std::string EnginePrelude()
    {
        return R"(
[[vk::binding(0, 0)]] cbuffer FrameUniforms : register(b0, space0)
{
    float4x4 view;
    float4x4 projection;
    float3   viewPos;        float _pad0;
    float3   lightColor;     float _pad1;
    float3   lightDir;       float lightIntensity;
    float4   time;
    float4   sinTime;
    float4   cosTime;
    float4   deltaTime;
    float4   screenParams;
};

[[vk::binding(0, 1)]] StructuredBuffer<float4x4> ModelMatrices  : register(t0, space1);
[[vk::binding(1, 1)]] StructuredBuffer<float4>   PropertyColors : register(t1, space1);
[[vk::binding(2, 1)]] Texture2D MainTex : register(t2, space1);
[[vk::binding(3, 1)]] SamplerState MainTexSampler : register(s3, space1);

#define fixed  float
#define fixed2 float2
#define fixed3 float3
#define fixed4 float4
#define half   float
#define half2  float2
#define half3  float3
#define half4  float4

#define MATRIX_V view
#define MATRIX_P projection
#define MATRIX_VP mul(projection, view)
#define MATRIX_M ObjectToWorld()
#define MATRIX_MV mul(MATRIX_V, MATRIX_M)
#define MATRIX_MVP mul(MATRIX_P, MATRIX_MV)
#define MATRIX_T_MV transpose(MATRIX_MV)
#define MATRIX_IT_MV MATRIX_MV
#define WorldSpaceCameraPos viewPos
#define LightColor0 float4(lightColor * lightIntensity, 1.0)
#define WorldSpaceLightDir0 float4(lightDir, 0.0)
#define Time time
#define SinTime sinTime
#define CosTime cosTime
#define DeltaTime deltaTime
#define ScreenParams screenParams
#define sampler2D Texture2D
#define _MainTex MainTex
#define _MainTexSampler MainTexSampler
#define sampler_MainTex MainTexSampler
#define tex2D(tex, uv) MainTex.Sample(MainTexSampler, uv)
#define SAMPLE_TEXTURE2D(tex, samplerTex, uv) tex.Sample(samplerTex, uv)
#define TRANSFORM_TEX(texcoord, name) (texcoord)

struct appdata_base
{
    float4 vertex : POSITION;
    float3 normal : NORMAL;
    uint __dittoInstanceID : SV_InstanceID;
};

struct appdata_tan
{
    float4 vertex : POSITION;
    float4 tangent : TANGENT;
    float3 normal : NORMAL;
    float4 texcoord : TEXCOORD0;
    uint __dittoInstanceID : SV_InstanceID;
};

struct appdata_full
{
    float4 vertex : POSITION;
    float4 tangent : TANGENT;
    float3 normal : NORMAL;
    float4 texcoord : TEXCOORD0;
    float4 texcoord1 : TEXCOORD1;
    float4 texcoord2 : TEXCOORD2;
    float4 texcoord3 : TEXCOORD3;
    fixed4 color : COLOR;
    uint __dittoInstanceID : SV_InstanceID;
};

struct appdata_img
{
    float4 vertex : POSITION;
    half2 texcoord : TEXCOORD0;
    uint __dittoInstanceID : SV_InstanceID;
};

float4x4 ObjectToWorldAt(uint instanceID)
{
    return ModelMatrices[instanceID];
}

float3 ObjectToWorldPosAt(float3 positionOS, uint instanceID)
{
    return mul(ObjectToWorldAt(instanceID), float4(positionOS, 1.0)).xyz;
}

float3 ObjectToWorldNormalAt(float3 normalOS, uint instanceID)
{
    return normalize(mul((float3x3)ObjectToWorldAt(instanceID), normalOS));
}

float4 WorldToClipPos(float3 positionWS)
{
    return mul(projection, mul(view, float4(positionWS, 1.0)));
}

float4 ObjectToClipPosAt(float3 positionOS, uint instanceID)
{
    return WorldToClipPos(ObjectToWorldPosAt(positionOS, instanceID));
}

float4 ObjectToClipPosAt(float4 positionOS, uint instanceID)
{
    return ObjectToClipPosAt(positionOS.xyz, instanceID);
}

float4 PropertyColorAt(uint instanceID)
{
    return PropertyColors[instanceID];
}

#define _Color PropertyColorAt(0)

float3 WorldSpaceViewDir(float3 worldPos)
{
    return normalize(viewPos - worldPos);
}

float3 WorldSpaceLightDir(float3 worldPos)
{
    return normalize(-lightDir);
}

float3 ObjectToViewPosAt(float3 positionOS, uint instanceID)
{
    return mul(view, float4(ObjectToWorldPosAt(positionOS, instanceID), 1.0)).xyz;
}

float3 ObjectToViewPosAt(float4 positionOS, uint instanceID)
{
    return ObjectToViewPosAt(positionOS.xyz, instanceID);
}

float3 ObjectSpaceViewDirAt(float4 positionOS, uint instanceID)
{
    return viewPos - ObjectToWorldPosAt(positionOS.xyz, instanceID);
}

float3 ObjectSpaceLightDirAt(float4 positionOS, uint instanceID)
{
    return WorldSpaceLightDir(ObjectToWorldPosAt(positionOS.xyz, instanceID));
}

float3 WorldSpaceViewDirUnnormalized(float3 worldPos)
{
    return viewPos - worldPos;
}

float4 ComputeScreenPos(float4 clipPos)
{
    float4 o = clipPos * 0.5;
    o.xy = float2(o.x, -o.y) + o.w;
    o.zw = clipPos.zw;
    return o;
}

float4 ComputeGrabScreenPos(float4 clipPos)
{
    return ComputeScreenPos(clipPos);
}

float Linear01Depth(float z)
{
    return z;
}

float LinearEyeDepth(float z)
{
    return z;
}

float3 LightingLambert(float3 normalWS, float3 lightDirWS, float3 lightColorRGB)
{
    return max(0.0, dot(normalize(normalWS), normalize(lightDirWS))) * lightColorRGB;
}

float3 LightingBlinnPhong(float3 worldPos, float3 normalWS, float3 lightDirWS, float3 lightColorRGB, float shininess)
{
    float3 n = normalize(normalWS);
    float3 l = normalize(lightDirWS);
    float3 v = WorldSpaceViewDir(worldPos);
    float3 h = normalize(l + v);
    float diffuse = max(0.0, dot(n, l));
    float specular = pow(max(0.0, dot(n, h)), shininess);
    return (diffuse + specular) * lightColorRGB;
}

float Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

float4 EncodeFloatRGBA(float v)
{
    float4 enc = frac(float4(1.0, 255.0, 65025.0, 160581375.0) * v);
    enc -= enc.yzww * float4(1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0);
    return enc;
}

float DecodeFloatRGBA(float4 enc)
{
    return dot(enc, float4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 1.0 / 160581375.0));
}

float2 EncodeFloatRG(float v)
{
    float2 enc = frac(float2(1.0, 255.0) * v);
    enc.x -= enc.y * (1.0 / 255.0);
    return enc;
}

float DecodeFloatRG(float2 enc)
{
    return dot(enc, float2(1.0, 1.0 / 255.0));
}

float2 EncodeViewNormalStereo(float3 n)
{
    return normalize(n).xy * 0.5 + 0.5;
}

float3 DecodeViewNormalStereo(float2 enc)
{
    float2 fenc = enc * 2.0 - 1.0;
    float z = sqrt(saturate(1.0 - dot(fenc, fenc)));
    return float3(fenc, z);
}

float2 ParallaxOffset(half h, half height, half3 viewDir)
{
    h = h * height - height / 2.0;
    return h * viewDir.xy / max(viewDir.z, 0.0001);
}
)";
    }

    static std::string GenerateDefaultProgram(const ShaderAsset& asset)
    {
        std::string shaderModel = "LitToon";
        return EnginePrelude() + R"(
struct appdata
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    uint instanceID : SV_InstanceID;
};

struct v2f
{
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normal   : NORMAL;
    float4 color    : COLOR;
};

v2f VSMain(appdata v)
{
    v2f o;
    o.worldPos = ObjectToWorldPosAt(v.position, v.instanceID);
    o.normal = ObjectToWorldNormalAt(v.normal, v.instanceID);
    o.position = WorldToClipPos(o.worldPos);
    o.color = PropertyColorAt(v.instanceID);
    return o;
}

float4 PSMain(v2f i) : SV_Target
{
    float3 lightDirWS = WorldSpaceLightDir(i.worldPos);
    float ndotl = max(0.0, dot(normalize(i.normal), lightDirWS));
    float toon = ndotl > 0.5 ? 1.0 : 0.45;
    return float4(i.color.rgb * LightColor0.rgb * toon, i.color.a);
}
)";
    }

    static std::string ExtractFunctionReturnType(const std::string& program, const std::string& functionName)
    {
        std::regex functionRe("([_A-Za-z]\\w*(?:\\s*<[^>]+>)?)\\s+" + functionName + "\\s*\\(");
        std::smatch match;
        if (std::regex_search(program, match, functionRe))
            return Trim(match[1].str());
        return {};
    }

    struct FunctionParam
    {
        std::string type;
        std::string name;
    };

    static FunctionParam ExtractFirstParameter(const std::string& program, const std::string& functionName)
    {
        std::regex functionRe("[_A-Za-z]\\w*(?:\\s*<[^>]+>)?\\s+" + functionName +
            R"(\s*\(\s*([_A-Za-z]\w*)\s+([_A-Za-z]\w*))");
        std::smatch match;
        if (std::regex_search(program, match, functionRe))
            return FunctionParam{ match[1].str(), match[2].str() };
        return {};
    }

    static std::string InjectHiddenInstanceID(std::string program, const std::string& appdataType)
    {
        if (appdataType.empty() || program.find("__dittoInstanceID") != std::string::npos)
            return program;

        std::regex structRe("struct\\s+" + appdataType + R"(\s*\{)");
        std::smatch match;
        if (!std::regex_search(program, match, structRe))
            return program;

        size_t openBrace = static_cast<size_t>(match.position(0) + match.length(0) - 1);
        int depth = 1;
        for (size_t i = openBrace + 1; i < program.size(); ++i)
        {
            if (program[i] == '{') ++depth;
            else if (program[i] == '}' && --depth == 0)
            {
                program.insert(i, "    uint __dittoInstanceID : SV_InstanceID;\n");
                return program;
            }
        }
        return program;
    }

    static bool StructContainsField(const std::string& program, const std::string& structName, const std::string& fieldName)
    {
        if (structName.empty()) return false;
        auto knownField = [&]() -> bool {
            if (fieldName == "vertex") return structName == "appdata_base" || structName == "appdata_tan" || structName == "appdata_full" || structName == "appdata_img";
            if (fieldName == "normal") return structName == "appdata_base" || structName == "appdata_tan" || structName == "appdata_full";
            if (fieldName == "texcoord") return structName == "appdata_tan" || structName == "appdata_full" || structName == "appdata_img";
            if (fieldName == "__dittoInstanceID") return structName == "appdata_base" || structName == "appdata_tan" || structName == "appdata_full" || structName == "appdata_img";
            return false;
        };
        std::regex structRe("struct\\s+" + structName + R"(\s*\{)");
        std::smatch match;
        if (!std::regex_search(program, match, structRe))
            return knownField();

        size_t openBrace = static_cast<size_t>(match.position(0) + match.length(0) - 1);
        int depth = 1;
        for (size_t i = openBrace + 1; i < program.size(); ++i)
        {
            if (program[i] == '{') ++depth;
            else if (program[i] == '}' && --depth == 0)
            {
                std::string body = program.substr(openBrace + 1, i - openBrace - 1);
                std::regex fieldRe("\\b" + fieldName + "\\b");
                return std::regex_search(body, fieldRe);
            }
        }
        return false;
    }

    static std::string BuildEngineHLSL(const std::string& source, const ShaderAsset& asset)
    {
        if (source.find("[[vk::binding") != std::string::npos)
            return source;

        std::string program = ExtractProgramBlock(source);
        if (program.empty())
            return GenerateDefaultProgram(asset);

        const std::string vertexEntry = ExtractPragmaEntry(program, "vertex", "vert");
        const std::string fragmentEntry = ExtractPragmaEntry(program, "fragment", "frag");
        program = StripEntryPragmas(program);

        bool hasVS = program.find("VSMain") != std::string::npos;
        bool hasPS = program.find("PSMain") != std::string::npos;
        bool hasVertexEntry = program.find(vertexEntry + "(") != std::string::npos;
        bool hasFragmentEntry = program.find(fragmentEntry + "(") != std::string::npos;
        std::string vertexReturnType = ExtractFunctionReturnType(program, vertexEntry);
        std::string fragmentReturnType = ExtractFunctionReturnType(program, fragmentEntry);
        FunctionParam vertexParam = ExtractFirstParameter(program, vertexEntry);

        if (!hasVS && hasVertexEntry)
            program = InjectHiddenInstanceID(program, vertexParam.type);

        std::string userHelperMacros;
        if (!hasVS && hasVertexEntry && !vertexParam.name.empty())
        {
            const std::string& v = vertexParam.name;
            userHelperMacros += "#define ObjectToWorld() ObjectToWorldAt(" + v + ".__dittoInstanceID)\n";
            userHelperMacros += "#define ObjectToWorldPos(positionOS) ObjectToWorldPosAt(positionOS, " + v + ".__dittoInstanceID)\n";
            userHelperMacros += "#define ObjectToWorldNormal(normalOS) ObjectToWorldNormalAt(normalOS, " + v + ".__dittoInstanceID)\n";
            userHelperMacros += "#define ObjectToClipPos(positionOS) ObjectToClipPosAt(positionOS, " + v + ".__dittoInstanceID)\n";
            userHelperMacros += "#define ObjectToViewPos(positionOS) ObjectToViewPosAt(positionOS, " + v + ".__dittoInstanceID)\n";
            userHelperMacros += "#define ObjectSpaceViewDir(positionOS) ObjectSpaceViewDirAt(positionOS, " + v + ".__dittoInstanceID)\n";
            userHelperMacros += "#define ObjectSpaceLightDir(positionOS) ObjectSpaceLightDirAt(positionOS, " + v + ".__dittoInstanceID)\n";
        }

        std::string hlsl = EnginePrelude() + "\n" + userHelperMacros + program + "\n";
        if (!hasVS)
        {
            if (hasVertexEntry && !vertexReturnType.empty())
            {
                std::string appdataType = vertexParam.type.empty() ? "appdata" : vertexParam.type;
                const bool hasVertex = StructContainsField(program, appdataType, "vertex");
                const bool hasNormal = StructContainsField(program, appdataType, "normal");
                const bool hasUv = StructContainsField(program, appdataType, "uv");
                const bool hasTexcoord = StructContainsField(program, appdataType, "texcoord");
                const bool hasHiddenInstance = StructContainsField(program, appdataType, "__dittoInstanceID");
                hlsl += "\n#undef ObjectToWorld\n#undef ObjectToWorldPos\n#undef ObjectToWorldNormal\n#undef ObjectToClipPos\n#undef ObjectToViewPos\n#undef ObjectSpaceViewDir\n#undef ObjectSpaceLightDir\n";
                hlsl += "\n" + vertexReturnType + R"( VSMain(float4 vertex : POSITION, float3 normal : NORMAL, float2 texcoord : TEXCOORD0, uint instanceID : SV_InstanceID)
{
    )" + appdataType + R"( v;
)";
                if (hasVertex)
                    hlsl += "    v.vertex = vertex;\n";
                if (hasNormal)
                    hlsl += "    v.normal = normal;\n";
                if (hasUv)
                    hlsl += "    v.uv = texcoord;\n";
                if (hasTexcoord)
                    hlsl += "    v.texcoord = float4(texcoord, 0.0, 0.0);\n";
                if (hasHiddenInstance)
                    hlsl += "    v.__dittoInstanceID = instanceID;\n";
                hlsl += R"(    return )" + vertexEntry + R"((v);
}
)";
            }
            else
            {
                hlsl += R"(
struct appdata
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    uint instanceID : SV_InstanceID;
};

struct v2f
{
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normal   : NORMAL;
    float4 color    : COLOR;
};

v2f VSMain(appdata v)
{
    v2f o;
    o.worldPos = ObjectToWorldPosAt(v.position, v.instanceID);
    o.normal = ObjectToWorldNormalAt(v.normal, v.instanceID);
    o.position = WorldToClipPos(o.worldPos);
    o.color = PropertyColorAt(v.instanceID);
    return o;
}
)";
            }
        }
        if (!hasPS && hasFragmentEntry && !fragmentReturnType.empty())
        {
            hlsl += "\n" + fragmentReturnType + " PSMain(" + vertexReturnType + R"( i) : SV_Target
{
    return )" + fragmentEntry + R"((i);
}
)";
        }
        return hlsl;
    }

    const ShaderProperty* ShaderAsset::FindProperty(const std::string& name) const
    {
        for (const ShaderProperty& p : properties)
            if (p.name == name) return &p;
        return nullptr;
    }

    bool ShaderAsset::HasColorProperty() const
    {
        for (const ShaderProperty& p : properties)
            if (p.type == ShaderPropertyType::Color)
                return true;
        return false;
    }

    bool ShaderAsset::HasTexture2DProperty() const
    {
        for (const ShaderProperty& p : properties)
            if (p.type == ShaderPropertyType::Texture2D)
                return true;
        return false;
    }

    ShaderAsset LoadShaderAsset(const std::string& shaderName, const fs::path& preferredRoot)
    {
        ShaderAsset asset;
        asset.shaderName = shaderName.empty() ? "Lit_Toon" : shaderName;
        fs::path path = ResolveShaderPath(asset.shaderName, preferredRoot);
        asset.sourcePath = path.string();
        if (!fs::exists(path))
        {
            asset.error = "Shader not found: " + asset.shaderName;
            Logger::Get().Error("[ShaderAsset] " + asset.error + " (" + path.string() + ")");
            return asset;
        }

        std::string source = ReadText(path);
        if (source.empty())
        {
            asset.error = "Shader file is empty: " + path.string();
            Logger::Get().Error("[ShaderAsset] " + asset.error);
            return asset;
        }

        ParseProperties(source, asset);
        ParseRenderState(source, asset);
        asset.engineHLSL = BuildEngineHLSL(source, asset);
        asset.ok = !asset.engineHLSL.empty();
        return asset;
    }
}
