class ScriptComponentMover
{
    void Tick(EntityRef owner, float deltaTime)
    {
        Vector3 position = owner.GetPosition();
        position.x += deltaTime;
        owner.SetPosition(position);
    }
}
