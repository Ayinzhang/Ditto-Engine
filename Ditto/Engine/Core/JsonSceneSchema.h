#pragma once

#include "JsonValue.h"

#include <string>
#include <vector>

namespace Ditto::JsonSceneSchema
{
    struct ValidationResult
    {
        bool ok = true;
        std::vector<std::string> errors;

        void Add(std::string error)
        {
            ok = false;
            errors.push_back(std::move(error));
        }

        std::string Summary() const;
    };

    ValidationResult ValidateScene(const Json::Value& value, int expectedVersion);
    ValidationResult ValidatePrefab(const Json::Value& value, int expectedVersion);
    ValidationResult ValidateGameObject(const Json::Value& value, const std::string& path = "root");
}
