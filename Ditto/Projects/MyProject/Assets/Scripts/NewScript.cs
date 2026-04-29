using DittoEngine;

public class NewScript : MonoBehaviour
{
    public float speed = 5.0f;
    public int health = 100;

    public override void Start()
    {
        if (gameObject != null && gameObject.transform != null)
        {
            Vector3 pos = gameObject.transform.position;
            gameObject.transform.position = new Vector3(0, 1, 0);
        }
        
        var renderer = gameObject.GetComponent<Renderer>();
        
        if (renderer != null)
        {
            renderer.color = new Vector4(0, 0, 1, 1);
        }
    }

    public override void Update()
    {
    }

    public override void OnDestroy()
    {
        Debug.Log("NewScript: OnDestroy");
    }
}
