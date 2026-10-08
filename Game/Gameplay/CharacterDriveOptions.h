#pragma once
#include "Physics/CharacterBodyTypes.h"
#include <cstddef>
namespace Game::Gameplay
{
    enum class CharacterDriveArgumentResult
    {
        Unrecognized,
        Invalid,
        Valid
    };
    template <typename Char>
    CharacterDriveArgumentResult ParseCharacterDriveArgument(const Char* text,
                                                             NorvesLib::Modules::Physics::CharacterDriveMode& mode)
    {
        if (!text)
            return CharacterDriveArgumentResult::Unrecognized;
        constexpr char prefix[] = "--character-drive=";
        size_t i = 0;
        for (; i < sizeof(prefix) - 1; ++i)
            if (text[i] != static_cast<Char>(prefix[i]))
                return prefix[i] == '=' && text[i] == Char{} ? CharacterDriveArgumentResult::Invalid
                                                             : CharacterDriveArgumentResult::Unrecognized;
        const auto equals = [](const Char* value, const char* expected) {
            for (; *expected; ++expected, ++value)
                if (*value != static_cast<Char>(*expected))
                    return false;
            return *value == Char{};
        };
        if (equals(text + i, "fixed"))
            mode = NorvesLib::Modules::Physics::CharacterDriveMode::Fixed;
        else if (equals(text + i, "variable"))
            mode = NorvesLib::Modules::Physics::CharacterDriveMode::Variable;
        else
            return CharacterDriveArgumentResult::Invalid;
        return CharacterDriveArgumentResult::Valid;
    }
} // namespace Game::Gameplay
