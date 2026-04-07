// DittoEngine C# 辅助库源码
// 用户需要编译成 DittoEngine.dll 并放到 3rdParty/Mono/ 目录

using System;
using System.Runtime.CompilerServices;

namespace DittoEngine
{
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
    
    // Transform 组件（对应 C++ 的 TransformComponent）
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
        public Vector3 forward => new Vector3(0, 0, 1); // TODO: 实现
        public Vector3 up => new Vector3(0, 1, 0); // TODO: 实现
        public Vector3 right => new Vector3(1, 0, 0); // TODO: 实现
        
        internal Transform(IntPtr native)
        {
            _nativeTransform = native;
        }
        
        // 内部调用 C++
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetPosition(IntPtr transform, float[] outPos);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetPosition(IntPtr transform, float x, float y, float z);
        
        public void Translate(Vector3 translation)
        {
            position += translation;
        }
        
        public void Translate(float x, float y, float z)
        {
            position += new Vector3(x, y, z);
        }
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
                    Console.WriteLine($"[C#] GetTransform returned: {transPtr}");
                    if (transPtr != IntPtr.Zero)
                    {
                        _transform = new Transform(transPtr);
                    }
                }
                return _transform;
            }
        }
        
        internal GameObject(IntPtr nativePtr)
        {
            _nativePtr = nativePtr;
        }
        
        // 组件
        public T GetComponent<T>() where T : class => null;
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr GetTransform(IntPtr gameObject);
    }
    
    // Time 类
    public class Time
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetDeltaTime();
        
        public static float deltaTime => GetDeltaTime();
        public static float time => 0;  // TODO: 实现
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
    
    // MonoBehaviour 基类（类似 Unity）
    public class MonoBehaviour
    {
        // 获取 GameObject - 由 C++ 设置
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
                    Console.WriteLine($"[C#] Creating GameObject wrapper for ptr: {_nativeGameObject}");
                    _gameObject = new GameObject(_nativeGameObject);
                }
                return _gameObject;
            }
        }
        
        // 获取 Transform（快捷方式）
        public Transform transform => gameObject?.transform;
        
        // 由 C++ 设置 native pointer
        public void SetNativeGameObject(IntPtr ptr)
        {
            _nativeGameObject = ptr;
        }
        
        // 生命周期方法（由 C++ 调用）
        public virtual void Start() { }
        public virtual void Update() { }
        public virtual void OnDestroy() { }
        public virtual void OnEnable() { }
        public virtual void OnDisable() { }
    }
}
