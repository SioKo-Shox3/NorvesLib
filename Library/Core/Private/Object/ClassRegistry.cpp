#include "Object/IClass.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core
{

    ClassRegistry &ClassRegistry::Get()
    {
        static ClassRegistry instance;
        return instance;
    }

    uint64_t ClassRegistry::AllocateClassId()
    {
        return m_NextClassId++;
    }

    void ClassRegistry::RegisterClass(const IClass *cls)
    {
        if (!cls)
        {
            return;
        }

        const Identity &className = cls->GetClassName();
        auto idIt = m_ClassesById.find(cls->GetClassId());
        if (idIt != m_ClassesById.end() && idIt->second != cls)
        {
            NORVES_LOG_ERROR("ClassRegistry", "ClassId collision detected for class: %s",
                             className.ToString().c_str());
            return;
        }

        m_ClassesByName[className] = cls;
        m_ClassesById[cls->GetClassId()] = cls;
    }

    const IClass *ClassRegistry::FindClass(const Identity &className) const
    {
        auto it = m_ClassesByName.find(className);
        if (it != m_ClassesByName.end())
        {
            return it->second;
        }
        return nullptr;
    }

    const IClass *ClassRegistry::FindClass(uint64_t classId) const
    {
        auto it = m_ClassesById.find(classId);
        if (it != m_ClassesById.end())
        {
            return it->second;
        }
        return nullptr;
    }

    Container::VariableArray<const IClass *> ClassRegistry::GetAllClasses() const
    {
        Container::VariableArray<const IClass *> result;
        result.reserve(m_ClassesByName.size());
        for (const auto &pair : m_ClassesByName)
        {
            result.push_back(pair.second);
        }
        return result;
    }

} // namespace NorvesLib::Core
