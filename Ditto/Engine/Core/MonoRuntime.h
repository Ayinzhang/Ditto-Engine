#pragma once

#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <windows.h>

// Forward declarations for Mono types
typedef struct _MonoDomain MonoDomain;
typedef struct _MonoAssembly MonoAssembly;
typedef struct _MonoImage MonoImage;
typedef struct _MonoClass MonoClass;
typedef struct _MonoObject MonoObject;
typedef struct _MonoMethod MonoMethod;
typedef struct _MonoString MonoString;
typedef struct _MonoClassField MonoClassField;
typedef struct _MonoThread MonoThread;

namespace MonoRuntime
{
    struct ScriptInstance
    {
        MonoObject* instance = nullptr;
        MonoMethod* startMethod = nullptr;
        MonoMethod* updateMethod = nullptr;
        MonoMethod* onDestroyMethod = nullptr;
        std::string className;
        std::string assemblyPath;
        bool started = false;
        uint32_t gcHandle = 0;  // GC handle to prevent object from being collected
    };

    bool Initialize(const std::string& monoLibPath);
    void Shutdown();
    bool IsInitialized();

    MonoAssembly* LoadAssembly(const std::string& path);
    MonoAssembly* LoadAssemblyFromMemory(const std::string& path);
    MonoImage* GetAssemblyImage(MonoAssembly* assembly);

    MonoClass* GetClass(MonoImage* image, const std::string& namespaceName, const std::string& className);
    MonoClass* GetClassFromObject(MonoObject* obj);
    MonoClass* GetParentClass(MonoClass* klass);
    MonoMethod* GetMethod(MonoClass* klass, const std::string& methodName, int paramCount = 0);

    MonoObject* CreateInstance(MonoClass* klass);

    MonoObject* InvokeMethod(MonoObject* instance, MonoMethod* method, void** params = nullptr);

    MonoString* CreateString(const std::string& str);

    void AddInternalCall(const std::string& name, void* method);

    std::shared_ptr<ScriptInstance> LoadScript(const std::string& dllPath, const std::string& className);
    void UnloadScript(std::shared_ptr<ScriptInstance> script);
    void CallStart(std::shared_ptr<ScriptInstance> script);
    void CallUpdate(std::shared_ptr<ScriptInstance> script);
    void CallOnDestroy(std::shared_ptr<ScriptInstance> script);

    void SetFieldValue(MonoObject* obj, const std::string& fieldName, void* value);
    void GetFieldValue(MonoObject* obj, const std::string& fieldName, void* value);

    std::string GetStringFromMono(MonoString* str);
    void PrintException(MonoObject* exc);
}
