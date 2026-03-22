// DittoEngine C# 辅助库源码
// 用户需要编译成 DittoEngine.dll 并放到 3rdParty/Mono/ 目录

using System;

namespace DittoEngine
{
    // Transform 组件（对应 C++ 的 TransformComponent）
    public class Transform
    {
        private IntPtr _nativeTransform;
        
        public Vector3 position
        {
            get 
            {
                float[] p = new float[3];
                GetPosition(_nativeTransform, p);
                return new Vector3(p[0], p[1], p[2]);
            }
            set
            {
                SetPosition(_nativeTransform, value.x, value.y, value.z);
            }
        }
        
        public Vector3 rotation;
        public Vector3 scale;
        public Vector3 forward;
        
        internal Transform(IntPtr native)
        {
            _nativeTransform = native;
        }
        
        // 内部调用 C++
        [System.Runtime.InteropServices.DllImport("DittoEngine.dll")]
        private static extern void GetPosition(IntPtr transform, float[] outPos);
        
        [System.Runtime.InteropServices.DllImport("DittoEngine.dll")]
        private static extern void SetPosition(IntPtr transform, float x, float y, float z);
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
        
        public static Vector3 operator +(Vector3 a, Vector3 b) 
            => new Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
        public static Vector3 operator -(Vector3 a, Vector3 b) 
            => new Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
        public static Vector3 operator *(Vector3 a, float b) 
            => new Vector3(a.x * b, a.y * b, a.z * b);
    }
    
    // Time 类
    public class Time
    {
        [System.Runtime.InteropServices.DllImport("DittoEngine.dll")]
        private static extern float GetDeltaTime();
        
        public static float deltaTime => GetDeltaTime();
        public static float time => 0;  // TODO: 实现
    }
    
    // Debug 类
    public class Debug
    {
        [System.Runtime.InteropServices.DllImport("DittoEngine.dll")]
        private static extern void Log(string msg);
        
        public static void Log(object msg) => Log(msg?.ToString() ?? "null");
    }
    
    // MonoBehaviour 基类（类似 Unity）
    public class MonoBehaviour
    {
        // 获取 GameObject
        public GameObject gameObject;
        
        // 获取 Transform
        public Transform transform => gameObject?.transform;
        
        // 生命周期方法（由 C++ 调用）
        public virtual void Start() { }
        public virtual void Update() { }
        public virtual void OnDestroy() { }
    }
    
    // GameObject 类
    public class GameObject
    {
        public string name;
        public Transform transform;
        
        // 组件
        public T GetComponent<T>() where T : class => null;
    }
}
