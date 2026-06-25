#include "JsonValue.h"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace Ditto::Json
{
    namespace
    {
        const Value::Object emptyObject;
        const Value::Array emptyArray;

        void SkipWhitespace(const std::string& text, size_t& pos)
        {
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
                ++pos;
        }

        std::string Escape(const std::string& value)
        {
            std::string out;
            out.reserve(value.size() + 8);
            for (char c : value)
            {
                switch (c)
                {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += c; break;
                }
            }
            return out;
        }

        bool ParseString(const std::string& text, size_t& pos, std::string& out)
        {
            SkipWhitespace(text, pos);
            if (pos >= text.size() || text[pos] != '"') return false;
            ++pos;
            out.clear();
            while (pos < text.size())
            {
                char c = text[pos++];
                if (c == '"') return true;
                if (c != '\\')
                {
                    out += c;
                    continue;
                }
                if (pos >= text.size()) return false;
                char escaped = text[pos++];
                switch (escaped)
                {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                default: out += escaped; break;
                }
            }
            return false;
        }

        bool ParseValue(const std::string& text, size_t& pos, Value& out);

        bool ParseArray(const std::string& text, size_t& pos, Value& out)
        {
            if (pos >= text.size() || text[pos] != '[') return false;
            ++pos;
            Value::Array array;
            SkipWhitespace(text, pos);
            if (pos < text.size() && text[pos] == ']') { ++pos; out = std::move(array); return true; }
            while (pos < text.size())
            {
                Value item;
                if (!ParseValue(text, pos, item)) return false;
                array.push_back(std::move(item));
                SkipWhitespace(text, pos);
                if (pos < text.size() && text[pos] == ',') { ++pos; continue; }
                if (pos < text.size() && text[pos] == ']') { ++pos; out = std::move(array); return true; }
                return false;
            }
            return false;
        }

        bool ParseObject(const std::string& text, size_t& pos, Value& out)
        {
            if (pos >= text.size() || text[pos] != '{') return false;
            ++pos;
            Value::Object object;
            SkipWhitespace(text, pos);
            if (pos < text.size() && text[pos] == '}') { ++pos; out = std::move(object); return true; }
            while (pos < text.size())
            {
                std::string key;
                if (!ParseString(text, pos, key)) return false;
                SkipWhitespace(text, pos);
                if (pos >= text.size() || text[pos] != ':') return false;
                ++pos;
                Value value;
                if (!ParseValue(text, pos, value)) return false;
                object[key] = std::move(value);
                SkipWhitespace(text, pos);
                if (pos < text.size() && text[pos] == ',') { ++pos; continue; }
                if (pos < text.size() && text[pos] == '}') { ++pos; out = std::move(object); return true; }
                return false;
            }
            return false;
        }

        bool ParseNumber(const std::string& text, size_t& pos, Value& out)
        {
            size_t start = pos;
            if (pos < text.size() && text[pos] == '-') ++pos;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
            if (pos < text.size() && text[pos] == '.')
            {
                ++pos;
                while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
            }
            if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E'))
            {
                ++pos;
                if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) ++pos;
                while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
            }
            if (start == pos) return false;
            try { out = std::stod(text.substr(start, pos - start)); }
            catch (...) { return false; }
            return true;
        }

        bool ParseValue(const std::string& text, size_t& pos, Value& out)
        {
            SkipWhitespace(text, pos);
            if (pos >= text.size()) return false;
            if (text[pos] == '"')
            {
                std::string value;
                if (!ParseString(text, pos, value)) return false;
                out = std::move(value);
                return true;
            }
            if (text[pos] == '{') return ParseObject(text, pos, out);
            if (text[pos] == '[') return ParseArray(text, pos, out);
            if (text.compare(pos, 4, "true") == 0) { pos += 4; out = true; return true; }
            if (text.compare(pos, 5, "false") == 0) { pos += 5; out = false; return true; }
            if (text.compare(pos, 4, "null") == 0) { pos += 4; out = nullptr; return true; }
            return ParseNumber(text, pos, out);
        }

        void WriteIndent(std::ostringstream& out, int depth, int indentSize)
        {
            for (int i = 0; i < depth * indentSize; ++i) out.put(' ');
        }

        void WriteValue(std::ostringstream& out, const Value& value, int depth, int indentSize)
        {
            if (value.IsNull()) { out << "null"; return; }
            if (value.IsBool()) { out << (std::get<bool>(value.data) ? "true" : "false"); return; }
            if (value.IsNumber())
            {
                out << std::setprecision(9) << std::get<double>(value.data);
                return;
            }
            if (value.IsString()) { out << '"' << Escape(std::get<std::string>(value.data)) << '"'; return; }
            if (value.IsArray())
            {
                const auto& array = std::get<Value::Array>(value.data);
                out << "[";
                if (!array.empty()) out << "\n";
                for (size_t i = 0; i < array.size(); ++i)
                {
                    WriteIndent(out, depth + 1, indentSize);
                    WriteValue(out, array[i], depth + 1, indentSize);
                    if (i + 1 < array.size()) out << ",";
                    out << "\n";
                }
                if (!array.empty()) WriteIndent(out, depth, indentSize);
                out << "]";
                return;
            }
            const auto& object = std::get<Value::Object>(value.data);
            out << "{";
            if (!object.empty()) out << "\n";
            size_t index = 0;
            for (const auto& [key, child] : object)
            {
                WriteIndent(out, depth + 1, indentSize);
                out << '"' << Escape(key) << "\": ";
                WriteValue(out, child, depth + 1, indentSize);
                if (++index < object.size()) out << ",";
                out << "\n";
            }
            if (!object.empty()) WriteIndent(out, depth, indentSize);
            out << "}";
        }
    }

    const Value::Object& Value::AsObject() const
    {
        return IsObject() ? std::get<Object>(data) : emptyObject;
    }

    Value::Object& Value::AsObject()
    {
        if (!IsObject()) data = Object{};
        return std::get<Object>(data);
    }

    const Value::Array& Value::AsArray() const
    {
        return IsArray() ? std::get<Array>(data) : emptyArray;
    }

    Value::Array& Value::AsArray()
    {
        if (!IsArray()) data = Array{};
        return std::get<Array>(data);
    }

    const Value* Value::Find(const std::string& key) const
    {
        if (!IsObject()) return nullptr;
        const auto& object = std::get<Object>(data);
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }

    Value* Value::Find(const std::string& key)
    {
        if (!IsObject()) return nullptr;
        auto& object = std::get<Object>(data);
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }

    std::string Value::String(const std::string& fallback) const
    {
        return IsString() ? std::get<std::string>(data) : fallback;
    }

    bool Value::Bool(bool fallback) const
    {
        return IsBool() ? std::get<bool>(data) : fallback;
    }

    int Value::Int(int fallback) const
    {
        return IsNumber() ? static_cast<int>(std::get<double>(data)) : fallback;
    }

    float Value::Float(float fallback) const
    {
        return IsNumber() ? static_cast<float>(std::get<double>(data)) : fallback;
    }

    double Value::Number(double fallback) const
    {
        return IsNumber() ? std::get<double>(data) : fallback;
    }

    bool Parse(const std::string& text, Value& outValue, std::string* error)
    {
        size_t pos = 0;
        if (!ParseValue(text, pos, outValue))
        {
            if (error) *error = "Invalid JSON near byte " + std::to_string(pos);
            return false;
        }
        SkipWhitespace(text, pos);
        if (pos != text.size())
        {
            if (error) *error = "Trailing JSON content near byte " + std::to_string(pos);
            return false;
        }
        return true;
    }

    std::string Write(const Value& value, int indentSize)
    {
        std::ostringstream out;
        WriteValue(out, value, 0, indentSize);
        out << "\n";
        return out.str();
    }
}
