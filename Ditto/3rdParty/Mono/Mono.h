// Mono headers for C++ integration
// Simplified version - add more functions as needed

#pragma once

#include <stdlib.h>

// Basic Mono types
typedef char*               MonoString;
typedef void*               MonoObject;
typedef void*               MonoClass;
typedef void*               MonoMethod;
typedef void*               MonoAssembly;
typedef void*               MonoImage;
typedef void*               MonoDomain;
typedef void*               MonoClassField;
typedef void*               MonoProperty;
typedef void*               MonoVTable;
typedef void*               MonoThread;

// Mono API function typedefs
typedef MonoDomain* (*mono_jit_init_t)(const char* domain_name);
typedef void (*mono_jit_cleanup_t)(MonoDomain* domain);
typedef MonoAssembly* (*mono_assembly_load_from_t)(MonoImage* image, const char* assembly_name, void* status);
typedef MonoImage* (*mono_assembly_get_image_t)(MonoAssembly* assembly);
typedef MonoClass* (*mono_class_from_name_t)(MonoImage* image, const char* name_space, const char* name);
typedef MonoObject* (*mono_class_new_t)(MonoVTable* vtable);
typedef MonoMethod* (*mono_class_get_methods_t)(MonoClass* klass, void** iter);
typedef MonoString* (*mono_string_new_t)(MonoDomain* domain, const char* text);
typedef MonoObject* (*mono_runtime_invoke_t)(MonoMethod* method, void* obj, void** params, MonoObject** exc);
typedef MonoClass* (*mono_object_get_class_t)(MonoObject* obj);
typedef const char* (*mono_class_get_name_t)(MonoClass* klass);
typedef const char* (*mono_method_get_name_t)(MonoMethod* method);
typedef void* (*mono_method_get_internal_t)(MonoMethod* method);
typedef MonoClassField* (*mono_class_get_fields_t)(MonoClass* klass, void** iter);
typedef const char* (*mono_field_get_name_t)(MonoClassField* field);
typedef void mono_field_set_value_t(MonoObject* obj, MonoClassField* field, void* value);
typedef void* (*mono_field_get_value_t)(MonoObject* obj, MonoClassField* field);
typedef void (*mono_add_internal_call_t)(const char* name, const void* method);
typedef void (*mono_config_parse_t)(const char* file);
typedef MonoObject* (*mono_object_new_t)(MonoDomain* domain, MonoClass* klass);
typedef void (*mono_property_get_value_t)(MonoProperty* prop, void* obj, void** params);
typedef MonoProperty* (*mono_class_get_property_from_name_t)(MonoClass* klass, const char* name);

// Mono assistant functions
typedef int (*mono_field_get_flags_t)(MonoClassField* field);
typedef size_t (*mono_field_get_offset_t)(MonoClassField* field);
