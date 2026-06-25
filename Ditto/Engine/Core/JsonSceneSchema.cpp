#include "JsonSceneSchema.h"

#include <set>
#include <sstream>

namespace Ditto::JsonSceneSchema
{
    namespace
    {
        const std::set<std::string>& ComponentTypes()
        {
            static const std::set<std::string> types = {
                "Transform",
                "Light",
                "Camera",
                "Renderer",
                "SpriteRenderer",
                "Rigidbody",
                "Collider",
                "AudioSource",
                "UIImage",
                "UIText",
                "UIButton",
                "CSharpScript",
                "Rigidbody2D",
                "Collider2D",
                "Canvas",
                "RectTransform",
                "Animator",
                "ParticleSystem",
            };
            return types;
        }

        void RequireObject(const Json::Value* value, const std::string& path,
            ValidationResult& result)
        {
            if (!value || !value->IsObject())
                result.Add(path + " must be an object");
        }

        void RequireArray(const Json::Value* value, const std::string& path,
            ValidationResult& result)
        {
            if (!value || !value->IsArray())
                result.Add(path + " must be an array");
        }

        void ValidateAssetReference(const Json::Value* value, const std::string& path,
            ValidationResult& result)
        {
            if (!value || value->IsNull()) return;
            if (value->IsString()) return;
            if (!value->IsObject())
            {
                result.Add(path + " must be a string or { path, guid } object");
                return;
            }

            const Json::Value* refPath = value->Find("path");
            const Json::Value* guid = value->Find("guid");
            if (refPath && !refPath->IsString())
                result.Add(path + ".path must be a string");
            if (guid && !guid->IsString())
                result.Add(path + ".guid must be a string");
        }

        bool IsAssetReferenceField(const std::string& key)
        {
            return key == "meshPath"
                || key == "materialPath"
                || key == "mainTexturePath"
                || key == "spritePath"
                || key == "clipPath"
                || key == "texturePath"
                || key == "fontPath"
                || key == "scriptPath";
        }

        ValidationResult ValidateDocument(const Json::Value& value,
            const std::string& expectedFormat, int expectedVersion)
        {
            ValidationResult result;
            if (!value.IsObject())
            {
                result.Add("document must be an object");
                return result;
            }

            const Json::Value* format = value.Find("format");
            const Json::Value* version = value.Find("version");
            const Json::Value* root = value.Find("root");
            if (!format || !format->IsString() || format->String() != expectedFormat)
                result.Add("format must be \"" + expectedFormat + "\"");
            if (!version || !version->IsNumber() || version->Int() != expectedVersion)
                result.Add("version must be " + std::to_string(expectedVersion));
            RequireObject(root, "root", result);
            if (root && root->IsObject())
            {
                ValidationResult rootResult = ValidateGameObject(*root, "root");
                if (!rootResult.ok)
                {
                    result.ok = false;
                    result.errors.insert(result.errors.end(), rootResult.errors.begin(), rootResult.errors.end());
                }
            }
            return result;
        }
    }

    std::string ValidationResult::Summary() const
    {
        std::ostringstream out;
        for (size_t i = 0; i < errors.size(); ++i)
        {
            if (i) out << "; ";
            out << errors[i];
        }
        return out.str();
    }

    ValidationResult ValidateGameObject(const Json::Value& value, const std::string& path)
    {
        ValidationResult result;
        if (!value.IsObject())
        {
            result.Add(path + " must be an object");
            return result;
        }

        const Json::Value* name = value.Find("name");
        if (name && !name->IsString())
            result.Add(path + ".name must be a string");
        const Json::Value* enabled = value.Find("enabled");
        if (enabled && !enabled->IsBool())
            result.Add(path + ".enabled must be a bool");
        const Json::Value* locked = value.Find("locked");
        if (locked && !locked->IsBool())
            result.Add(path + ".locked must be a bool");

        if (const Json::Value* prefab = value.Find("prefab"))
            RequireObject(prefab, path + ".prefab", result);

        const Json::Value* components = value.Find("components");
        RequireArray(components, path + ".components", result);
        if (components && components->IsArray())
        {
            const auto& array = components->AsArray();
            for (size_t i = 0; i < array.size(); ++i)
            {
                const std::string componentPath = path + ".components[" + std::to_string(i) + "]";
                const Json::Value& component = array[i];
                if (!component.IsObject())
                {
                    result.Add(componentPath + " must be an object");
                    continue;
                }
                const Json::Value* type = component.Find("type");
                if (!type || !type->IsString())
                {
                    result.Add(componentPath + ".type must be a string");
                    continue;
                }
                if (!ComponentTypes().contains(type->String()))
                    result.Add(componentPath + ".type is unknown: " + type->String());

                if (const Json::Value* compEnabled = component.Find("enabled"); compEnabled && !compEnabled->IsBool())
                    result.Add(componentPath + ".enabled must be a bool");

                for (const auto& [key, child] : component.AsObject())
                {
                    if (IsAssetReferenceField(key))
                        ValidateAssetReference(&child, componentPath + "." + key, result);
                }
            }
        }

        const Json::Value* children = value.Find("children");
        RequireArray(children, path + ".children", result);
        if (children && children->IsArray())
        {
            const auto& array = children->AsArray();
            for (size_t i = 0; i < array.size(); ++i)
            {
                ValidationResult childResult = ValidateGameObject(array[i],
                    path + ".children[" + std::to_string(i) + "]");
                if (!childResult.ok)
                {
                    result.ok = false;
                    result.errors.insert(result.errors.end(), childResult.errors.begin(), childResult.errors.end());
                }
            }
        }

        return result;
    }

    ValidationResult ValidateScene(const Json::Value& value, int expectedVersion)
    {
        return ValidateDocument(value, "DittoScene", expectedVersion);
    }

    ValidationResult ValidatePrefab(const Json::Value& value, int expectedVersion)
    {
        return ValidateDocument(value, "DittoPrefab", expectedVersion);
    }
}
