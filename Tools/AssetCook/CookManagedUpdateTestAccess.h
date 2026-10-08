#pragma once
#include "CookManagedUpdate.h"
#include "CookManagedTransactionController.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    // probeはprivate controllerのphaseとpackage ordinalを観測する。所有権や書込許可を与えない。
    using CookManagedUpdatePoint = ManagedTransaction::Point;
    using CookManagedUpdateProbe = ManagedTransaction::Probe;
    [[nodiscard]] CookManagedUpdateResult UpdateCookManagedAssetSetForTest(const CookManagedUpdateRequest& request,
                                                                           const CookManagedUpdateProbe& probe,
                                                                           CookManagedUpdateOutcome& out,
                                                                           Core::Container::AnsiString& error);
    [[nodiscard]] CookManagedRecoveryResult RecoverCookManagedUpdateForTest(const std::filesystem::path& runtime,
                                                                            const CookManagedUpdateProbe& probe,
                                                                            CookManagedUpdateOutcome& out,
                                                                            Core::Container::AnsiString& error);
} // 名前空間 NorvesLib::Tools::AssetCook::Detail
