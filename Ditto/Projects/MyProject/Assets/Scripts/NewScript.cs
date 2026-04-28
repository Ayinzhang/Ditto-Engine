using DittoEngine;

public class NewScript : MonoBehaviour
{
    public float speed = 5.0f;
    public int health = 100;

    public override void Start()
    {
        Debug.Log("NewScript: Start");
        Debug.Log("gameObject is: " + (gameObject != null ? "NOT null" : "null"));
        Debug.Log("gameObject.transform is: " + (gameObject.transform != null ? "NOT null" : "null"));
        
        if (gameObject != null && gameObject.transform != null)
        {
            Vector3 pos = gameObject.transform.position;
            Debug.Log("Current position: " + pos.x + ", " + pos.y + ", " + pos.z);
            
            gameObject.transform.position = new Vector3(0, 5, 0);
            Debug.Log("Set position to: 0, 5, 0");
        }
        
        Debug.Log("Getting Renderer...");
        var renderer = gameObject.GetComponent<Renderer>();
        Debug.Log("Renderer is: " + (renderer != null ? "NOT null" : "null"));
        
        if (renderer != null)
        {
            renderer.color = new Vector4(1, 0, 0, 1);
            Debug.Log("Set renderer color to red");
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
