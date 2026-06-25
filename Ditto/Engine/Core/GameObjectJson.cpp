#include "GameObjectJson.h"

#include "CSharpScript.h"
#include "GameObject.h"
#include "../Animation/AnimatorComponent.h"
#include "../Graphics/ParticleSystemComponent.h"
#include "../Graphics/Camera.h"
#include "../Resources/AssetDatabase.h"
#include "../Resources/AssetPath.h"

#include <memory>
#include <string>
#include <variant>

namespace Ditto::GameObjectJson
{
    namespace
    {
        namespace CI = ComponentIndex;

        using Json::Value;
        using Object = Value::Object;
        using Array = Value::Array;

        Value Vec2(const glm::vec2& v) { return Array{ v.x, v.y }; }
        Value Vec3(const glm::vec3& v) { return Array{ v.x, v.y, v.z }; }
        Value Vec4(const glm::vec4& v) { return Array{ v.x, v.y, v.z, v.w }; }

        glm::vec2 ReadVec2(const Value* value, const glm::vec2& fallback = glm::vec2(0.0f))
        {
            if (!value || !value->IsArray()) return fallback;
            const Array& a = value->AsArray();
            if (a.size() < 2) return fallback;
            return glm::vec2(a[0].Float(fallback.x), a[1].Float(fallback.y));
        }

        glm::vec3 ReadVec3(const Value* value, const glm::vec3& fallback = glm::vec3(0.0f))
        {
            if (!value || !value->IsArray()) return fallback;
            const Array& a = value->AsArray();
            if (a.size() < 3) return fallback;
            return glm::vec3(a[0].Float(fallback.x), a[1].Float(fallback.y), a[2].Float(fallback.z));
        }

        glm::vec4 ReadVec4(const Value* value, const glm::vec4& fallback = glm::vec4(0.0f))
        {
            if (!value || !value->IsArray()) return fallback;
            const Array& a = value->AsArray();
            if (a.size() < 4) return fallback;
            return glm::vec4(a[0].Float(fallback.x), a[1].Float(fallback.y),
                a[2].Float(fallback.z), a[3].Float(fallback.w));
        }

        Value AssetReference(const std::string& path)
        {
            Object json;
            json["path"] = Ditto::AssetPath::NormalizeAssetKey(path);
            json["guid"] = Ditto::AssetDatabase::Get().GuidForPath(path);
            return json;
        }

        std::string ReadAssetReference(const Value* value, const std::string& fallback = {})
        {
            if (!value) return fallback;
            if (value->IsString())
                return Ditto::AssetPath::NormalizeAssetKey(value->String(fallback));
            if (!value->IsObject()) return fallback;

            std::string path = value->Find("path") ? value->Find("path")->String(fallback) : fallback;
            path = Ditto::AssetPath::NormalizeAssetKey(path);

            std::string guid = value->Find("guid") ? value->Find("guid")->String() : std::string();
            if (!guid.empty())
            {
                std::string resolved = Ditto::AssetDatabase::Get().RelativePathForGuid(guid);
                if (!resolved.empty()) return resolved;
            }
            return path;
        }

        template<typename T>
        T EnumValue(const Value* value, T fallback)
        {
            return static_cast<T>(value ? value->Int(static_cast<int>(fallback)) : static_cast<int>(fallback));
        }

        const char* ComponentTypeName(int index)
        {
            switch (index)
            {
            case CI::Transform: return "Transform";
            case CI::Light: return "Light";
            case CI::Camera: return "Camera";
            case CI::Renderer: return "Renderer";
            case CI::SpriteRenderer: return "SpriteRenderer";
            case CI::Rigidbody: return "Rigidbody";
            case CI::Collider: return "Collider";
            case CI::AudioSource: return "AudioSource";
            case CI::UIImage: return "UIImage";
            case CI::UIText: return "UIText";
            case CI::UIButton: return "UIButton";
            case CI::CSharpScript: return "CSharpScript";
            case CI::Rigidbody2D: return "Rigidbody2D";
            case CI::Collider2D: return "Collider2D";
            case CI::Canvas: return "Canvas";
            case CI::RectTransform: return "RectTransform";
            case CI::Animator: return "Animator";
            case CI::ParticleSystem: return "ParticleSystem";
            default: return "Unknown";
            }
        }

        std::unique_ptr<Component> CreateComponent(const std::string& type)
        {
            if (type == "Transform") return std::make_unique<TransformComponent>();
            if (type == "Light") return std::make_unique<LightComponent>();
            if (type == "Camera") return std::make_unique<CameraComponent>();
            if (type == "Renderer") return std::make_unique<RendererComponent>();
            if (type == "SpriteRenderer") return std::make_unique<SpriteRendererComponent>();
            if (type == "Rigidbody") return std::make_unique<RigidbodyComponent>();
            if (type == "Collider") return std::make_unique<ColliderComponent>();
            if (type == "AudioSource") return std::make_unique<AudioSourceComponent>();
            if (type == "UIImage") return std::make_unique<UIImageComponent>();
            if (type == "UIText") return std::make_unique<UITextComponent>();
            if (type == "UIButton") return std::make_unique<UIButtonComponent>();
            if (type == "CSharpScript") return std::make_unique<CSharpScriptComponent>();
            if (type == "Rigidbody2D") return std::make_unique<Rigidbody2DComponent>();
            if (type == "Collider2D") return std::make_unique<Collider2DComponent>();
            if (type == "Canvas") return std::make_unique<CanvasComponent>();
            if (type == "RectTransform") return std::make_unique<RectTransformComponent>();
            if (type == "Animator") return std::make_unique<AnimatorComponent>();
            if (type == "ParticleSystem") return std::make_unique<ParticleSystemComponent>();
            return nullptr;
        }

        Value ScriptFieldToJson(const ScriptField& field)
        {
            Object json;
            json["name"] = field.name;
            json["type"] = static_cast<int>(field.type);
            switch (field.type)
            {
            case ScriptFieldType::Float: json["value"] = std::get<float>(field.value); break;
            case ScriptFieldType::Int: json["value"] = std::get<int>(field.value); break;
            case ScriptFieldType::Bool: json["value"] = std::get<bool>(field.value); break;
            case ScriptFieldType::String: json["value"] = std::get<std::string>(field.value); break;
            case ScriptFieldType::Vector2: json["value"] = Vec2(std::get<glm::vec2>(field.value)); break;
            case ScriptFieldType::Vector3: json["value"] = Vec3(std::get<glm::vec3>(field.value)); break;
            case ScriptFieldType::Vector4: json["value"] = Vec4(std::get<glm::vec4>(field.value)); break;
            }
            return json;
        }

        ScriptField ScriptFieldFromJson(const Value& value)
        {
            ScriptFieldType type = EnumValue(value.Find("type"), ScriptFieldType::Float);
            ScriptField field(value.Find("name") ? value.Find("name")->String() : std::string(), type);
            const Value* fieldValue = value.Find("value");
            switch (type)
            {
            case ScriptFieldType::Float: field.value = field.defaultValue = fieldValue ? fieldValue->Float() : 0.0f; break;
            case ScriptFieldType::Int: field.value = field.defaultValue = fieldValue ? fieldValue->Int() : 0; break;
            case ScriptFieldType::Bool: field.value = field.defaultValue = fieldValue ? fieldValue->Bool() : false; break;
            case ScriptFieldType::String: field.value = field.defaultValue = fieldValue ? fieldValue->String() : std::string(); break;
            case ScriptFieldType::Vector2: field.value = field.defaultValue = ReadVec2(fieldValue); break;
            case ScriptFieldType::Vector3: field.value = field.defaultValue = ReadVec3(fieldValue); break;
            case ScriptFieldType::Vector4: field.value = field.defaultValue = ReadVec4(fieldValue); break;
            }
            return field;
        }

        Value ComponentToJson(const Component& component)
        {
            Object json;
            json["type"] = ComponentTypeName(component.index);
            json["enabled"] = component.enabled;

            if (const auto* c = dynamic_cast<const TransformComponent*>(&component))
            {
                json["position"] = Vec3(c->position);
                json["rotation"] = Vec3(c->rotation);
                json["scale"] = Vec3(c->scale);
            }
            else if (const auto* c = dynamic_cast<const LightComponent*>(&component))
            {
                json["lightType"] = static_cast<int>(c->type);
                json["color"] = Vec3(c->color);
                json["intensity"] = c->intensity;
                json["range"] = c->range;
                json["spotAngle"] = c->spotAngle;
                json["indirectMultiplier"] = c->indirectMultiplier;
                json["castShadows"] = c->castShadows;
            }
            else if (const auto* c = dynamic_cast<const CameraComponent*>(&component))
            {
                json["mainCamera"] = c->mainCamera;
                json["clearFlags"] = static_cast<int>(c->clearFlags);
                json["projectionType"] = static_cast<int>(c->projectionType);
                json["fieldOfView"] = c->fieldOfView;
                json["orthographicSize"] = c->orthographicSize;
                json["nearClipPlane"] = c->nearClipPlane;
                json["farClipPlane"] = c->farClipPlane;
                json["backgroundColor"] = Vec4(c->backgroundColor);
                json["viewportRect"] = Vec4(c->viewportRect);
                json["depth"] = c->depth;
                json["occlusionCulling"] = c->occlusionCulling;
                json["allowHDR"] = c->allowHDR;
                json["allowMSAA"] = c->allowMSAA;
            }
            else if (const auto* c = dynamic_cast<const RendererComponent*>(&component))
            {
                json["color"] = Vec4(c->color);
                json["meshPath"] = AssetReference(c->meshPath);
                json["mainTexturePath"] = AssetReference(c->mainTexturePath);
                json["materialPath"] = AssetReference(c->materialPath);
                json["shadowCastingMode"] = static_cast<int>(c->shadowCastingMode);
                json["receiveShadows"] = c->receiveShadows;
                json["staticShadowCaster"] = c->staticShadowCaster;
                json["contributeGI"] = c->contributeGI;
                json["lightProbeUsage"] = c->lightProbeUsage;
                json["reflectionProbeUsage"] = c->reflectionProbeUsage;
                json["motionVectors"] = c->motionVectors;
                json["dynamicOcclusion"] = c->dynamicOcclusion;
                json["renderingLayerMask"] = static_cast<int>(c->renderingLayerMask);
            }
            else if (const auto* c = dynamic_cast<const SpriteRendererComponent*>(&component))
            {
                json["color"] = Vec4(c->color);
                json["spritePath"] = AssetReference(c->spritePath);
                json["materialPath"] = AssetReference(c->materialPath);
                json["flipX"] = c->flipX;
                json["flipY"] = c->flipY;
                json["drawMode"] = static_cast<int>(c->drawMode);
                json["size"] = Vec2(c->size);
                json["maskInteraction"] = static_cast<int>(c->maskInteraction);
                json["sortingLayer"] = c->sortingLayer;
                json["sortingOrder"] = c->sortingOrder;
                json["spriteSortPoint"] = static_cast<int>(c->spriteSortPoint);
            }
            else if (const auto* c = dynamic_cast<const RigidbodyComponent*>(&component))
            {
                json["bodyType"] = static_cast<int>(c->type);
                json["mass"] = c->mass;
                json["useGravity"] = c->useGravity;
                json["linearDamping"] = c->damp;
                json["angularDamping"] = c->angularDamp;
                json["isKinematic"] = c->isKinematic;
                json["interpolate"] = c->interpolate;
                json["collisionDetection"] = c->collisionDetection;
                json["freezePosition"] = Array{ c->freezePosition[0], c->freezePosition[1], c->freezePosition[2] };
                json["freezeRotation"] = Array{ c->freezeRotation[0], c->freezeRotation[1], c->freezeRotation[2] };
            }
            else if (const auto* c = dynamic_cast<const ColliderComponent*>(&component))
            {
                json["colliderType"] = static_cast<int>(c->type);
                json["isTrigger"] = c->isTrigger;
                json["providesContacts"] = c->providesContacts;
                json["biasPosition"] = Vec3(c->biasPosition);
                json["biasRotation"] = Vec3(c->biasRotation);
                json["biasScale"] = Vec3(c->biasScale);
                json["meshPath"] = AssetReference(c->meshPath);
            }
            else if (const auto* c = dynamic_cast<const Rigidbody2DComponent*>(&component))
            {
                json["bodyType"] = static_cast<int>(c->type);
                json["materialPath"] = AssetReference(c->materialPath);
                json["simulated"] = c->simulated;
                json["useAutoMass"] = c->useAutoMass;
                json["mass"] = c->mass;
                json["useGravity"] = c->useGravity;
                json["gravityScale"] = c->gravityScale;
                json["linearDamping"] = c->linearDamping;
                json["angularDamping"] = c->angularDamping;
                json["collisionDetection"] = c->collisionDetection;
                json["sleepingMode"] = c->sleepingMode;
                json["interpolate"] = c->interpolate;
                json["freezePositionX"] = c->freezePositionX;
                json["freezePositionY"] = c->freezePositionY;
                json["freezeRotation"] = c->freezeRotation;
                json["velocity"] = Vec2(c->velocity);
                json["angularVelocity"] = c->angularVelocity;
            }
            else if (const auto* c = dynamic_cast<const Collider2DComponent*>(&component))
            {
                json["colliderType"] = static_cast<int>(c->type);
                json["isTrigger"] = c->isTrigger;
                json["usedByEffector"] = c->usedByEffector;
                json["usedByComposite"] = c->usedByComposite;
                json["offset"] = Vec2(c->offset);
                json["size"] = Vec2(c->size);
                json["radius"] = c->radius;
                json["restitution"] = c->restitution;
                json["friction"] = c->friction;
            }
            else if (const auto* c = dynamic_cast<const AudioSourceComponent*>(&component))
            {
                json["clipPath"] = AssetReference(c->clipPath);
                json["outputPath"] = c->outputPath;
                json["mute"] = c->mute;
                json["bypassEffects"] = c->bypassEffects;
                json["bypassListenerEffects"] = c->bypassListenerEffects;
                json["bypassReverbZones"] = c->bypassReverbZones;
                json["volume"] = c->volume;
                json["pitch"] = c->pitch;
                json["loop"] = c->loop;
                json["playOnAwake"] = c->playOnAwake;
                json["priority"] = c->priority;
                json["stereoPan"] = c->stereoPan;
                json["spatialBlend"] = c->spatialBlend;
                json["reverbZoneMix"] = c->reverbZoneMix;
            }
            else if (const auto* c = dynamic_cast<const CanvasComponent*>(&component))
            {
                json["renderMode"] = static_cast<int>(c->renderMode);
                json["pixelPerfect"] = c->pixelPerfect;
                json["planeDistance"] = c->planeDistance;
                json["sortingOrder"] = c->sortingOrder;
            }
            else if (const auto* c = dynamic_cast<const RectTransformComponent*>(&component))
            {
                json["anchor"] = static_cast<int>(c->anchor);
                json["anchoredPosition"] = Vec2(c->anchoredPosition);
                json["sizeDelta"] = Vec2(c->sizeDelta);
                json["pivot"] = Vec2(c->pivot);
            }
            else if (const auto* c = dynamic_cast<const UIImageComponent*>(&component))
            {
                json["anchor"] = static_cast<int>(c->anchor);
                json["offset"] = Vec2(c->offset);
                json["size"] = Vec2(c->size);
                json["color"] = Vec4(c->color);
                json["texturePath"] = AssetReference(c->texturePath);
                json["imageType"] = static_cast<int>(c->type);
                json["raycastTarget"] = c->raycastTarget;
                json["maskable"] = c->maskable;
            }
            else if (const auto* c = dynamic_cast<const UITextComponent*>(&component))
            {
                json["anchor"] = static_cast<int>(c->anchor);
                json["offset"] = Vec2(c->offset);
                json["fontSize"] = c->fontSize;
                json["color"] = Vec4(c->color);
                json["text"] = c->text;
                json["fontPath"] = AssetReference(c->fontPath);
                json["fontStyle"] = c->fontStyle;
                json["alignment"] = c->alignment;
                json["raycastTarget"] = c->raycastTarget;
                json["maskable"] = c->maskable;
            }
            else if (const auto* c = dynamic_cast<const UIButtonComponent*>(&component))
            {
                json["anchor"] = static_cast<int>(c->anchor);
                json["offset"] = Vec2(c->offset);
                json["size"] = Vec2(c->size);
                json["color"] = Vec4(c->color);
                json["hoverColor"] = Vec4(c->hoverColor);
                json["pressedColor"] = Vec4(c->pressedColor);
                json["label"] = c->label;
                json["fontSize"] = c->fontSize;
                json["labelColor"] = Vec4(c->labelColor);
                json["interactable"] = c->interactable;
                json["transition"] = c->transition;
                json["disabledColor"] = Vec4(c->disabledColor);
                json["colorMultiplier"] = c->colorMultiplier;
                json["fadeDuration"] = c->fadeDuration;
            }
            else if (const auto* c = dynamic_cast<const CSharpScriptComponent*>(&component))
            {
                json["scriptName"] = c->scriptName;
                json["scriptPath"] = AssetReference(c->scriptPath);
                Array fields;
                for (const ScriptField& field : c->fields)
                    fields.push_back(ScriptFieldToJson(field));
                json["fields"] = std::move(fields);
            }
            else if (const auto* c = dynamic_cast<const AnimatorComponent*>(&component))
            {
                json["defaultClip"] = c->defaultClip;
                json["playOnAwake"] = c->playOnAwake;
                json["playbackSpeed"] = c->playbackSpeed;
                Array clips;
                for (const auto& [name, clip] : c->clips)
                {
                    Object clipJson;
                    clipJson["name"] = clip.name;
                    clipJson["length"] = clip.length;
                    clipJson["loop"] = clip.loop;
                    Array keyframes;
                    for (const AnimationKeyframe& keyframe : clip.keyframes)
                    {
                        Object keyframeJson;
                        keyframeJson["time"] = keyframe.time;
                        keyframeJson["position"] = Vec3(keyframe.position);
                        keyframeJson["rotation"] = Vec3(keyframe.rotation);
                        keyframeJson["scale"] = Vec3(keyframe.scale);
                        keyframes.push_back(std::move(keyframeJson));
                    }
                    clipJson["keyframes"] = std::move(keyframes);
                    clips.push_back(std::move(clipJson));
                }
                json["clips"] = std::move(clips);
            }
            else if (const auto* c = dynamic_cast<const ParticleSystemComponent*>(&component))
            {
                json["maxParticles"] = c->maxParticles;
                json["emissionRate"] = c->emissionRate;
                json["looping"] = c->looping;
                json["duration"] = c->duration;
                json["playOnAwake"] = c->playOnAwake;
                json["startLifetime"] = c->startLifetime;
                json["startSpeed"] = c->startSpeed;
                json["startColor"] = Vec4(c->startColor);
                json["endColor"] = Vec4(c->endColor);
                json["startSize"] = c->startSize;
                json["endSize"] = c->endSize;
                json["materialPath"] = AssetReference(c->materialPath);
                json["gravity"] = Vec3(c->gravity);
                json["shape"] = static_cast<int>(c->shape);
                json["coneAngle"] = c->coneAngle;
                json["sphereRadius"] = c->sphereRadius;
            }
            return json;
        }

        void ComponentFromJson(Component& component, const Value& value)
        {
            component.enabled = value.Find("enabled") ? value.Find("enabled")->Bool(component.enabled) : component.enabled;

            if (auto* c = dynamic_cast<TransformComponent*>(&component))
            {
                c->SetTRS(ReadVec3(value.Find("position"), c->position),
                    ReadVec3(value.Find("rotation"), c->rotation),
                    ReadVec3(value.Find("scale"), c->scale));
            }
            else if (auto* c = dynamic_cast<LightComponent*>(&component))
            {
                c->type = EnumValue(value.Find("lightType"), c->type);
                c->color = ReadVec3(value.Find("color"), c->color);
                c->intensity = value.Find("intensity") ? value.Find("intensity")->Float(c->intensity) : c->intensity;
                c->range = value.Find("range") ? value.Find("range")->Float(c->range) : c->range;
                c->spotAngle = value.Find("spotAngle") ? value.Find("spotAngle")->Float(c->spotAngle) : c->spotAngle;
                c->indirectMultiplier = value.Find("indirectMultiplier") ? value.Find("indirectMultiplier")->Float(c->indirectMultiplier) : c->indirectMultiplier;
                c->castShadows = value.Find("castShadows") ? value.Find("castShadows")->Bool(c->castShadows) : c->castShadows;
            }
            else if (auto* c = dynamic_cast<CameraComponent*>(&component))
            {
                c->mainCamera = value.Find("mainCamera") ? value.Find("mainCamera")->Bool(c->mainCamera) : c->mainCamera;
                c->clearFlags = EnumValue(value.Find("clearFlags"), c->clearFlags);
                c->projectionType = EnumValue(value.Find("projectionType"), c->projectionType);
                c->fieldOfView = value.Find("fieldOfView") ? value.Find("fieldOfView")->Float(c->fieldOfView) : c->fieldOfView;
                c->orthographicSize = value.Find("orthographicSize") ? value.Find("orthographicSize")->Float(c->orthographicSize) : c->orthographicSize;
                c->nearClipPlane = value.Find("nearClipPlane") ? value.Find("nearClipPlane")->Float(c->nearClipPlane) : c->nearClipPlane;
                c->farClipPlane = value.Find("farClipPlane") ? value.Find("farClipPlane")->Float(c->farClipPlane) : c->farClipPlane;
                c->backgroundColor = ReadVec4(value.Find("backgroundColor"), c->backgroundColor);
                c->viewportRect = ReadVec4(value.Find("viewportRect"), c->viewportRect);
                c->depth = value.Find("depth") ? value.Find("depth")->Float(c->depth) : c->depth;
                c->occlusionCulling = value.Find("occlusionCulling") ? value.Find("occlusionCulling")->Bool(c->occlusionCulling) : c->occlusionCulling;
                c->allowHDR = value.Find("allowHDR") ? value.Find("allowHDR")->Bool(c->allowHDR) : c->allowHDR;
                c->allowMSAA = value.Find("allowMSAA") ? value.Find("allowMSAA")->Bool(c->allowMSAA) : c->allowMSAA;
            }
            else if (auto* c = dynamic_cast<RendererComponent*>(&component))
            {
                c->color = ReadVec4(value.Find("color"), c->color);
                c->meshPath = ReadAssetReference(value.Find("meshPath"), c->meshPath);
                c->mainTexturePath = ReadAssetReference(value.Find("mainTexturePath"), c->mainTexturePath);
                c->materialPath = ReadAssetReference(value.Find("materialPath"), c->materialPath);
                c->shadowCastingMode = EnumValue(value.Find("shadowCastingMode"), c->shadowCastingMode);
                c->receiveShadows = value.Find("receiveShadows") ? value.Find("receiveShadows")->Bool(c->receiveShadows) : c->receiveShadows;
                c->staticShadowCaster = value.Find("staticShadowCaster") ? value.Find("staticShadowCaster")->Bool(c->staticShadowCaster) : c->staticShadowCaster;
                c->contributeGI = value.Find("contributeGI") ? value.Find("contributeGI")->Bool(c->contributeGI) : c->contributeGI;
                c->lightProbeUsage = value.Find("lightProbeUsage") ? value.Find("lightProbeUsage")->Int(c->lightProbeUsage) : c->lightProbeUsage;
                c->reflectionProbeUsage = value.Find("reflectionProbeUsage") ? value.Find("reflectionProbeUsage")->Int(c->reflectionProbeUsage) : c->reflectionProbeUsage;
                c->motionVectors = value.Find("motionVectors") ? value.Find("motionVectors")->Int(c->motionVectors) : c->motionVectors;
                c->dynamicOcclusion = value.Find("dynamicOcclusion") ? value.Find("dynamicOcclusion")->Bool(c->dynamicOcclusion) : c->dynamicOcclusion;
                c->renderingLayerMask = static_cast<uint32_t>(value.Find("renderingLayerMask") ? value.Find("renderingLayerMask")->Int(static_cast<int>(c->renderingLayerMask)) : c->renderingLayerMask);
            }
            else if (auto* c = dynamic_cast<SpriteRendererComponent*>(&component))
            {
                c->color = ReadVec4(value.Find("color"), c->color);
                c->spritePath = ReadAssetReference(value.Find("spritePath"), c->spritePath);
                c->materialPath = ReadAssetReference(value.Find("materialPath"), c->materialPath);
                c->flipX = value.Find("flipX") ? value.Find("flipX")->Bool(c->flipX) : c->flipX;
                c->flipY = value.Find("flipY") ? value.Find("flipY")->Bool(c->flipY) : c->flipY;
                c->drawMode = EnumValue(value.Find("drawMode"), c->drawMode);
                c->size = ReadVec2(value.Find("size"), c->size);
                c->maskInteraction = EnumValue(value.Find("maskInteraction"), c->maskInteraction);
                c->sortingLayer = value.Find("sortingLayer") ? value.Find("sortingLayer")->Int(c->sortingLayer) : c->sortingLayer;
                c->sortingOrder = value.Find("sortingOrder") ? value.Find("sortingOrder")->Int(c->sortingOrder) : c->sortingOrder;
                c->spriteSortPoint = EnumValue(value.Find("spriteSortPoint"), c->spriteSortPoint);
            }
            else if (auto* c = dynamic_cast<RigidbodyComponent*>(&component))
            {
                c->type = EnumValue(value.Find("bodyType"), c->type);
                c->mass = value.Find("mass") ? value.Find("mass")->Float(c->mass) : c->mass;
                c->useGravity = value.Find("useGravity") ? value.Find("useGravity")->Bool(c->useGravity) : c->useGravity;
                c->damp = value.Find("linearDamping") ? value.Find("linearDamping")->Float(c->damp) : c->damp;
                c->angularDamp = value.Find("angularDamping") ? value.Find("angularDamping")->Float(c->angularDamp) : c->angularDamp;
                c->isKinematic = value.Find("isKinematic") ? value.Find("isKinematic")->Bool(c->isKinematic) : c->isKinematic;
                c->interpolate = value.Find("interpolate") ? value.Find("interpolate")->Int(c->interpolate) : c->interpolate;
                c->collisionDetection = value.Find("collisionDetection") ? value.Find("collisionDetection")->Int(c->collisionDetection) : c->collisionDetection;
                if (const Value* freeze = value.Find("freezePosition"); freeze && freeze->IsArray() && freeze->AsArray().size() >= 3)
                    for (int i = 0; i < 3; ++i) c->freezePosition[i] = freeze->AsArray()[i].Bool(c->freezePosition[i]);
                if (const Value* freeze = value.Find("freezeRotation"); freeze && freeze->IsArray() && freeze->AsArray().size() >= 3)
                    for (int i = 0; i < 3; ++i) c->freezeRotation[i] = freeze->AsArray()[i].Bool(c->freezeRotation[i]);
                if (c->isKinematic) c->type = RigidbodyComponent::Kinematic;
            }
            else if (auto* c = dynamic_cast<ColliderComponent*>(&component))
            {
                c->type = EnumValue(value.Find("colliderType"), c->type);
                c->isTrigger = value.Find("isTrigger") ? value.Find("isTrigger")->Bool(c->isTrigger) : c->isTrigger;
                c->providesContacts = value.Find("providesContacts") ? value.Find("providesContacts")->Bool(c->providesContacts) : c->providesContacts;
                c->biasPosition = ReadVec3(value.Find("biasPosition"), c->biasPosition);
                c->biasRotation = ReadVec3(value.Find("biasRotation"), c->biasRotation);
                c->biasScale = ReadVec3(value.Find("biasScale"), c->biasScale);
                c->meshPath = ReadAssetReference(value.Find("meshPath"), c->meshPath);
            }
            else if (auto* c = dynamic_cast<Rigidbody2DComponent*>(&component))
            {
                c->type = EnumValue(value.Find("bodyType"), c->type);
                c->materialPath = ReadAssetReference(value.Find("materialPath"), c->materialPath);
                c->simulated = value.Find("simulated") ? value.Find("simulated")->Bool(c->simulated) : c->simulated;
                c->useAutoMass = value.Find("useAutoMass") ? value.Find("useAutoMass")->Bool(c->useAutoMass) : c->useAutoMass;
                c->mass = value.Find("mass") ? value.Find("mass")->Float(c->mass) : c->mass;
                c->useGravity = value.Find("useGravity") ? value.Find("useGravity")->Bool(c->useGravity) : c->useGravity;
                c->gravityScale = value.Find("gravityScale") ? value.Find("gravityScale")->Float(c->gravityScale) : c->gravityScale;
                c->linearDamping = value.Find("linearDamping") ? value.Find("linearDamping")->Float(c->linearDamping) : c->linearDamping;
                c->angularDamping = value.Find("angularDamping") ? value.Find("angularDamping")->Float(c->angularDamping) : c->angularDamping;
                c->collisionDetection = value.Find("collisionDetection") ? value.Find("collisionDetection")->Int(c->collisionDetection) : c->collisionDetection;
                c->sleepingMode = value.Find("sleepingMode") ? value.Find("sleepingMode")->Int(c->sleepingMode) : c->sleepingMode;
                c->interpolate = value.Find("interpolate") ? value.Find("interpolate")->Int(c->interpolate) : c->interpolate;
                c->freezePositionX = value.Find("freezePositionX") ? value.Find("freezePositionX")->Bool(c->freezePositionX) : c->freezePositionX;
                c->freezePositionY = value.Find("freezePositionY") ? value.Find("freezePositionY")->Bool(c->freezePositionY) : c->freezePositionY;
                c->freezeRotation = value.Find("freezeRotation") ? value.Find("freezeRotation")->Bool(c->freezeRotation) : c->freezeRotation;
                c->velocity = ReadVec2(value.Find("velocity"), c->velocity);
                c->angularVelocity = value.Find("angularVelocity") ? value.Find("angularVelocity")->Float(c->angularVelocity) : c->angularVelocity;
            }
            else if (auto* c = dynamic_cast<Collider2DComponent*>(&component))
            {
                c->type = EnumValue(value.Find("colliderType"), c->type);
                c->isTrigger = value.Find("isTrigger") ? value.Find("isTrigger")->Bool(c->isTrigger) : c->isTrigger;
                c->usedByEffector = value.Find("usedByEffector") ? value.Find("usedByEffector")->Bool(c->usedByEffector) : c->usedByEffector;
                c->usedByComposite = value.Find("usedByComposite") ? value.Find("usedByComposite")->Bool(c->usedByComposite) : c->usedByComposite;
                c->offset = ReadVec2(value.Find("offset"), c->offset);
                c->size = ReadVec2(value.Find("size"), c->size);
                c->radius = value.Find("radius") ? value.Find("radius")->Float(c->radius) : c->radius;
                c->restitution = value.Find("restitution") ? value.Find("restitution")->Float(c->restitution) : c->restitution;
                c->friction = value.Find("friction") ? value.Find("friction")->Float(c->friction) : c->friction;
            }
            else if (auto* c = dynamic_cast<AudioSourceComponent*>(&component))
            {
                c->clipPath = ReadAssetReference(value.Find("clipPath"), c->clipPath);
                c->outputPath = value.Find("outputPath") ? value.Find("outputPath")->String(c->outputPath) : c->outputPath;
                c->mute = value.Find("mute") ? value.Find("mute")->Bool(c->mute) : c->mute;
                c->bypassEffects = value.Find("bypassEffects") ? value.Find("bypassEffects")->Bool(c->bypassEffects) : c->bypassEffects;
                c->bypassListenerEffects = value.Find("bypassListenerEffects") ? value.Find("bypassListenerEffects")->Bool(c->bypassListenerEffects) : c->bypassListenerEffects;
                c->bypassReverbZones = value.Find("bypassReverbZones") ? value.Find("bypassReverbZones")->Bool(c->bypassReverbZones) : c->bypassReverbZones;
                c->volume = value.Find("volume") ? value.Find("volume")->Float(c->volume) : c->volume;
                c->pitch = value.Find("pitch") ? value.Find("pitch")->Float(c->pitch) : c->pitch;
                c->loop = value.Find("loop") ? value.Find("loop")->Bool(c->loop) : c->loop;
                c->playOnAwake = value.Find("playOnAwake") ? value.Find("playOnAwake")->Bool(c->playOnAwake) : c->playOnAwake;
                c->priority = value.Find("priority") ? value.Find("priority")->Int(c->priority) : c->priority;
                c->stereoPan = value.Find("stereoPan") ? value.Find("stereoPan")->Float(c->stereoPan) : c->stereoPan;
                c->spatialBlend = value.Find("spatialBlend") ? value.Find("spatialBlend")->Float(c->spatialBlend) : c->spatialBlend;
                c->reverbZoneMix = value.Find("reverbZoneMix") ? value.Find("reverbZoneMix")->Float(c->reverbZoneMix) : c->reverbZoneMix;
            }
            else if (auto* c = dynamic_cast<CanvasComponent*>(&component))
            {
                c->renderMode = EnumValue(value.Find("renderMode"), c->renderMode);
                c->pixelPerfect = value.Find("pixelPerfect") ? value.Find("pixelPerfect")->Bool(c->pixelPerfect) : c->pixelPerfect;
                c->planeDistance = value.Find("planeDistance") ? value.Find("planeDistance")->Float(c->planeDistance) : c->planeDistance;
                c->sortingOrder = value.Find("sortingOrder") ? value.Find("sortingOrder")->Int(c->sortingOrder) : c->sortingOrder;
            }
            else if (auto* c = dynamic_cast<RectTransformComponent*>(&component))
            {
                c->anchor = EnumValue(value.Find("anchor"), c->anchor);
                c->anchoredPosition = ReadVec2(value.Find("anchoredPosition"), c->anchoredPosition);
                c->sizeDelta = ReadVec2(value.Find("sizeDelta"), c->sizeDelta);
                c->pivot = ReadVec2(value.Find("pivot"), c->pivot);
            }
            else if (auto* c = dynamic_cast<UIImageComponent*>(&component))
            {
                c->anchor = EnumValue(value.Find("anchor"), c->anchor);
                c->offset = ReadVec2(value.Find("offset"), c->offset);
                c->size = ReadVec2(value.Find("size"), c->size);
                c->color = ReadVec4(value.Find("color"), c->color);
                c->texturePath = ReadAssetReference(value.Find("texturePath"), c->texturePath);
                c->type = EnumValue(value.Find("imageType"), c->type);
                c->raycastTarget = value.Find("raycastTarget") ? value.Find("raycastTarget")->Bool(c->raycastTarget) : c->raycastTarget;
                c->maskable = value.Find("maskable") ? value.Find("maskable")->Bool(c->maskable) : c->maskable;
            }
            else if (auto* c = dynamic_cast<UITextComponent*>(&component))
            {
                c->anchor = EnumValue(value.Find("anchor"), c->anchor);
                c->offset = ReadVec2(value.Find("offset"), c->offset);
                c->fontSize = value.Find("fontSize") ? value.Find("fontSize")->Float(c->fontSize) : c->fontSize;
                c->color = ReadVec4(value.Find("color"), c->color);
                c->text = value.Find("text") ? value.Find("text")->String(c->text) : c->text;
                c->fontPath = ReadAssetReference(value.Find("fontPath"), c->fontPath);
                c->fontStyle = value.Find("fontStyle") ? value.Find("fontStyle")->Int(c->fontStyle) : c->fontStyle;
                c->alignment = value.Find("alignment") ? value.Find("alignment")->Int(c->alignment) : c->alignment;
                c->raycastTarget = value.Find("raycastTarget") ? value.Find("raycastTarget")->Bool(c->raycastTarget) : c->raycastTarget;
                c->maskable = value.Find("maskable") ? value.Find("maskable")->Bool(c->maskable) : c->maskable;
            }
            else if (auto* c = dynamic_cast<UIButtonComponent*>(&component))
            {
                c->anchor = EnumValue(value.Find("anchor"), c->anchor);
                c->offset = ReadVec2(value.Find("offset"), c->offset);
                c->size = ReadVec2(value.Find("size"), c->size);
                c->color = ReadVec4(value.Find("color"), c->color);
                c->hoverColor = ReadVec4(value.Find("hoverColor"), c->hoverColor);
                c->pressedColor = ReadVec4(value.Find("pressedColor"), c->pressedColor);
                c->label = value.Find("label") ? value.Find("label")->String(c->label) : c->label;
                c->fontSize = value.Find("fontSize") ? value.Find("fontSize")->Float(c->fontSize) : c->fontSize;
                c->labelColor = ReadVec4(value.Find("labelColor"), c->labelColor);
                c->interactable = value.Find("interactable") ? value.Find("interactable")->Bool(c->interactable) : c->interactable;
                c->transition = value.Find("transition") ? value.Find("transition")->Int(c->transition) : c->transition;
                c->disabledColor = ReadVec4(value.Find("disabledColor"), c->disabledColor);
                c->colorMultiplier = value.Find("colorMultiplier") ? value.Find("colorMultiplier")->Float(c->colorMultiplier) : c->colorMultiplier;
                c->fadeDuration = value.Find("fadeDuration") ? value.Find("fadeDuration")->Float(c->fadeDuration) : c->fadeDuration;
            }
            else if (auto* c = dynamic_cast<CSharpScriptComponent*>(&component))
            {
                c->scriptName = value.Find("scriptName") ? value.Find("scriptName")->String(c->scriptName) : c->scriptName;
                c->scriptPath = ReadAssetReference(value.Find("scriptPath"), c->scriptPath);
                c->fields.clear();
                if (const Value* fields = value.Find("fields"); fields && fields->IsArray())
                    for (const Value& fieldValue : fields->AsArray())
                        c->fields.push_back(ScriptFieldFromJson(fieldValue));
            }
            else if (auto* c = dynamic_cast<AnimatorComponent*>(&component))
            {
                c->defaultClip = value.Find("defaultClip") ? value.Find("defaultClip")->String(c->defaultClip) : c->defaultClip;
                c->playOnAwake = value.Find("playOnAwake") ? value.Find("playOnAwake")->Bool(c->playOnAwake) : c->playOnAwake;
                c->playbackSpeed = value.Find("playbackSpeed") ? value.Find("playbackSpeed")->Float(c->playbackSpeed) : c->playbackSpeed;
                c->clips.clear();
                if (const Value* clips = value.Find("clips"); clips && clips->IsArray())
                {
                    for (const Value& clipValue : clips->AsArray())
                    {
                        AnimationClip clip;
                        clip.name = clipValue.Find("name") ? clipValue.Find("name")->String(clip.name) : clip.name;
                        clip.length = clipValue.Find("length") ? clipValue.Find("length")->Float(clip.length) : clip.length;
                        clip.loop = clipValue.Find("loop") ? clipValue.Find("loop")->Bool(clip.loop) : clip.loop;
                        if (const Value* keyframes = clipValue.Find("keyframes"); keyframes && keyframes->IsArray())
                        {
                            for (const Value& keyframeValue : keyframes->AsArray())
                            {
                                AnimationKeyframe keyframe;
                                keyframe.time = keyframeValue.Find("time") ? keyframeValue.Find("time")->Float(keyframe.time) : keyframe.time;
                                keyframe.position = ReadVec3(keyframeValue.Find("position"), keyframe.position);
                                keyframe.rotation = ReadVec3(keyframeValue.Find("rotation"), keyframe.rotation);
                                keyframe.scale = ReadVec3(keyframeValue.Find("scale"), keyframe.scale);
                                clip.keyframes.push_back(keyframe);
                            }
                        }
                        c->clips[clip.name] = std::move(clip);
                    }
                }
            }
            else if (auto* c = dynamic_cast<ParticleSystemComponent*>(&component))
            {
                c->maxParticles = value.Find("maxParticles") ? value.Find("maxParticles")->Int(c->maxParticles) : c->maxParticles;
                c->emissionRate = value.Find("emissionRate") ? value.Find("emissionRate")->Float(c->emissionRate) : c->emissionRate;
                c->looping = value.Find("looping") ? value.Find("looping")->Bool(c->looping) : c->looping;
                c->duration = value.Find("duration") ? value.Find("duration")->Float(c->duration) : c->duration;
                c->playOnAwake = value.Find("playOnAwake") ? value.Find("playOnAwake")->Bool(c->playOnAwake) : c->playOnAwake;
                c->startLifetime = value.Find("startLifetime") ? value.Find("startLifetime")->Float(c->startLifetime) : c->startLifetime;
                c->startSpeed = value.Find("startSpeed") ? value.Find("startSpeed")->Float(c->startSpeed) : c->startSpeed;
                c->startColor = ReadVec4(value.Find("startColor"), c->startColor);
                c->endColor = ReadVec4(value.Find("endColor"), c->endColor);
                c->startSize = value.Find("startSize") ? value.Find("startSize")->Float(c->startSize) : c->startSize;
                c->endSize = value.Find("endSize") ? value.Find("endSize")->Float(c->endSize) : c->endSize;
                c->materialPath = ReadAssetReference(value.Find("materialPath"), c->materialPath);
                c->gravity = ReadVec3(value.Find("gravity"), c->gravity);
                c->shape = EnumValue(value.Find("shape"), c->shape);
                c->coneAngle = value.Find("coneAngle") ? value.Find("coneAngle")->Float(c->coneAngle) : c->coneAngle;
                c->sphereRadius = value.Find("sphereRadius") ? value.Find("sphereRadius")->Float(c->sphereRadius) : c->sphereRadius;
                if (c->maxParticles > 0) c->particles.assign(c->maxParticles, Particle{});
            }
        }
    }

    Value ToJson(const GameObject& object)
    {
        Object json;
        json["name"] = object.name;
        json["enabled"] = object.enabled;
        json["locked"] = object.locked;
        if (!object.prefabSourcePath.empty() || !object.prefabSourceGuid.empty())
        {
            Object prefab;
            prefab["path"] = object.prefabSourcePath;
            prefab["guid"] = object.prefabSourceGuid;
            json["prefab"] = std::move(prefab);
        }

        Array components;
        for (const auto& component : object.components)
            if (component)
                components.push_back(ComponentToJson(*component));
        json["components"] = std::move(components);

        Array children;
        for (const auto& child : object.children)
            if (child)
                children.push_back(ToJson(*child));
        json["children"] = std::move(children);

        return json;
    }

    bool FromJson(GameObject& object, const Value& value, std::string* error)
    {
        if (!value.IsObject())
        {
            if (error) *error = "GameObject JSON must be an object";
            return false;
        }

        object.enabled = value.Find("enabled") ? value.Find("enabled")->Bool(true) : true;
        object.locked = value.Find("locked") ? value.Find("locked")->Bool(false) : false;
        object.name = value.Find("name") ? value.Find("name")->String("New GameObject") : "New GameObject";
        object.prefabSourcePath.clear();
        object.prefabSourceGuid.clear();
        if (const Value* prefab = value.Find("prefab"); prefab && prefab->IsObject())
        {
            object.prefabSourcePath = prefab->Find("path") ? prefab->Find("path")->String() : std::string();
            object.prefabSourceGuid = prefab->Find("guid") ? prefab->Find("guid")->String() : std::string();
        }
        object.components.clear();
        object.children.clear();
        object.removeComps.clear();
        object.compMask = 0;

        if (const Value* components = value.Find("components"); components && components->IsArray())
        {
            for (const Value& componentValue : components->AsArray())
            {
                std::string type = componentValue.Find("type") ? componentValue.Find("type")->String() : std::string();
                std::unique_ptr<Component> component = CreateComponent(type);
                if (!component) continue;
                component->gameObject = &object;
                ComponentFromJson(*component, componentValue);
                object.compMask |= component->index;
                object.components.push_back(std::move(component));
            }
        }

        if (!object.GetComponent<TransformComponent>())
            object.AddComponent<TransformComponent>();

        if (const Value* children = value.Find("children"); children && children->IsArray())
        {
            for (const Value& childValue : children->AsArray())
            {
                auto child = std::make_unique<GameObject>(false);
                if (!FromJson(*child, childValue, error)) return false;
                child->parent = &object;
                object.children.push_back(std::move(child));
            }
        }

        return true;
    }
}
