class ScriptComponentThrowingTick
{
    void Tick(EntityRef owner, float deltaTime)
    {
        throw("Tick failure");
    }
}
