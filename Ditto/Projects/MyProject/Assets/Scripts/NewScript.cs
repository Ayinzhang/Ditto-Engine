using System;

public class NewScript
{
    public float speed = 5.0f;
    public int health = 100;

    void Start()
    {
        Console.WriteLine("NewScript: Start");
    }

    void Update()
    {
    }

    void OnDestroy()
    {
        Console.WriteLine("NewScript: OnDestroy");
    }
}
