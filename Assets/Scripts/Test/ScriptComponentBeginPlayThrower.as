class ScriptComponentBeginPlayExceptionProbe
{
    void Trigger()
    {
    }
}

class ScriptComponentBeginPlayThrower
{
    ScriptComponentBeginPlayExceptionProbe@ exceptionProbe;

    void BeginPlay(EntityRef owner)
    {
        Vector3 position = owner.GetPosition();
        position.x = 99.0f;
        owner.SetPosition(position);
        exceptionProbe.Trigger();
    }

    void Tick(EntityRef owner, float deltaTime)
    {
    }
}
