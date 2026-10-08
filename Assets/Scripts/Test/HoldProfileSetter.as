class HoldProfileSetter
{
    void Tick(EntityRef owner, float deltaTime)
    {
        owner.SetHoldProfileByIndex(1);
    }
}
