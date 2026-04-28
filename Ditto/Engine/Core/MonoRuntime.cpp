#include "MonoRuntime.h"
#include <iostream>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

typedef MonoDomain* (*mono_jit_init_t)(const char*);
typedef void (*mono_jit_cleanup_t)(MonoDomain*);
typedef void (*mono_config_parse_t)(const char*);
typedef MonoAssembly* (*mono_domain_assembly_open_t)(MonoDomain*, const char*);
typedef MonoImage* (*mono_assembly_get_image_t)(MonoAssembly*);
typedef MonoClass* (*mono_class_from_name_t)(MonoImage*, const char*, const char*);
typedef MonoObject* (*mono_object_new_t)(MonoDomain*, MonoClass*);
typedef void (*mono_runtime_object_init_t)(MonoObject*);
typedef MonoMethod* (*mono_class_get_method_from_name_t)(MonoClass*, const char*, int);
typedef MonoObject* (*mono_runtime_invoke_t)(MonoMethod*, void*, void**, MonoObject**);
typedef MonoString* (*mono_string_new_t)(MonoDomain*, const char*);
typedef char* (*mono_string_to_utf8_t)(MonoString*);
typedef void (*mono_free_t)(void*);
typedef void (*mono_add_internal_call_t)(const char*, void*);
typedef MonoClassField* (*mono_class_get_field_from_name_t)(MonoClass*, const char*);
typedef void (*mono_field_set_value_t)(MonoObject*, MonoClassField*, void*);
typedef void (*mono_field_get_value_t)(MonoObject*, MonoClassField*, void*);
typedef void (*mono_set_assemblies_path_t)(const char*);
typedef MonoClass* (*mono_object_get_class_t)(MonoObject*);
typedef MonoClass* (*mono_class_get_parent_t)(MonoClass*);
typedef MonoThread* (*mono_thread_attach_t)(MonoDomain*);
typedef void (*mono_thread_detach_t)(MonoThread*);
typedef uint32_t (*mono_gchandle_new_t)(MonoObject*, int);
typedef void (*mono_gchandle_free_t)(uint32_t);
typedef const char* (*mono_get_version_t)(void);
typedef int (*mono_image_get_table_rows_t)(MonoImage*, int);
typedef MonoClass* (*mono_class_get_t)(MonoImage*, uint32_t);
typedef const char* (*mono_class_get_name_t)(MonoClass*);
typedef const char* (*mono_class_get_namespace_t)(MonoClass*);

namespace MonoRuntime
{
    static HMODULE g_monoDll = nullptr;
    static MonoDomain* g_domain = nullptr;
    static bool g_initialized = false;

    static mono_jit_init_t p_mono_jit_init = nullptr;
    static mono_jit_cleanup_t p_mono_jit_cleanup = nullptr;
    static mono_config_parse_t p_mono_config_parse = nullptr;
    static mono_domain_assembly_open_t p_mono_domain_assembly_open = nullptr;
    static mono_assembly_get_image_t p_mono_assembly_get_image = nullptr;
    static mono_class_from_name_t p_mono_class_from_name = nullptr;
    static mono_object_new_t p_mono_object_new = nullptr;
    static mono_runtime_object_init_t p_mono_runtime_object_init = nullptr;
    static mono_class_get_method_from_name_t p_mono_class_get_method_from_name = nullptr;
    static mono_runtime_invoke_t p_mono_runtime_invoke = nullptr;
    static mono_string_new_t p_mono_string_new = nullptr;
    static mono_string_to_utf8_t p_mono_string_to_utf8 = nullptr;
    static mono_free_t p_mono_free = nullptr;
    static mono_add_internal_call_t p_mono_add_internal_call = nullptr;
    static mono_class_get_field_from_name_t p_mono_class_get_field_from_name = nullptr;
    static mono_field_set_value_t p_mono_field_set_value = nullptr;
    static mono_field_get_value_t p_mono_field_get_value = nullptr;
    static mono_object_get_class_t p_mono_object_get_class = nullptr;
    static mono_class_get_parent_t p_mono_class_get_parent = nullptr;
    static mono_set_assemblies_path_t p_mono_set_assemblies_path = nullptr;
    static mono_thread_attach_t p_mono_thread_attach = nullptr;
    static mono_thread_detach_t p_mono_thread_detach = nullptr;
    static mono_gchandle_new_t p_mono_gchandle_new = nullptr;
    static mono_gchandle_free_t p_mono_gchandle_free = nullptr;
    static mono_get_version_t p_mono_get_version = nullptr;
    static mono_image_get_table_rows_t p_mono_image_get_table_rows = nullptr;
    static mono_class_get_t p_mono_class_get = nullptr;
    static mono_class_get_name_t p_mono_class_get_name = nullptr;
    static mono_class_get_namespace_t p_mono_class_get_namespace = nullptr;

    static bool LoadMonoFunctions()
    {
        if (!g_monoDll) return false;

        p_mono_jit_init = (mono_jit_init_t)GetProcAddress(g_monoDll, "mono_jit_init");
        p_mono_jit_cleanup = (mono_jit_cleanup_t)GetProcAddress(g_monoDll, "mono_jit_cleanup");
        p_mono_config_parse = (mono_config_parse_t)GetProcAddress(g_monoDll, "mono_config_parse");
        p_mono_domain_assembly_open = (mono_domain_assembly_open_t)GetProcAddress(g_monoDll, "mono_domain_assembly_open");
        p_mono_assembly_get_image = (mono_assembly_get_image_t)GetProcAddress(g_monoDll, "mono_assembly_get_image");
        p_mono_class_from_name = (mono_class_from_name_t)GetProcAddress(g_monoDll, "mono_class_from_name");
        p_mono_object_new = (mono_object_new_t)GetProcAddress(g_monoDll, "mono_object_new");
        p_mono_runtime_object_init = (mono_runtime_object_init_t)GetProcAddress(g_monoDll, "mono_runtime_object_init");
        p_mono_class_get_method_from_name = (mono_class_get_method_from_name_t)GetProcAddress(g_monoDll, "mono_class_get_method_from_name");
        p_mono_runtime_invoke = (mono_runtime_invoke_t)GetProcAddress(g_monoDll, "mono_runtime_invoke");
        p_mono_string_new = (mono_string_new_t)GetProcAddress(g_monoDll, "mono_string_new");
        p_mono_string_to_utf8 = (mono_string_to_utf8_t)GetProcAddress(g_monoDll, "mono_string_to_utf8");
        p_mono_free = (mono_free_t)GetProcAddress(g_monoDll, "mono_free");
        p_mono_add_internal_call = (mono_add_internal_call_t)GetProcAddress(g_monoDll, "mono_add_internal_call");
        p_mono_class_get_field_from_name = (mono_class_get_field_from_name_t)GetProcAddress(g_monoDll, "mono_class_get_field_from_name");
        p_mono_field_set_value = (mono_field_set_value_t)GetProcAddress(g_monoDll, "mono_field_set_value");
        p_mono_field_get_value = (mono_field_get_value_t)GetProcAddress(g_monoDll, "mono_field_get_value");
        p_mono_object_get_class = (mono_object_get_class_t)GetProcAddress(g_monoDll, "mono_object_get_class");
        p_mono_class_get_parent = (mono_class_get_parent_t)GetProcAddress(g_monoDll, "mono_class_get_parent");
        p_mono_set_assemblies_path = (mono_set_assemblies_path_t)GetProcAddress(g_monoDll, "mono_set_assemblies_path");
        p_mono_thread_attach = (mono_thread_attach_t)GetProcAddress(g_monoDll, "mono_thread_attach");
        p_mono_thread_detach = (mono_thread_detach_t)GetProcAddress(g_monoDll, "mono_thread_detach");
        p_mono_gchandle_new = (mono_gchandle_new_t)GetProcAddress(g_monoDll, "mono_gchandle_new");
        p_mono_gchandle_free = (mono_gchandle_free_t)GetProcAddress(g_monoDll, "mono_gchandle_free");
        p_mono_get_version = (mono_get_version_t)GetProcAddress(g_monoDll, "mono_get_version");
        p_mono_image_get_table_rows = (mono_image_get_table_rows_t)GetProcAddress(g_monoDll, "mono_image_get_table_rows");
        p_mono_class_get = (mono_class_get_t)GetProcAddress(g_monoDll, "mono_class_get");
        p_mono_class_get_name = (mono_class_get_name_t)GetProcAddress(g_monoDll, "mono_class_get_name");
        p_mono_class_get_namespace = (mono_class_get_namespace_t)GetProcAddress(g_monoDll, "mono_class_get_namespace");

        if (!p_mono_runtime_invoke) std::cerr << "[MonoRuntime] Failed to load mono_runtime_invoke" << std::endl;
        if (!p_mono_thread_attach) std::cerr << "[MonoRuntime] Failed to load mono_thread_attach" << std::endl;
        if (!p_mono_object_get_class) std::cerr << "[MonoRuntime] Failed to load mono_object_get_class" << std::endl;

        return p_mono_jit_init && p_mono_jit_cleanup && p_mono_domain_assembly_open;
    }

    bool Initialize(const std::string& monoLibPath)
    {
        if (g_initialized) return true;

        static const char* monoRegKeys[] = {
            "SOFTWARE\\Mono",
            "SOFTWARE\\Ximian",
            "SOFTWARE\\Novell"
        };

        std::vector<std::string> tryPaths;

        if (!monoLibPath.empty())
        {
            tryPaths.push_back(monoLibPath + "/mono-2.0-sgen.dll");
        }

        char* monoPathEnv = nullptr;
        size_t monoPathLen = 0;
        if (_dupenv_s(&monoPathEnv, &monoPathLen, "MONO_PATH") == 0 && monoPathEnv)
        {
            tryPaths.push_back(std::string(monoPathEnv) + "/bin/mono-2.0-sgen.dll");
            free(monoPathEnv);
        }

        for (auto& regKey : monoRegKeys)
        {
            HKEY hKey = nullptr;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                char installRoot[MAX_PATH];
                DWORD size = MAX_PATH;
                if (RegQueryValueExA(hKey, "InstallRoot", nullptr, nullptr, (LPBYTE)installRoot, &size) == ERROR_SUCCESS)
                {
                    tryPaths.push_back(std::string(installRoot) + "/bin/mono-2.0-sgen.dll");
                }
                RegCloseKey(hKey);
            }
        }

        tryPaths.push_back("3rdParty/Mono/mono-2.0-sgen.dll");
        tryPaths.push_back("../../3rdParty/Mono/mono-2.0-sgen.dll");
        tryPaths.push_back("../3rdParty/Mono/mono-2.0-sgen.dll");

        for (const auto& path : tryPaths)
        {
            g_monoDll = LoadLibraryA(path.c_str());
            if (g_monoDll)
            {
                std::cout << "[MonoRuntime] Loaded mono.dll from: " << path << std::endl;
                break;
            }
        }

        if (!g_monoDll)
        {
            std::cerr << "[MonoRuntime] Failed to load mono.dll" << std::endl;
            std::cerr << "[MonoRuntime] Please install Mono from https://www.mono-project.com/download/" << std::endl;
            return false;
        }

        if (!LoadMonoFunctions())
        {
            std::cerr << "[MonoRuntime] Failed to load Mono functions" << std::endl;
            FreeLibrary(g_monoDll);
            g_monoDll = nullptr;
            return false;
        }

        if (p_mono_set_assemblies_path)
        {
            std::vector<std::string> assemblyPaths;

            char exePathBuf[MAX_PATH];
            DWORD exeLen = GetModuleFileNameA(NULL, exePathBuf, MAX_PATH);
            if (exeLen > 0 && exeLen < MAX_PATH)
            {
                std::string exeDir(exePathBuf);
                size_t lastSlash = exeDir.find_last_of("\\/");
                if (lastSlash != std::string::npos)
                {
                    std::string dir = exeDir.substr(0, lastSlash);
                    assemblyPaths.push_back(dir);
                    assemblyPaths.push_back(dir + "/Mono");
                }
            }

            assemblyPaths.push_back("3rdParty/Mono");
            assemblyPaths.push_back("../../3rdParty/Mono");
            assemblyPaths.push_back("../3rdParty/Mono");

            char* monoPathEnv = nullptr;
            size_t monoPathLen = 0;
            if (_dupenv_s(&monoPathEnv, &monoPathLen, "MONO_PATH") == 0 && monoPathEnv)
            {
                assemblyPaths.push_back(std::string(monoPathEnv) + "/lib/mono/4.5");
                free(monoPathEnv);
            }

            for (auto& regKey : monoRegKeys)
            {
                HKEY hKey = nullptr;
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
                {
                    char installRoot[MAX_PATH];
                    DWORD size = MAX_PATH;
                    if (RegQueryValueExA(hKey, "InstallRoot", nullptr, nullptr, (LPBYTE)installRoot, &size) == ERROR_SUCCESS)
                    {
                        assemblyPaths.push_back(std::string(installRoot) + "/lib/mono/4.5");
                    }
                    RegCloseKey(hKey);
                }
            }

            std::string combinedPath;
            for (const auto& path : assemblyPaths)
            {
                if (fs::exists(path + "/mscorlib.dll") || fs::exists(path + "/GameScripts.dll") || fs::exists(path + "/DittoEngine.dll"))
                {
                    if (!combinedPath.empty()) combinedPath += ";";
                    combinedPath += path;
                }
            }

            if (!combinedPath.empty())
            {
                p_mono_set_assemblies_path(combinedPath.c_str());
                std::cout << "[MonoRuntime] Set assemblies path: " << combinedPath << std::endl;
            }
        }

        p_mono_config_parse(nullptr);

        g_domain = p_mono_jit_init("DittoEngine");
        if (!g_domain)
        {
            std::cerr << "[MonoRuntime] Failed to initialize JIT" << std::endl;
            FreeLibrary(g_monoDll);
            g_monoDll = nullptr;
            return false;
        }

        g_initialized = true;

        if (p_mono_get_version)
        {
            std::cout << "[MonoRuntime] Mono version: " << p_mono_get_version() << std::endl;
        }

        std::cout << "[MonoRuntime] Initialized successfully" << std::endl;
        return true;
    }

    void Shutdown()
    {
        if (!g_initialized) return;

        if (g_domain && p_mono_jit_cleanup)
        {
            p_mono_jit_cleanup(g_domain);
            g_domain = nullptr;
        }

        if (g_monoDll)
        {
            FreeLibrary(g_monoDll);
            g_monoDll = nullptr;
        }

        g_initialized = false;
        std::cout << "[MonoRuntime] Shutdown" << std::endl;
    }

    bool IsInitialized()
    {
        return g_initialized;
    }

    MonoAssembly* LoadAssembly(const std::string& path)
    {
        if (!g_initialized || !g_domain) return nullptr;

        std::string absPath = fs::absolute(path).string();
        MonoAssembly* assembly = p_mono_domain_assembly_open(g_domain, absPath.c_str());

        if (!assembly)
        {
            std::cerr << "[MonoRuntime] Failed to load assembly: " << path << std::endl;
            return nullptr;
        }

        std::cout << "[MonoRuntime] Loaded assembly: " << path << std::endl;
        return assembly;
    }

    MonoImage* GetAssemblyImage(MonoAssembly* assembly)
    {
        if (!g_initialized || !assembly) return nullptr;
        return p_mono_assembly_get_image(assembly);
    }

    MonoClass* GetClass(MonoImage* image, const std::string& namespaceName, const std::string& className)
    {
        if (!g_initialized || !image) return nullptr;
        return p_mono_class_from_name(image, namespaceName.c_str(), className.c_str());
    }

    MonoClass* GetClassFromObject(MonoObject* obj)
    {
        if (!g_initialized || !obj || !p_mono_object_get_class) return nullptr;
        return p_mono_object_get_class(obj);
    }

    MonoClass* GetParentClass(MonoClass* klass)
    {
        if (!g_initialized || !klass || !p_mono_class_get_parent) return nullptr;
        return p_mono_class_get_parent(klass);
    }

    MonoMethod* GetMethod(MonoClass* klass, const std::string& methodName, int paramCount)
    {
        if (!g_initialized || !klass) return nullptr;
        return p_mono_class_get_method_from_name(klass, methodName.c_str(), paramCount);
    }

    MonoObject* CreateInstance(MonoClass* klass)
    {
        if (!g_initialized || !g_domain || !klass) return nullptr;

        MonoObject* obj = p_mono_object_new(g_domain, klass);
        if (obj)
        {
            p_mono_runtime_object_init(obj);
        }
        return obj;
    }

    MonoObject* InvokeMethod(MonoObject* instance, MonoMethod* method, void** params)
    {
        if (!g_initialized || !method) return nullptr;

        if (!p_mono_runtime_invoke)
        {
            std::cerr << "[MonoRuntime] InvokeMethod: p_mono_runtime_invoke is null!" << std::endl;
            return nullptr;
        }

        if (p_mono_thread_attach && g_domain)
        {
            p_mono_thread_attach(g_domain);
        }

        MonoObject* exc = nullptr;
        MonoObject* result = p_mono_runtime_invoke(method, instance, params, &exc);

        if (exc)
        {
            PrintException(exc);
            return nullptr;
        }

        return result;
    }

    MonoString* CreateString(const std::string& str)
    {
        if (!g_initialized || !g_domain) return nullptr;
        return p_mono_string_new(g_domain, str.c_str());
    }

    void AddInternalCall(const std::string& name, void* method)
    {
        if (!g_initialized || !p_mono_add_internal_call) return;
        p_mono_add_internal_call(name.c_str(), method);
    }

    std::shared_ptr<ScriptInstance> LoadScript(const std::string& dllPath, const std::string& className)
    {
        if (!g_initialized) return nullptr;

        auto script = std::make_shared<ScriptInstance>();
        script->className = className;
        script->assemblyPath = dllPath;

        // Pre-load DittoEngine.dll so base class MonoBehaviour can be resolved
        {
            const std::vector<std::string> enginePaths = {
                "3rdParty/Mono/DittoEngine.dll",
                "../../3rdParty/Mono/DittoEngine.dll",
                "../3rdParty/Mono/DittoEngine.dll",
                "Ditto/3rdParty/Mono/DittoEngine.dll",
                "../../Ditto/3rdParty/Mono/DittoEngine.dll",
            };
            for (const auto& path : enginePaths)
            {
                if (fs::exists(path))
                {
                    std::string absPath = fs::absolute(path).string();
                    MonoAssembly* engineAssembly = p_mono_domain_assembly_open(g_domain, absPath.c_str());
                    if (engineAssembly)
                        std::cout << "[MonoRuntime] Pre-loaded DittoEngine assembly: " << path << std::endl;
                    break;
                }
            }
        }

        MonoAssembly* assembly = LoadAssembly(dllPath);
        if (!assembly) return nullptr;

        MonoImage* image = GetAssemblyImage(assembly);
        if (!image) return nullptr;

        MonoClass* klass = nullptr;

        klass = GetClass(image, "", className);
        if (!klass) klass = GetClass(image, "DittoEngine", className);
        if (!klass) klass = GetClass(image, "MyProject", className);
        if (!klass) klass = GetClass(image, "Scripts", className);
        if (!klass) klass = GetClass(image, "Assets", className);

        // Fallback: enumerate TypeDef table to find class by name
        // Mono 4.6 has a bug where mono_class_from_name fails for Roslyn-compiled classes
        if (!klass && p_mono_image_get_table_rows && p_mono_class_get && p_mono_class_get_name)
        {
            int typeCount = p_mono_image_get_table_rows(image, 0x02);
            std::cout << "[MonoRuntime] mono_class_from_name failed, enumerating " << typeCount << " types..." << std::endl;
            for (int i = 1; i <= typeCount; i++)
            {
                uint32_t token = (0x02 << 24) | i;
                MonoClass* cls = p_mono_class_get(image, token);
                if (cls)
                {
                    const char* name = p_mono_class_get_name(cls);
                    if (name && className == name)
                    {
                        klass = cls;
                        const char* ns = p_mono_class_get_namespace ? p_mono_class_get_namespace(cls) : "";
                        std::cout << "[MonoRuntime] Found class '" << className << "' via enumeration, namespace: '" << (ns ? ns : "") << "'" << std::endl;
                        break;
                    }
                }
            }
        }

        if (!klass)
        {
            std::cerr << "[MonoRuntime] Class not found: " << className << std::endl;
            return nullptr;
        }

        script->instance = CreateInstance(klass);
        if (!script->instance)
        {
            std::cerr << "[MonoRuntime] Failed to create instance of: " << className << std::endl;

            MonoClass* parent = p_mono_class_get_parent ? p_mono_class_get_parent(klass) : nullptr;
            if (parent)
            {
                const char* parentName = p_mono_class_get_name ? p_mono_class_get_name(parent) : "?";
                std::cerr << "[MonoRuntime] Parent class: " << parentName << std::endl;
            }
            else
            {
                std::cerr << "[MonoRuntime] Parent class is null - base type resolution failed!" << std::endl;
            }
            return nullptr;
        }

        script->startMethod = GetMethod(klass, "Start", 0);
        script->updateMethod = GetMethod(klass, "Update", 0);
        script->onDestroyMethod = GetMethod(klass, "OnDestroy", 0);

        if (p_mono_gchandle_new && script->instance)
        {
            script->gcHandle = p_mono_gchandle_new(script->instance, 0);
            std::cout << "[MonoRuntime] Created GC handle for script: " << className << std::endl;
        }

        std::cout << "[MonoRuntime] Script loaded: " << className << std::endl;
        return script;
    }

    void UnloadScript(std::shared_ptr<ScriptInstance> script)
    {
        if (!script) return;

        CallOnDestroy(script);

        if (p_mono_gchandle_free && script->gcHandle)
        {
            p_mono_gchandle_free(script->gcHandle);
            script->gcHandle = 0;
        }

        script->instance = nullptr;
    }

    void CallStart(std::shared_ptr<ScriptInstance> script)
    {
        if (!script || !script->instance || !script->startMethod || script->started) return;

        InvokeMethod(script->instance, script->startMethod, nullptr);
        script->started = true;
    }

    void CallUpdate(std::shared_ptr<ScriptInstance> script)
    {
        if (!script || !script->instance || !script->updateMethod) return;

        if (!script->started)
        {
            CallStart(script);
        }

        InvokeMethod(script->instance, script->updateMethod, nullptr);
    }

    void CallOnDestroy(std::shared_ptr<ScriptInstance> script)
    {
        if (!script || !script->instance || !script->onDestroyMethod) return;
        InvokeMethod(script->instance, script->onDestroyMethod, nullptr);
    }

    void SetFieldValue(MonoObject* obj, const std::string& fieldName, void* value)
    {
        if (!g_initialized || !obj || !p_mono_class_get_field_from_name || !p_mono_object_get_class) return;

        MonoClass* klass = p_mono_object_get_class(obj);
        MonoClassField* field = p_mono_class_get_field_from_name(klass, fieldName.c_str());

        if (field && p_mono_field_set_value)
        {
            p_mono_field_set_value(obj, field, value);
        }
    }

    void GetFieldValue(MonoObject* obj, const std::string& fieldName, void* value)
    {
        if (!g_initialized || !obj || !p_mono_class_get_field_from_name || !p_mono_object_get_class) return;

        MonoClass* klass = p_mono_object_get_class(obj);
        MonoClassField* field = p_mono_class_get_field_from_name(klass, fieldName.c_str());

        if (field && p_mono_field_get_value)
        {
            p_mono_field_get_value(obj, field, value);
        }
    }

    std::string GetStringFromMono(MonoString* str)
    {
        if (!g_initialized || !str || !p_mono_string_to_utf8) return "";

        char* utf8 = p_mono_string_to_utf8(str);
        std::string result(utf8);

        if (p_mono_free)
        {
            p_mono_free(utf8);
        }

        return result;
    }

    void PrintException(MonoObject* exc)
    {
        if (!exc) return;
        std::cerr << "[MonoRuntime] Exception occurred during method invocation" << std::endl;
    }
}
