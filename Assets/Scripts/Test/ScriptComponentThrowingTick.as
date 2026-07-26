class ScriptComponentExceptionProbe
{
    void Trigger()
    {
    }
}

class ScriptComponentThrowingTick
{
    ScriptComponentExceptionProbe@ exceptionProbe;

    void Tick(EntityRef owner, float deltaTime)
    {
        Vector3 position = owner.GetPosition();
        position.x += 10.0f;
        owner.SetPosition(position);
        exceptionProbe.Trigger();
    }
}
