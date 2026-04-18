using DittoEngine;

public class NewScript : MonoBehaviour
{
    public float speed = 5.0f;
    public int health = 100;

    public override void Start()
    {
        Debug.Log("NewScript: Start");
        gameObject.GetComponent<Renderer>().color = new Vector4(0, 0, 1, 1);
        gameObject.GetComponent<Light>().color = new Vector3(1, 0, 0);
    }

    public override void Update()
    {
    }

    public override void OnDestroy()
    {
        Debug.Log("NewScript: OnDestroy");
    }
}
