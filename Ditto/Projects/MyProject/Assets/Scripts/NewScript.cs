using DittoEngine;

public class NewScript : MonoBehaviour
{
    public float speed = 5.0f;
    public int health = 100;

    void Start()
    {
        Debug.Log("NewScript: Start");
        gameObject.transform.position += new Vector3(0, 1, 0);
    }

    void Update()
    {
    }

    void OnDestroy()
    {
        Debug.Log("NewScript: OnDestroy");
    }
}
