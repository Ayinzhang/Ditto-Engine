using DittoEngine;

public class NewScript1 : MonoBehaviour
{
    public float speed = 5.0f;
    public int health = 100;

    void Start()
    {
        Debug.Log("NewScript1: Start");
    }

    void Update()
    {
    }

    void OnDestroy()
    {
        Debug.Log("NewScript1: OnDestroy");
    }
}
