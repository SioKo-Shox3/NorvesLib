class M6Mover
{
    void BeginPlay(EntityRef owner)
    {
        Vector3 position = owner.GetPosition();
        position.z = 1.0f;
        owner.SetPosition(position);
    }

    void Tick(EntityRef owner, float deltaSeconds)
    {
        Vector3 position = owner.GetPosition();
        position.x += 1.0f;
        owner.SetPosition(position);
    }
}
