#pragma once
#include "CookManagedStoreIndex.h"
#include "CookOwnedState.h"
#include "CookOutputPlan.h"
#include <utility>
namespace NorvesLib::Tools::AssetCook
{
    struct CookManagedObjectId
    {
        uint64_t Volume = 0;
        Core::Container::FixedArray<uint8_t, 16> File = Core::Container::FixedArray<uint8_t, 16>(uint8_t{0});
    };
    struct CookManagedFileImage
    {
        CookManagedObjectId Object;
        uint64_t Size = 0, ContentHash = 0;
    };
    struct CookManagedBeforeImage
    {
        bool bPresent = false;
        CookManagedObjectId Parent;
        CookManagedFileImage File;
    };
    enum class CookManagedIntentMode
    {
        Bootstrap,
        Update
    };
    enum class CookManagedControlRole
    {
        IndexBefore,
        IndexAfter,
        StateBefore,
        StateAfter
    };
    struct CookManagedStoreAnchor
    {
        Core::Container::AnsiString StoreId, RootLeaf;
        CookManagedObjectId Workspace, Store, Pending;
        // 元callerまたは復旧controllerが独立に束縛する。保存locatorはI/O入口にしない。
        CookStateBinding Binding;
    };
    struct CookManagedPackageMutation
    {
        Core::Container::AnsiString Package;
        CookManagedBeforeImage Before;
        CookManagedFileImage After;
    };
    struct CookManagedTreeDirectory
    {
        Core::Container::AnsiString Relative;
        CookManagedObjectId Object, Parent;
    };
    struct CookManagedIntentDraft
    {
        CookManagedIntentMode Mode = CookManagedIntentMode::Bootstrap;
        CookManagedStoreAnchor Anchor;
        Core::Container::AnsiString TransactionId, ClaimId;
        CookManagedObjectId Root;
        uint64_t IndexGeneration = 0, StateGeneration = 0;
        // 添字はCookManagedControlRoleの固定4slot。BootstrapのStateBeforeだけzero image。
        Core::Container::FixedArray<CookManagedFileImage, 4> Controls;
        CookManagedBeforeImage ManifestBefore;
        CookManagedFileImage ManifestAfter, Receipt;
        Core::Container::VariableArray<CookManagedPackageMutation> Packages;
        // Bootstrapの必要directory閉包だけ。root自身はRootに保持する。
        Core::Container::VariableArray<CookManagedTreeDirectory> Directories;
    };
    struct CookManagedDocumentBytes
    {
        CookManagedObjectId ObservedObject;
        Core::Container::Span<const uint8_t> Bytes;
    };
    struct CookManagedControlDocuments
    {
        Core::Container::FixedArray<CookManagedDocumentBytes, 4> Controls;
        CookManagedDocumentBytes ManifestAfter;
    };
    struct CookManagedIntentBuildInput
    {
        CookManagedIntentDraft Draft;
        Core::Container::Span<const CookPreparedPlan> FinalPlans;
        // Cookは共通capture、Skipは旧recordを渡す。source locatorやdecisionをwireへ保存しない。
        Core::Container::Span<const CookOwnedRecord> FinalRecords;
    };
    class CookManagedTransactionIntent;
    class CookManagedTransactionEnvelope;
    inline constexpr size_t MaximumCookManagedIntentBytes = 32 * 1024 * 1024;
    inline constexpr size_t MaximumCookManagedTreeEntries = 65536;
    // いずれも純値検査。成功はfileの所有証明・rename/delete権限・live再検査の代用ではない。
    // NoChangeはcontrollerで処理し、世代を進めるmutationだけをfactoryへ渡す。
    // errorは入力/outと独立に渡す。失敗時outは保持する。
    [[nodiscard]] bool BuildCookManagedTransactionIntent(const CookManagedIntentBuildInput& input,
                                                         const CookManagedControlDocuments& documents,
                                                         CookManagedTransactionIntent& out,
                                                         Core::Container::AnsiString& error);
    [[nodiscard]] bool SerializeCookManagedTransactionIntent(const CookManagedTransactionIntent& intent,
                                                             Core::Container::AnsiString& out,
                                                             Core::Container::AnsiString& error);
    [[nodiscard]] bool ParseCookManagedTransactionEnvelope(Core::Container::Span<const uint8_t> bytes,
                                                           const CookManagedStoreAnchor& independentAnchor,
                                                           CookManagedTransactionEnvelope& out,
                                                           Core::Container::AnsiString& error);
    [[nodiscard]] bool ParseCookManagedTransactionIntent(const CookManagedTransactionEnvelope& envelope,
                                                         const CookManagedControlDocuments& documents,
                                                         CookManagedTransactionIntent& out,
                                                         Core::Container::AnsiString& error);
    // receipt bodyはintent digestを含めず、作成前に生成できる。16KiB以下。
    [[nodiscard]] bool MakeCookManagedReceiptBody(const CookManagedIntentDraft& draft, Core::Container::AnsiString& out,
                                                  Core::Container::AnsiString& error);
    class CookManagedTransactionIntent
    {
      public:
        CookManagedTransactionIntent() = default;
        CookManagedTransactionIntent(const CookManagedTransactionIntent&) = default;
        CookManagedTransactionIntent& operator=(const CookManagedTransactionIntent& other)
        {
            if (this != &other)
            {
                CookManagedTransactionIntent candidate(other);
                *this = std::move(candidate);
            }
            return *this;
        }
        CookManagedTransactionIntent(CookManagedTransactionIntent&& other)
            : m_bValid(std::exchange(other.m_bValid, false)), m_Value(std::move(other.m_Value))
        {
        }
        CookManagedTransactionIntent& operator=(CookManagedTransactionIntent&& other)
        {
            if (this != &other)
            {
                const bool bValid = std::exchange(other.m_bValid, false);
                m_bValid = false;
                m_Value = std::move(other.m_Value);
                m_bValid = bValid;
            }
            return *this;
        }
        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_bValid;
        }
        [[nodiscard]] const CookManagedIntentDraft& Value() const noexcept
        {
            return m_Value;
        }

      private:
        bool m_bValid = false;
        CookManagedIntentDraft m_Value;
        friend bool BuildCookManagedTransactionIntent(const CookManagedIntentBuildInput&,
                                                      const CookManagedControlDocuments&, CookManagedTransactionIntent&,
                                                      Core::Container::AnsiString&);
        friend bool ParseCookManagedTransactionIntent(const CookManagedTransactionEnvelope&,
                                                      const CookManagedControlDocuments&, CookManagedTransactionIntent&,
                                                      Core::Container::AnsiString&);
    };
    class CookManagedTransactionEnvelope
    {
      public:
        CookManagedTransactionEnvelope() = default;
        CookManagedTransactionEnvelope(const CookManagedTransactionEnvelope&) = default;
        CookManagedTransactionEnvelope& operator=(const CookManagedTransactionEnvelope& other)
        {
            if (this != &other)
            {
                CookManagedTransactionEnvelope candidate(other);
                *this = std::move(candidate);
            }
            return *this;
        }
        CookManagedTransactionEnvelope(CookManagedTransactionEnvelope&& other)
            : m_bValid(std::exchange(other.m_bValid, false)), m_Value(std::move(other.m_Value))
        {
        }
        CookManagedTransactionEnvelope& operator=(CookManagedTransactionEnvelope&& other)
        {
            if (this != &other)
            {
                const bool bValid = std::exchange(other.m_bValid, false);
                m_bValid = false;
                m_Value = std::move(other.m_Value);
                m_bValid = bValid;
            }
            return *this;
        }
        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_bValid;
        }
        [[nodiscard]] Core::Container::AnsiStringView ClaimId() const noexcept
        {
            return m_bValid ? Core::Container::AnsiStringView(m_Value.ClaimId) : Core::Container::AnsiStringView{};
        }
        // 任意保存pathを公開せず、固定roleの既知objectだけを読み取るための値。
        [[nodiscard]] const CookManagedFileImage* Control(CookManagedControlRole role) const noexcept
        {
            const auto index = static_cast<size_t>(role);
            return m_bValid && index < 4 ? &m_Value.Controls[index] : nullptr;
        }
        [[nodiscard]] const CookManagedFileImage* ManifestAfter() const noexcept
        {
            return m_bValid ? &m_Value.ManifestAfter : nullptr;
        }

      private:
        bool m_bValid = false;
        CookManagedIntentDraft m_Value;
        friend bool ParseCookManagedTransactionEnvelope(Core::Container::Span<const uint8_t>,
                                                        const CookManagedStoreAnchor&, CookManagedTransactionEnvelope&,
                                                        Core::Container::AnsiString&);
        friend bool ParseCookManagedTransactionIntent(const CookManagedTransactionEnvelope&,
                                                      const CookManagedControlDocuments&, CookManagedTransactionIntent&,
                                                      Core::Container::AnsiString&);
    };
} // namespace NorvesLib::Tools::AssetCook
