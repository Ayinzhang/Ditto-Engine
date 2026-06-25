#pragma once

#include "JsonValue.h"

struct GameObject;

namespace Ditto::GameObjectJson
{
    Json::Value ToJson(const GameObject& object);
    bool FromJson(GameObject& object, const Json::Value& value, std::string* error = nullptr);
}
