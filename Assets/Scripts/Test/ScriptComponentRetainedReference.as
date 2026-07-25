class ScriptComponentRetainedReference
{
    EntityRef m_RetainedOwner;

    void BeginPlay(EntityRef owner)
    {
        m_RetainedOwner = owner;
    }

    void Tick(EntityRef owner, float deltaTime)
    {
        Vector3 position = m_RetainedOwner.GetPosition();
        position.x += deltaTime;
        m_RetainedOwner.SetPosition(position);
    }
}
