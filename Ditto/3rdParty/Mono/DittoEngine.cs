// DittoEngine C# 辅助库源码
// 用户需要编译成 DittoEngine.dll 并放到 3rdParty/Mono/ 目录

using System;
using System.Runtime.CompilerServices;

namespace DittoEngine
{
    // Vector2
    public struct Vector2
    {
        public float x, y;
        
        public Vector2(float x, float y)
        {
            this.x = x; this.y = y;
        }
        
        public static Vector2 zero => new Vector2(0, 0);
        public static Vector2 one => new Vector2(1, 1);
        public static Vector2 right => new Vector2(1, 0);
        public static Vector2 up => new Vector2(0, 1);
        
        public static Vector2 operator +(Vector2 a, Vector2 b) 
            => new Vector2(a.x + b.x, a.y + b.y);
        public static Vector2 operator -(Vector2 a, Vector2 b) 
            => new Vector2(a.x - b.x, a.y - b.y);
        public static Vector2 operator *(Vector2 a, float b) 
            => new Vector2(a.x * b, a.y * b);
        public static Vector2 operator /(Vector2 a, float b) 
            => new Vector2(a.x / b, a.y / b);
        
        public override string ToString() => $"({x}, {y})";
    }
    
    // Vector3
    public struct Vector3
    {
        public float x, y, z;
        
        public Vector3(float x, float y, float z)
        {
            this.x = x; this.y = y; this.z = z;
        }
        
        public static Vector3 zero => new Vector3(0, 0, 0);
        public static Vector3 one => new Vector3(1, 1, 1);
        public static Vector3 forward => new Vector3(0, 0, 1);
        public static Vector3 up => new Vector3(0, 1, 0);
        public static Vector3 right => new Vector3(1, 0, 0);
        
        public static Vector3 operator +(Vector3 a, Vector3 b) 
            => new Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
        public static Vector3 operator -(Vector3 a, Vector3 b) 
            => new Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
        public static Vector3 operator *(Vector3 a, float b) 
            => new Vector3(a.x * b, a.y * b, a.z * b);
        public static Vector3 operator /(Vector3 a, float b) 
            => new Vector3(a.x / b, a.y / b, a.z / b);
        
        public override string ToString() => $"({x}, {y}, {z})";
    }
    
    // Vector4
    public struct Vector4
    {
        public float x, y, z, w;
        
        public Vector4(float x, float y, float z, float w)
        {
            this.x = x; this.y = y; this.z = z; this.w = w;
        }
        
        public static Vector4 zero => new Vector4(0, 0, 0, 0);
        public static Vector4 one => new Vector4(1, 1, 1, 1);
        
        public static Vector4 operator +(Vector4 a, Vector4 b) 
            => new Vector4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
        public static Vector4 operator -(Vector4 a, Vector4 b) 
            => new Vector4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
        public static Vector4 operator *(Vector4 a, float b) 
            => new Vector4(a.x * b, a.y * b, a.z * b, a.w * b);
        public static Vector4 operator /(Vector4 a, float b) 
            => new Vector4(a.x / b, a.y / b, a.z / b, a.w / b);
        
        public override string ToString() => $"({x}, {y}, {z}, {w})";
    }
    
    // Transform 组件
    public class Transform
    {
        private IntPtr _nativeTransform;
        
        public Vector3 position
        {
            get 
            {
                if (_nativeTransform == IntPtr.Zero)
                {
                    Console.WriteLine("[C#] Transform.position get: _nativeTransform is null!");
                    return Vector3.zero;
                }
                float[] p = new float[3];
                GetPosition(_nativeTransform, p);
                return new Vector3(p[0], p[1], p[2]);
            }
            set
            {
                if (_nativeTransform == IntPtr.Zero)
                {
                    Console.WriteLine("[C#] Transform.position set: _nativeTransform is null!");
                    return;
                }
                SetPosition(_nativeTransform, value.x, value.y, value.z);
            }
        }
        
        public Vector3 rotation { get; set; }
        public Vector3 scale { get; set; } = Vector3.one;
        public Vector3 forward => new Vector3(0, 0, 1);
        public Vector3 up => new Vector3(0, 1, 0);
        public Vector3 right => new Vector3(1, 0, 0);
        
        internal Transform(IntPtr native)
        {
            _nativeTransform = native;
        }
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetPosition(IntPtr transform, float[] outPos);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetPosition(IntPtr transform, float x, float y, float z);
        
        public void Translate(Vector3 translation) => position += translation;
        public void Translate(float x, float y, float z) => position += new Vector3(x, y, z);
    }
    
    // Renderer 组件
    public class Renderer
    {
        private IntPtr _nativeRenderer;
        
        public Vector4 color
        {
            get
            {
                if (_nativeRenderer == IntPtr.Zero)
                {
                    Console.WriteLine("[C#] Renderer.color get: _nativeRenderer is null!");
                    return Vector4.one;
                }
                float[] c = new float[4];
                GetColor(_nativeRenderer, c);
                return new Vector4(c[0], c[1], c[2], c[3]);
            }
            set
            {
                if (_nativeRenderer == IntPtr.Zero)
                {
                    Console.WriteLine("[C#] Renderer.color set: _nativeRenderer is null!");
                    return;
                }
                SetColor(_nativeRenderer, value.x, value.y, value.z, value.w);
            }
        }
        
        internal Renderer(IntPtr native)
        {
            _nativeRenderer = native;
        }
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetColor(IntPtr renderer, float[] outColor);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetColor(IntPtr renderer, float r, float g, float b, float a);
    }
    
    // GameObject 类
    public class GameObject
    {
        private IntPtr _nativePtr;
        private Transform _transform;
        
        public string name { get; set; }
        
        public Transform transform 
        { 
            get 
            {
                if (_nativePtr == IntPtr.Zero)
                {
                    Console.WriteLine("[C#] GameObject.transform: _nativePtr is null!");
                    return null;
                }
                if (_transform == null)
                {
                    IntPtr transPtr = GetTransform(_nativePtr);
                    if (transPtr != IntPtr.Zero)
                        _transform = new Transform(transPtr);
                }
                return _transform;
            }
        }
        
        internal GameObject(IntPtr nativePtr)
        {
            _nativePtr = nativePtr;
        }
        
        // 泛型 GetComponent
        public T GetComponent<T>() where T : class
        {
            if (_nativePtr == IntPtr.Zero) return null;
            
            string typeName = typeof(T).Name;
            IntPtr compPtr = GetComponentByType(_nativePtr, typeName);
            
            if (compPtr == IntPtr.Zero) return null;
            
            if (typeof(T) == typeof(Transform))
                return new Transform(compPtr) as T;
            if (typeof(T) == typeof(Renderer))
                return new Renderer(compPtr) as T;
            
            return null;
        }
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr GetTransform(IntPtr gameObject);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr GetComponentByType(IntPtr gameObject, string typeName);
    }
    
    // Time 类
    public class Time
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetDeltaTime();
        
        public static float deltaTime => GetDeltaTime();
        public static float time => 0;
        public static float fixedDeltaTime => 0.02f;
    }
    
    // Debug 类
    public class Debug
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void Log(string msg);
        
        public static void Log(object msg) => Log(msg?.ToString() ?? "null");
        public static void LogWarning(object msg) => Log("[Warning] " + msg?.ToString());
        public static void LogError(object msg) => Log("[Error] " + msg?.ToString());
    }
    
    // MonoBehaviour 基类
    public class MonoBehaviour
    {
        private IntPtr _nativeGameObject;
        private GameObject _gameObject;
        
        public GameObject gameObject 
        { 
            get 
            {
                if (_nativeGameObject == IntPtr.Zero)
                {
                    Console.WriteLine("[C#] MonoBehaviour.gameObject: _nativeGameObject is null!");
                    return null;
                }
                if (_gameObject == null && _nativeGameObject != IntPtr.Zero)
                {
                    _gameObject = new GameObject(_nativeGameObject);
                }
                return _gameObject;
            }
        }
        
        public Transform transform => gameObject?.transform;
        
        public void SetNativeGameObject(IntPtr ptr)
        {
            _nativeGameObject = ptr;
        }
        
        public virtual void Start() { }
        public virtual void Update() { }
        public virtual void OnDestroy() { }
        public virtual void OnEnable() { }
        public virtual void OnDisable() { }
    }
}
