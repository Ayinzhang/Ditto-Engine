#include "JsonConfig.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace Ditto::JsonConfig
{
    namespace
    {
        std::string ReadAllText(const std::filesystem::path& path)
        {
            std::ifstream file(path);
            if (!file.is_open()) return {};
            std::ostringstream ss;
            ss << file.rdbuf();
            return ss.str();
        }

        void SkipWhitespace(const std::string& text, size_t& pos)
        {
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
                ++pos;
        }

        std::string EscapeString(const std::string& value)
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

        bool ParseStringLiteral(const std::string& text, size_t& pos, std::string& out)
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

        bool FindFieldValue(const std::string& text, const std::string& key, size_t& valuePos)
        {
            size_t pos = 0;
            while (pos < text.size())
            {
                std::string parsedKey;
                if (!ParseStringLiteral(text, pos, parsedKey))
                {
                    ++pos;
                    continue;
                }

                SkipWhitespace(text, pos);
                if (pos >= text.size() || text[pos] != ':') continue;
                ++pos;
                if (parsedKey == key)
                {
                    SkipWhitespace(text, pos);
                    valuePos = pos;
                    return true;
                }
            }
            return false;
        }

        std::string GetStringField(const std::string& text, const std::string& key)
        {
            size_t pos = 0;
            std::string value;
            if (!FindFieldValue(text, key, pos)) return {};
            return ParseStringLiteral(text, pos, value) ? value : std::string{};
        }

        bool GetBoolField(const std::string& text, const std::string& key, bool fallback)
        {
            size_t pos = 0;
            if (!FindFieldValue(text, key, pos)) return fallback;
            if (text.compare(pos, 4, "true") == 0) return true;
            if (text.compare(pos, 5, "false") == 0) return false;
            return fallback;
        }

        std::vector<std::string> GetStringArrayField(const std::string& text, const std::string& key)
        {
            std::vector<std::string> values;
            size_t pos = 0;
            if (!FindFieldValue(text, key, pos)) return values;
            if (pos >= text.size() || text[pos] != '[') return values;
            ++pos;

            while (pos < text.size())
            {
                SkipWhitespace(text, pos);
                if (pos < text.size() && text[pos] == ']') break;

                std::string value;
                if (!ParseStringLiteral(text, pos, value)) break;
                values.push_back(value);

                SkipWhitespace(text, pos);
                if (pos < text.size() && text[pos] == ',') ++pos;
            }
            return values;
        }
    }

    bool ReadProjectConfig(const std::filesystem::path& path, ProjectConfig& outConfig)
    {
        std::string text = ReadAllText(path);
        if (text.empty()) return false;

        outConfig.name = GetStringField(text, "name");
        std::string version = GetStringField(text, "version");
        std::string engineVersion = GetStringField(text, "engineVersion");
        outConfig.version = version.empty() ? "1.0" : version;
        outConfig.engineVersion = engineVersion.empty() ? "1.0" : engineVersion;
        outConfig.lastScene = GetStringField(text, "lastScene");
        return true;
    }

    bool WriteProjectConfig(const std::filesystem::path& path, const ProjectConfig& config)
    {
        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open()) return false;

        file << "{\n";
        file << "  \"name\": \"" << EscapeString(config.name) << "\",\n";
        file << "  \"version\": \"" << EscapeString(config.version) << "\",\n";
        file << "  \"engineVersion\": \"" << EscapeString(config.engineVersion) << "\",\n";
        file << "  \"lastScene\": \"" << EscapeString(config.lastScene) << "\"\n";
        file << "}\n";
        return true;
    }

    bool UpdateProjectLastScene(const std::filesystem::path& path, const std::string& lastScene)
    {
        ProjectConfig config;
        if (!ReadProjectConfig(path, config))
            return false;
        config.lastScene = lastScene;
        return WriteProjectConfig(path, config);
    }

    bool ReadGameConfig(const std::filesystem::path& path, GameConfig& outConfig)
    {
        std::string text = ReadAllText(path);
        if (text.empty()) return false;

        outConfig.productName = GetStringField(text, "productName");
        outConfig.companyName = GetStringField(text, "companyName");
        outConfig.version = GetStringField(text, "version");
        outConfig.startupScene = GetStringField(text, "startupScene");
        outConfig.scenes = GetStringArrayField(text, "scenes");
        outConfig.developmentBuild = GetBoolField(text, "developmentBuild", false);
        outConfig.enableScriptDebugging = GetBoolField(text, "enableScriptDebugging", false);
        return true;
    }

    bool WriteGameConfig(const std::filesystem::path& path, const GameConfig& config)
    {
        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open()) return false;

        file << "{\n";
        file << "  \"productName\": \"" << EscapeString(config.productName) << "\",\n";
        file << "  \"companyName\": \"" << EscapeString(config.companyName) << "\",\n";
        file << "  \"version\": \"" << EscapeString(config.version) << "\",\n";
        file << "  \"startupScene\": \"" << EscapeString(config.startupScene) << "\",\n";
        file << "  \"scenes\": [\n";
        for (size_t i = 0; i < config.scenes.size(); ++i)
        {
            file << "    \"" << EscapeString(config.scenes[i]) << "\"";
            if (i + 1 < config.scenes.size()) file << ",";
            file << "\n";
        }
        file << "  ],\n";
        file << "  \"developmentBuild\": " << (config.developmentBuild ? "true" : "false") << ",\n";
        file << "  \"enableScriptDebugging\": " << (config.enableScriptDebugging ? "true" : "false") << "\n";
        file << "}\n";
        return true;
    }
}
