#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace Ditto::Json
{
    struct Value
    {
        using Array = std::vector<Value>;
        using Object = std::map<std::string, Value>;

        std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data;

        Value() : data(nullptr) {}
        Value(std::nullptr_t) : data(nullptr) {}
        Value(bool value) : data(value) {}
        Value(int value) : data(static_cast<double>(value)) {}
        Value(double value) : data(value) {}
        Value(const char* value) : data(std::string(value ? value : "")) {}
        Value(std::string value) : data(std::move(value)) {}
        Value(Array value) : data(std::move(value)) {}
        Value(Object value) : data(std::move(value)) {}

        bool IsNull() const { return std::holds_alternative<std::nullptr_t>(data); }
        bool IsBool() const { return std::holds_alternative<bool>(data); }
        bool IsNumber() const { return std::holds_alternative<double>(data); }
        bool IsString() const { return std::holds_alternative<std::string>(data); }
        bool IsArray() const { return std::holds_alternative<Array>(data); }
        bool IsObject() const { return std::holds_alternative<Object>(data); }

        const Object& AsObject() const;
        Object& AsObject();
        const Array& AsArray() const;
        Array& AsArray();

        const Value* Find(const std::string& key) const;
        Value* Find(const std::string& key);

        std::string String(const std::string& fallback = {}) const;
        bool Bool(bool fallback = false) const;
        int Int(int fallback = 0) const;
        float Float(float fallback = 0.0f) const;
        double Number(double fallback = 0.0) const;
    };

    bool Parse(const std::string& text, Value& outValue, std::string* error = nullptr);
    std::string Write(const Value& value, int indentSize = 2);
}
