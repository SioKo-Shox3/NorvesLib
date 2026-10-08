class AnimatorParameterSetter
{
    void Tick(EntityRef owner, float deltaTime)
    {
        owner.SetAnimFloatByIndex(0, 0.75f);
    }
}
