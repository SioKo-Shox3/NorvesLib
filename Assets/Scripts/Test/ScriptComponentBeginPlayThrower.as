class ScriptComponentBeginPlayThrower
{
    void BeginPlay(EntityRef owner)
    {
        Vector3 position = owner.GetPosition();
        position.x = 99.0f;
        owner.SetPosition(position);
        throw("BeginPlay failure");
    }

    void Tick(EntityRef owner, float deltaTime)
    {
    }
}
