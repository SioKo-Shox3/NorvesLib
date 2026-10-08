#include "Game/Gameplay/CharacterDriveOptions.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace Game::Gameplay;
using NorvesLib::Modules::Physics::CharacterDriveMode;
template <typename Char>
void Cases(const Char* fixed, const Char* variable, const Char* missing, const Char* invalid, const Char* unrelated)
{
    CharacterDriveMode mode = CharacterDriveMode::Variable;
    assert(ParseCharacterDriveArgument(fixed, mode) == CharacterDriveArgumentResult::Valid &&
           mode == CharacterDriveMode::Fixed);
    assert(ParseCharacterDriveArgument(variable, mode) == CharacterDriveArgumentResult::Valid &&
           mode == CharacterDriveMode::Variable);
    for (const auto* value : {missing, invalid})
    {
        assert(ParseCharacterDriveArgument(value, mode) == CharacterDriveArgumentResult::Invalid);
        assert(mode == CharacterDriveMode::Variable);
    }
    assert(ParseCharacterDriveArgument(unrelated, mode) == CharacterDriveArgumentResult::Unrecognized);
    assert(ParseCharacterDriveArgument(static_cast<const Char*>(nullptr), mode) ==
           CharacterDriveArgumentResult::Unrecognized);
    assert(mode == CharacterDriveMode::Variable);
}
int main()
{
    Cases("--character-drive=fixed", "--character-drive=variable", "--character-drive",
          "--character-drive=", "--character-driver=fixed");
    Cases(L"--character-drive=fixed", L"--character-drive=variable", L"--character-drive", L"--character-drive=FIXED",
          L"--fixed-update-hz=120");
    Cases(u"--character-drive=fixed", u"--character-drive=variable", u"--character-drive",
          u"--character-drive=variable ", u"other");
    CharacterDriveMode mode = CharacterDriveMode::Fixed;
    for (const auto* text : {"--character-drive=variablex", "--character-drive= fixed", "--character-drive=0",
                             "--character-drive=variable=fixed"})
    {
        assert(ParseCharacterDriveArgument(text, mode) == CharacterDriveArgumentResult::Invalid);
        assert(mode == CharacterDriveMode::Fixed);
    }
    std::cout << "CharacterDriveOptionsTest passed\n";
}
