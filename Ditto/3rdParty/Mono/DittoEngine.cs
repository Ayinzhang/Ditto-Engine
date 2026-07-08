


using System;
using System.Runtime.CompilerServices;

namespace DittoEngine
{
    [AttributeUsage(AttributeTargets.Field)]
    public sealed class SerializeFieldAttribute : Attribute
    {
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class HideInInspectorAttribute : Attribute
    {
    }

    
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
        
        public Vector3 rotation
        {
            get
            {
                if (_nativeTransform == IntPtr.Zero) return Vector3.zero;
                float[] r = new float[3];
                GetRotation(_nativeTransform, r);
                return new Vector3(r[0], r[1], r[2]);
            }
            set
            {
                if (_nativeTransform == IntPtr.Zero) return;
                SetRotation(_nativeTransform, value.x, value.y, value.z);
            }
        }

        public Vector3 scale
        {
            get
            {
                if (_nativeTransform == IntPtr.Zero) return Vector3.one;
                float[] s = new float[3];
                GetScale(_nativeTransform, s);
                return new Vector3(s[0], s[1], s[2]);
            }
            set
            {
                if (_nativeTransform == IntPtr.Zero) return;
                SetScale(_nativeTransform, value.x, value.y, value.z);
            }
        }
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

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetRotation(IntPtr transform, float[] outRot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetRotation(IntPtr transform, float x, float y, float z);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetScale(IntPtr transform, float[] outScale);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetScale(IntPtr transform, float x, float y, float z);
        
        public void Translate(Vector3 translation) => position += translation;
        public void Translate(float x, float y, float z) => position += new Vector3(x, y, z);
    }
    
    
    public class Renderer
    {
        public enum ShapeType { Cube, Sphere }
        
        private IntPtr _nativeRenderer;
        
        public ShapeType shapeType
        {
            get
            {
                if (_nativeRenderer == IntPtr.Zero) return ShapeType.Cube;
                return (ShapeType)GetShapeType(_nativeRenderer);
            }
            set
            {
                if (_nativeRenderer == IntPtr.Zero) return;
                SetShapeType(_nativeRenderer, (int)value);
            }
        }
        
        public Vector4 color
        {
            get
            {
                if (_nativeRenderer == IntPtr.Zero) return Vector4.one;
                float[] c = new float[4];
                GetColor(_nativeRenderer, c);
                return new Vector4(c[0], c[1], c[2], c[3]);
            }
            set
            {
                if (_nativeRenderer == IntPtr.Zero) return;
                SetColor(_nativeRenderer, value.x, value.y, value.z, value.w);
            }
        }
        
        internal Renderer(IntPtr native)
        {
            _nativeRenderer = native;
        }
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetShapeType(IntPtr renderer);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetShapeType(IntPtr renderer, int type);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetColor(IntPtr renderer, float[] outColor);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetColor(IntPtr renderer, float r, float g, float b, float a);
    }
    
    
    public class SpriteRenderer
    {
        private IntPtr _nativeSpriteRenderer;

        public Vector4 color
        {
            get
            {
                if (_nativeSpriteRenderer == IntPtr.Zero) return Vector4.one;
                float[] c = new float[4];
                GetColor(_nativeSpriteRenderer, c);
                return new Vector4(c[0], c[1], c[2], c[3]);
            }
            set
            {
                if (_nativeSpriteRenderer == IntPtr.Zero) return;
                SetColor(_nativeSpriteRenderer, value.x, value.y, value.z, value.w);
            }
        }

        public string sprite
        {
            get
            {
                if (_nativeSpriteRenderer == IntPtr.Zero) return "";
                return GetSprite(_nativeSpriteRenderer);
            }
            set
            {
                if (_nativeSpriteRenderer == IntPtr.Zero) return;
                SetSprite(_nativeSpriteRenderer, value ?? "");
            }
        }

        internal SpriteRenderer(IntPtr native)
        {
            _nativeSpriteRenderer = native;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetColor(IntPtr spriteRenderer, float[] outColor);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetColor(IntPtr spriteRenderer, float r, float g, float b, float a);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetSprite(IntPtr spriteRenderer);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetSprite(IntPtr spriteRenderer, string spritePath);
    }

    public class Light
    {
        private IntPtr _nativeLight;
        
        public Vector3 color
        {
            get
            {
                if (_nativeLight == IntPtr.Zero) return Vector3.one;
                float[] c = new float[3];
                GetLightColor(_nativeLight, c);
                return new Vector3(c[0], c[1], c[2]);
            }
            set
            {
                if (_nativeLight == IntPtr.Zero) return;
                SetLightColor(_nativeLight, value.x, value.y, value.z);
            }
        }
        
        public float intensity
        {
            get
            {
                if (_nativeLight == IntPtr.Zero) return 1.0f;
                return GetIntensity(_nativeLight);
            }
            set
            {
                if (_nativeLight == IntPtr.Zero) return;
                SetIntensity(_nativeLight, value);
            }
        }
        
        internal Light(IntPtr native)
        {
            _nativeLight = native;
        }
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetLightColor(IntPtr light, float[] outColor);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetLightColor(IntPtr light, float r, float g, float b);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetIntensity(IntPtr light);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetIntensity(IntPtr light, float intensity);
    }

    public struct Ray
    {
        public Vector3 origin;
        public Vector3 direction;

        public Ray(Vector3 origin, Vector3 direction)
        {
            this.origin = origin;
            this.direction = direction;
        }
    }

    public class Camera
    {
        public enum ProjectionType { Perspective = 0, Orthographic = 1 }

        private IntPtr _nativeCamera;
        internal Camera(IntPtr native) { _nativeCamera = native; }

        public bool isValid => _nativeCamera != IntPtr.Zero;

        public static Camera main
        {
            get
            {
                IntPtr camera = GetMainCameraNative();
                return camera == IntPtr.Zero ? null : new Camera(camera);
            }
        }

        public ProjectionType projectionType
        {
            get => _nativeCamera == IntPtr.Zero ? ProjectionType.Perspective : (ProjectionType)GetProjectionTypeNative(_nativeCamera);
            set { if (_nativeCamera != IntPtr.Zero) SetProjectionTypeNative(_nativeCamera, (int)value); }
        }

        public float fieldOfView
        {
            get => _nativeCamera == IntPtr.Zero ? 45.0f : GetFieldOfViewNative(_nativeCamera);
            set { if (_nativeCamera != IntPtr.Zero) SetFieldOfViewNative(_nativeCamera, value); }
        }

        public float orthographicSize
        {
            get => _nativeCamera == IntPtr.Zero ? 5.0f : GetOrthographicSizeNative(_nativeCamera);
            set { if (_nativeCamera != IntPtr.Zero) SetOrthographicSizeNative(_nativeCamera, value); }
        }

        public float nearClipPlane
        {
            get => _nativeCamera == IntPtr.Zero ? 0.1f : GetNearClipPlaneNative(_nativeCamera);
            set { if (_nativeCamera != IntPtr.Zero) SetNearClipPlaneNative(_nativeCamera, value); }
        }

        public float farClipPlane
        {
            get => _nativeCamera == IntPtr.Zero ? 100.0f : GetFarClipPlaneNative(_nativeCamera);
            set { if (_nativeCamera != IntPtr.Zero) SetFarClipPlaneNative(_nativeCamera, value); }
        }

        public Ray ScreenPointToRay(Vector2 screenPoint)
        {
            if (_nativeCamera == IntPtr.Zero)
                return new Ray(Vector3.zero, new Vector3(0.0f, 0.0f, -1.0f));

            float[] ray = new float[6];
            ScreenPointToRayNative(_nativeCamera, screenPoint.x, screenPoint.y, ray);
            return new Ray(
                new Vector3(ray[0], ray[1], ray[2]),
                new Vector3(ray[3], ray[4], ray[5]));
        }

        public Vector3 ScreenToWorldPoint(Vector3 screenPoint)
        {
            if (_nativeCamera == IntPtr.Zero) return Vector3.zero;
            float[] point = new float[3];
            ScreenToWorldPointNative(_nativeCamera, screenPoint.x, screenPoint.y, screenPoint.z, point);
            return new Vector3(point[0], point[1], point[2]);
        }

        public Vector3 ScreenToWorldPoint(Vector2 screenPoint, float worldZ = 0.0f)
        {
            if (_nativeCamera == IntPtr.Zero) return Vector3.zero;
            float[] point = new float[3];
            ScreenToWorldPointOnPlaneNative(_nativeCamera, screenPoint.x, screenPoint.y, worldZ, point);
            return new Vector3(point[0], point[1], point[2]);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr GetMainCameraNative();
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetProjectionTypeNative(IntPtr camera);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetProjectionTypeNative(IntPtr camera, int projectionType);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetFieldOfViewNative(IntPtr camera);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetFieldOfViewNative(IntPtr camera, float fieldOfView);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetOrthographicSizeNative(IntPtr camera);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetOrthographicSizeNative(IntPtr camera, float orthographicSize);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetNearClipPlaneNative(IntPtr camera);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetNearClipPlaneNative(IntPtr camera, float nearClipPlane);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetFarClipPlaneNative(IntPtr camera);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetFarClipPlaneNative(IntPtr camera, float farClipPlane);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ScreenPointToRayNative(IntPtr camera, float x, float y, float[] outRay);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ScreenToWorldPointNative(IntPtr camera, float x, float y, float distance, float[] outPoint);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ScreenToWorldPointOnPlaneNative(IntPtr camera, float x, float y, float worldZ, float[] outPoint);
    }
    
    
    public class Rigidbody
    {
        
        
        
        public enum BodyType { Static, Dynamic, Kinematic }
        
        private IntPtr _nativeRigidbody;
        
        public BodyType bodyType
        {
            get
            {
                if (_nativeRigidbody == IntPtr.Zero) return BodyType.Static;
                return (BodyType)GetBodyType(_nativeRigidbody);
            }
            set
            {
                if (_nativeRigidbody == IntPtr.Zero) return;
                SetBodyType(_nativeRigidbody, (int)value);
            }
        }
        
        public float mass
        {
            get
            {
                if (_nativeRigidbody == IntPtr.Zero) return 1.0f;
                return GetMass(_nativeRigidbody);
            }
            set
            {
                if (_nativeRigidbody == IntPtr.Zero) return;
                SetMass(_nativeRigidbody, value);
            }
        }
        
        public bool useGravity
        {
            get
            {
                if (_nativeRigidbody == IntPtr.Zero) return false;
                return GetUseGravity(_nativeRigidbody) != 0;
            }
            set
            {
                if (_nativeRigidbody == IntPtr.Zero) return;
                SetUseGravity(_nativeRigidbody, value ? 1 : 0);
            }
        }
        
        public float linearDamping
        {
            get
            {
                if (_nativeRigidbody == IntPtr.Zero) return 0.0f;
                return GetLinearDamping(_nativeRigidbody);
            }
            set
            {
                if (_nativeRigidbody == IntPtr.Zero) return;
                SetLinearDamping(_nativeRigidbody, value);
            }
        }
        
        public float angularDamping
        {
            get
            {
                if (_nativeRigidbody == IntPtr.Zero) return 0.0f;
                return GetAngularDamping(_nativeRigidbody);
            }
            set
            {
                if (_nativeRigidbody == IntPtr.Zero) return;
                SetAngularDamping(_nativeRigidbody, value);
            }
        }
        
        public Vector3 velocity
        {
            get
            {
                if (_nativeRigidbody == IntPtr.Zero) return Vector3.zero;
                float[] v = new float[3];
                GetVelocity(_nativeRigidbody, v);
                return new Vector3(v[0], v[1], v[2]);
            }
            set
            {
                if (_nativeRigidbody == IntPtr.Zero) return;
                SetVelocity(_nativeRigidbody, value.x, value.y, value.z);
            }
        }
        
        public Vector3 angularVelocity
        {
            get
            {
                if (_nativeRigidbody == IntPtr.Zero) return Vector3.zero;
                float[] v = new float[3];
                GetAngularVelocity(_nativeRigidbody, v);
                return new Vector3(v[0], v[1], v[2]);
            }
            set
            {
                if (_nativeRigidbody == IntPtr.Zero) return;
                SetAngularVelocity(_nativeRigidbody, value.x, value.y, value.z);
            }
        }
        
        internal Rigidbody(IntPtr native)
        {
            _nativeRigidbody = native;
        }
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetBodyType(IntPtr rigidbody);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetBodyType(IntPtr rigidbody, int type);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetMass(IntPtr rigidbody);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetMass(IntPtr rigidbody, float mass);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetUseGravity(IntPtr rigidbody);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetUseGravity(IntPtr rigidbody, int useGravity);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetLinearDamping(IntPtr rigidbody);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetLinearDamping(IntPtr rigidbody, float damp);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetAngularDamping(IntPtr rigidbody);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetAngularDamping(IntPtr rigidbody, float damp);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetVelocity(IntPtr rigidbody, float[] outVel);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetVelocity(IntPtr rigidbody, float x, float y, float z);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetAngularVelocity(IntPtr rigidbody, float[] outVel);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetAngularVelocity(IntPtr rigidbody, float x, float y, float z);
    }
    
    
    public enum ForceMode2D
    {
        Force = 0,
        Impulse = 1,
    }

    public class Rigidbody2D
    {
        public enum BodyType { Static, Dynamic, Kinematic }

        private IntPtr _nativeRigidbody;

        public BodyType bodyType
        {
            get => _nativeRigidbody == IntPtr.Zero ? BodyType.Static : (BodyType)GetBodyType(_nativeRigidbody);
            set { if (_nativeRigidbody != IntPtr.Zero) SetBodyType(_nativeRigidbody, (int)value); }
        }

        public float mass
        {
            get => _nativeRigidbody == IntPtr.Zero ? 1.0f : GetMass(_nativeRigidbody);
            set { if (_nativeRigidbody != IntPtr.Zero) SetMass(_nativeRigidbody, value); }
        }

        public bool useGravity
        {
            get => _nativeRigidbody != IntPtr.Zero && GetUseGravity(_nativeRigidbody) != 0;
            set { if (_nativeRigidbody != IntPtr.Zero) SetUseGravity(_nativeRigidbody, value ? 1 : 0); }
        }

        public float gravityScale
        {
            get => _nativeRigidbody == IntPtr.Zero ? 1.0f : GetGravityScale(_nativeRigidbody);
            set { if (_nativeRigidbody != IntPtr.Zero) SetGravityScale(_nativeRigidbody, value); }
        }

        public Vector2 velocity
        {
            get
            {
                if (_nativeRigidbody == IntPtr.Zero) return Vector2.zero;
                float[] v = new float[2];
                GetVelocity(_nativeRigidbody, v);
                return new Vector2(v[0], v[1]);
            }
            set { if (_nativeRigidbody != IntPtr.Zero) SetVelocity(_nativeRigidbody, value.x, value.y); }
        }

        public float angularVelocity
        {
            get => _nativeRigidbody == IntPtr.Zero ? 0.0f : GetAngularVelocity(_nativeRigidbody);
            set { if (_nativeRigidbody != IntPtr.Zero) SetAngularVelocity(_nativeRigidbody, value); }
        }

        internal Rigidbody2D(IntPtr native)
        {
            _nativeRigidbody = native;
        }

        public void AddForce(Vector2 force, ForceMode2D mode = ForceMode2D.Force)
        {
            if (_nativeRigidbody != IntPtr.Zero)
                AddForceNative(_nativeRigidbody, force.x, force.y, (int)mode);
        }

        public void AddTorque(float torque, ForceMode2D mode = ForceMode2D.Force)
        {
            if (_nativeRigidbody != IntPtr.Zero)
                AddTorqueNative(_nativeRigidbody, torque, (int)mode);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetBodyType(IntPtr rigidbody);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetBodyType(IntPtr rigidbody, int type);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetMass(IntPtr rigidbody);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetMass(IntPtr rigidbody, float mass);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetUseGravity(IntPtr rigidbody);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetUseGravity(IntPtr rigidbody, int useGravity);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetGravityScale(IntPtr rigidbody);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetGravityScale(IntPtr rigidbody, float gravityScale);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetVelocity(IntPtr rigidbody, float[] outVel);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetVelocity(IntPtr rigidbody, float x, float y);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetAngularVelocity(IntPtr rigidbody);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetAngularVelocity(IntPtr rigidbody, float v);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void AddForceNative(IntPtr rigidbody, float x, float y, int mode);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void AddTorqueNative(IntPtr rigidbody, float torque, int mode);
    }

    public class Collider2D
    {
        private IntPtr _native;
        internal Collider2D(IntPtr native) { _native = native; }
        public bool isValid => _native != IntPtr.Zero;
    }

    public class AudioSource
    {
        private IntPtr _nativeAudioSource;

        public float volume
        {
            get
            {
                if (_nativeAudioSource == IntPtr.Zero) return 1.0f;
                return GetVolume(_nativeAudioSource);
            }
            set
            {
                if (_nativeAudioSource == IntPtr.Zero) return;
                SetVolume(_nativeAudioSource, value);
            }
        }

        public bool loop
        {
            get
            {
                if (_nativeAudioSource == IntPtr.Zero) return false;
                return GetLoop(_nativeAudioSource) != 0;
            }
            set
            {
                if (_nativeAudioSource == IntPtr.Zero) return;
                SetLoop(_nativeAudioSource, value ? 1 : 0);
            }
        }

        public bool isPlaying
        {
            get
            {
                if (_nativeAudioSource == IntPtr.Zero) return false;
                return IsPlayingNative(_nativeAudioSource) != 0;
            }
        }

        public void Play()
        {
            if (_nativeAudioSource == IntPtr.Zero) return;
            PlayNative(_nativeAudioSource);
        }

        public void Stop()
        {
            if (_nativeAudioSource == IntPtr.Zero) return;
            StopNative(_nativeAudioSource);
        }

        internal AudioSource(IntPtr native)
        {
            _nativeAudioSource = native;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void PlayNative(IntPtr audioSource);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void StopNative(IntPtr audioSource);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetVolume(IntPtr audioSource);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetVolume(IntPtr audioSource, float volume);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetLoop(IntPtr audioSource);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetLoop(IntPtr audioSource, int loop);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsPlayingNative(IntPtr audioSource);
    }

    
    public class UIText
    {
        private IntPtr _native;

        public string text
        {
            get => _native == IntPtr.Zero ? "" : GetTextNative(_native);
            set { if (_native != IntPtr.Zero) SetTextNative(_native, value ?? ""); }
        }

        public Vector4 color
        {
            set { if (_native != IntPtr.Zero) SetColorNative(_native, value.x, value.y, value.z, value.w); }
        }

        internal UIText(IntPtr native) { _native = native; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetTextNative(IntPtr uiText, string text);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string GetTextNative(IntPtr uiText);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetColorNative(IntPtr uiText, float r, float g, float b, float a);
    }

    
    public class UIImage
    {
        private IntPtr _native;

        public Vector4 color
        {
            get
            {
                if (_native == IntPtr.Zero) return Vector4.one;
                float[] c = new float[4];
                GetColorNative(_native, c);
                return new Vector4(c[0], c[1], c[2], c[3]);
            }
            set { if (_native != IntPtr.Zero) SetColorNative(_native, value.x, value.y, value.z, value.w); }
        }

        internal UIImage(IntPtr native) { _native = native; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetColorNative(IntPtr uiImage, float r, float g, float b, float a);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetColorNative(IntPtr uiImage, float[] outColor);
    }

    
    public class UIButton
    {
        private IntPtr _native;

        
        public bool wasClicked
        {
            get => _native != IntPtr.Zero && ConsumeClick(_native) != 0;
        }

        public bool isHovered
        {
            get => _native != IntPtr.Zero && IsHoveredNative(_native) != 0;
        }

        public string label
        {
            set { if (_native != IntPtr.Zero) SetLabelNative(_native, value ?? ""); }
        }

        internal UIButton(IntPtr native) { _native = native; }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int ConsumeClick(IntPtr uiButton);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsHoveredNative(IntPtr uiButton);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetLabelNative(IntPtr uiButton, string label);
    }

    
    public class GameObject
    {
        private IntPtr _nativePtr;
        private Transform _transform;
        internal IntPtr NativePtr => _nativePtr;
        
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
            if (typeof(T) == typeof(SpriteRenderer))
                return new SpriteRenderer(compPtr) as T;
            if (typeof(T) == typeof(Camera))
                return new Camera(compPtr) as T;
            if (typeof(T) == typeof(Light))
                return new Light(compPtr) as T;
            if (typeof(T) == typeof(Rigidbody))
                return new Rigidbody(compPtr) as T;
            if (typeof(T) == typeof(Rigidbody2D))
                return new Rigidbody2D(compPtr) as T;
            if (typeof(T) == typeof(Collider2D))
                return new Collider2D(compPtr) as T;
            if (typeof(T) == typeof(AudioSource))
                return new AudioSource(compPtr) as T;
            if (typeof(T) == typeof(UIText))
                return new UIText(compPtr) as T;
            if (typeof(T) == typeof(UIImage))
                return new UIImage(compPtr) as T;
            if (typeof(T) == typeof(UIButton))
                return new UIButton(compPtr) as T;
            if (typeof(T) == typeof(Animator))
                return new Animator(compPtr) as T;
            if (typeof(T) == typeof(ParticleSystem))
                return new ParticleSystem(compPtr) as T;

            return null;
        }
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr GetTransform(IntPtr gameObject);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr GetComponentByType(IntPtr gameObject, string typeName);
    }
    
    
    public class Time
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetDeltaTime();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetTime();

        public static float deltaTime => GetDeltaTime();
        public static float time => GetTime();
        public static float fixedDeltaTime => 0.02f;
    }

    
    public enum KeyCode
    {
        Space = 32,
        Apostrophe = 39,
        Comma = 44, Minus = 45, Period = 46, Slash = 47,
        Alpha0 = 48, Alpha1 = 49, Alpha2 = 50, Alpha3 = 51, Alpha4 = 52,
        Alpha5 = 53, Alpha6 = 54, Alpha7 = 55, Alpha8 = 56, Alpha9 = 57,
        Semicolon = 59, Equal = 61,
        A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72,
        I = 73, J = 74, K = 75, L = 76, M = 77, N = 78, O = 79, P = 80,
        Q = 81, R = 82, S = 83, T = 84, U = 85, V = 86, W = 87, X = 88,
        Y = 89, Z = 90,
        LeftBracket = 91, Backslash = 92, RightBracket = 93, GraveAccent = 96,
        Escape = 256, Enter = 257, Tab = 258, Backspace = 259,
        Insert = 260, Delete = 261,
        RightArrow = 262, LeftArrow = 263, DownArrow = 264, UpArrow = 265,
        PageUp = 266, PageDown = 267, Home = 268, End = 269,
        CapsLock = 280, ScrollLock = 281, NumLock = 282,
        PrintScreen = 283, Pause = 284,
        F1 = 290, F2 = 291, F3 = 292, F4 = 293, F5 = 294, F6 = 295,
        F7 = 296, F8 = 297, F9 = 298, F10 = 299, F11 = 300, F12 = 301,
        Keypad0 = 320, Keypad1 = 321, Keypad2 = 322, Keypad3 = 323,
        Keypad4 = 324, Keypad5 = 325, Keypad6 = 326, Keypad7 = 327,
        Keypad8 = 328, Keypad9 = 329,
        KeypadDecimal = 330, KeypadDivide = 331, KeypadMultiply = 332,
        KeypadSubtract = 333, KeypadAdd = 334, KeypadEnter = 335,
        LeftShift = 340, LeftControl = 341, LeftAlt = 342, LeftSuper = 343,
        RightShift = 344, RightControl = 345, RightAlt = 346, RightSuper = 347,
        Menu = 348,
    }

    public enum MouseButton
    {
        Left = 0,
        Right = 1,
        Middle = 2,
        Button4 = 3,
        Button5 = 4,
        Button6 = 5,
        Button7 = 6,
        Button8 = 7,
    }

    
    
    public static class Input
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetKeyNative(int key);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetKeyDownNative(int key);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetKeyUpNative(int key);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMouseButtonNative(int button);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMouseButtonDownNative(int button);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetMouseButtonUpNative(int button);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetMousePositionNative(float[] outPos);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void GetGameViewportSizeNative(float[] outSize);

        public static bool GetKey(KeyCode key) => GetKeyNative((int)key) != 0;
        public static bool GetKeyDown(KeyCode key) => GetKeyDownNative((int)key) != 0;
        public static bool GetKeyUp(KeyCode key) => GetKeyUpNative((int)key) != 0;

        
        public static bool GetMouseButton(int button) => GetMouseButtonNative(button) != 0;
        public static bool GetMouseButtonDown(int button) => GetMouseButtonDownNative(button) != 0;
        public static bool GetMouseButtonUp(int button) => GetMouseButtonUpNative(button) != 0;
        public static bool GetMouseButton(MouseButton button) => GetMouseButtonNative((int)button) != 0;
        public static bool GetMouseButtonDown(MouseButton button) => GetMouseButtonDownNative((int)button) != 0;
        public static bool GetMouseButtonUp(MouseButton button) => GetMouseButtonUpNative((int)button) != 0;

        public static Vector2 mousePosition
        {
            get
            {
                float[] p = new float[2];
                GetMousePositionNative(p);
                return new Vector2(p[0], p[1]);
            }
        }

        public static Vector2 gameViewportSize
        {
            get
            {
                float[] size = new float[2];
                GetGameViewportSizeNative(size);
                return new Vector2(size[0], size[1]);
            }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetAxisNative(string axisName);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetAxisRawNative(string axisName);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetButtonNative(string buttonName);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetButtonDownNative(string buttonName);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int GetButtonUpNative(string buttonName);

        
        
        public static float GetAxis(string axisName) => GetAxisNative(axisName);
        public static float GetAxisRaw(string axisName) => GetAxisRawNative(axisName);

        
        
        public static bool GetButton(string buttonName) => GetButtonNative(buttonName) != 0;
        public static bool GetButtonDown(string buttonName) => GetButtonDownNative(buttonName) != 0;
        public static bool GetButtonUp(string buttonName) => GetButtonUpNative(buttonName) != 0;
    }
    
    
    public class Debug
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void Log(string msg);
        
        public static void Log(object msg) => Log(msg?.ToString() ?? "null");
        public static void LogWarning(object msg) => Log("[Warning] " + msg?.ToString());
        public static void LogError(object msg) => Log("[Error] " + msg?.ToString());
    }
    
    
    public class Collision
    {
        public GameObject gameObject;       
        public Vector3 contactPoint;        
        public Vector3 normal;              
        public float depth;                 

        public Transform transform => gameObject?.transform;
    }

    
    public struct RaycastHit
    {
        public GameObject gameObject;
        public Vector3 point;
        public Vector3 normal;
        public float distance;
    }

    
    public static class Physics
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int RaycastNative(float ox, float oy, float oz,
            float dx, float dy, float dz, float maxDist, float[] out7, out IntPtr outGo);

        public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit)
        {
            float[] data = new float[7];
            IntPtr go;
            int result = RaycastNative(origin.x, origin.y, origin.z,
                direction.x, direction.y, direction.z, maxDistance, data, out go);

            if (result == 0 || go == IntPtr.Zero)
            {
                hit = default(RaycastHit);
                return false;
            }

            hit = new RaycastHit
            {
                gameObject = new GameObject(go),
                point = new Vector3(data[0], data[1], data[2]),
                normal = new Vector3(data[3], data[4], data[5]),
                distance = data[6],
            };
            return true;
        }

        public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance)
        {
            RaycastHit hit;
            return Raycast(origin, direction, maxDistance, out hit);
        }
    }

    
    public class Animator
    {
        private IntPtr _nativePtr;

        public Animator(IntPtr ptr) { _nativePtr = ptr; }

        public void Play(string clipName = "")
        {
            if (_nativePtr != IntPtr.Zero)
                PlayNative(_nativePtr, clipName);
        }

        public void Stop()
        {
            if (_nativePtr != IntPtr.Zero)
                StopNative(_nativePtr);
        }

        public void Pause()
        {
            if (_nativePtr != IntPtr.Zero)
                PauseNative(_nativePtr);
        }

        public void Resume()
        {
            if (_nativePtr != IntPtr.Zero)
                ResumeNative(_nativePtr);
        }

        public float speed
        {
            get => _nativePtr != IntPtr.Zero ? GetSpeedNative(_nativePtr) : 1.0f;
            set { if (_nativePtr != IntPtr.Zero) SetSpeedNative(_nativePtr, value); }
        }

        public bool isPlaying => _nativePtr != IntPtr.Zero && IsPlayingNative(_nativePtr) != 0;

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void PlayNative(IntPtr animator, string clipName);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void StopNative(IntPtr animator);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void PauseNative(IntPtr animator);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ResumeNative(IntPtr animator);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float GetSpeedNative(IntPtr animator);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetSpeedNative(IntPtr animator, float speed);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsPlayingNative(IntPtr animator);
    }

    
    public class ParticleSystem
    {
        private IntPtr _nativePtr;

        public ParticleSystem(IntPtr ptr) { _nativePtr = ptr; }

        public void Play()
        {
            if (_nativePtr != IntPtr.Zero)
                PlayNative(_nativePtr);
        }

        public void Stop()
        {
            if (_nativePtr != IntPtr.Zero)
                StopNative(_nativePtr);
        }

        public void Clear()
        {
            if (_nativePtr != IntPtr.Zero)
                ClearNative(_nativePtr);
        }

        public bool isPlaying => _nativePtr != IntPtr.Zero && IsPlayingNative(_nativePtr) != 0;

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void PlayNative(IntPtr particleSystem);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void StopNative(IntPtr particleSystem);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ClearNative(IntPtr particleSystem);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int IsPlayingNative(IntPtr particleSystem);
    }

    
    public static class Object
    {
        public static GameObject Instantiate(GameObject original)
        {
            if (original == null) return null;
            IntPtr clone = InstantiateNative(original.NativePtr);
            return clone == IntPtr.Zero ? null : new GameObject(clone);
        }

        public static void Destroy(GameObject obj)
        {
            if (obj != null) DestroyNative(obj.NativePtr);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr InstantiateNative(IntPtr gameObject);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void DestroyNative(IntPtr gameObject);
    }

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
        public virtual void FixedUpdate() { }
        public virtual void OnDestroy() { }
        public virtual void OnEnable() { }
        public virtual void OnDisable() { }

        
        public virtual void OnCollisionEnter(Collision collision) { }
        public virtual void OnCollisionExit(Collision collision) { }
        public virtual void OnTriggerEnter(GameObject other) { }
        public virtual void OnTriggerExit(GameObject other) { }

        
        
        
        public void __DispatchCollision(int kind, IntPtr otherGO,
            float px, float py, float pz, float nx, float ny, float nz, float depth)
        {
            if (otherGO == IntPtr.Zero) return;
            var other = new GameObject(otherGO);

            switch (kind)
            {
                case 0:
                case 1:
                {
                    var collision = new Collision
                    {
                        gameObject = other,
                        contactPoint = new Vector3(px, py, pz),
                        normal = new Vector3(nx, ny, nz),
                        depth = depth,
                    };
                    if (kind == 0) OnCollisionEnter(collision);
                    else OnCollisionExit(collision);
                    break;
                }
                case 2: OnTriggerEnter(other); break;
                case 3: OnTriggerExit(other); break;
            }
        }
    }
}
