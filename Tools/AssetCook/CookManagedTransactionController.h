#pragma once
// AssetCook内部だけで共有するnative transaction機構。値の成功だけで書込権限を与えない。
#include "CookManagedBootstrap.h"
#include "CookManagedStoreNative.h"
#include "CookManagedTransactionIntent.h"
#include "CookDestinationLockTestAccess.h"
#include "CookOutputPaths.h"
namespace NorvesLib::Tools::AssetCook::Detail::ManagedTransaction
{
    using Text = Core::Container::AnsiString;
    using View = Core::Container::AnsiStringView;
    using Bytes = Core::Container::VariableArray<uint8_t>;
    template <class T> using Array = Core::Container::VariableArray<T>;
    using ByteView = Core::Container::Span<const uint8_t>;
    using RecoveryResult = CookManagedRecoveryResult;
    enum class Point
    {
        Prepared,
        PendingPublished,
        RootPublished,
        StatePublished,
        IndexBackedUp,
        IndexPublished,
        BeforeReceipt,
        ReceiptCommitted,
        BeforeRetire,
        Retired,
        RollbackIndexRemoved,
        RollbackIndexRestored,
        RollbackStateRestored,
        RollbackRootRestored,
        PackageBackedUp,
        PackagePublished,
        ManifestBackedUp,
        ManifestPublished,
        StateBackedUp,
        RollbackStateRemoved,
        RollbackManifestRemoved,
        RollbackManifestRestored,
        RollbackPackageRemoved,
        RollbackPackageRestored
    };
    struct Probe
    {
        bool (*Checkpoint)(Point, const std::filesystem::path&, const std::filesystem::path&, size_t, void*) = nullptr;
        void* Context = nullptr;
        CookLockFault LockFault = CookLockFault::None;
        bool* bAbandonedObserved = nullptr;
    };
    bool Fail(Text& error, const char* code);
    RecoveryResult Recover(const std::filesystem::path& runtime, const Probe* probe, CookManagedBootstrapOutcome& out,
                           Text& error);
#if defined(_WIN32)
    namespace N = Detail::ManagedStoreNative;
    namespace P = Detail::CookOutputPaths;
    using N::Handle;
    using N::Identity;
    inline constexpr size_t MaximumPackageBytes = 512ull * 1024 * 1024;
    struct File
    {
        Identity Native;
        CookManagedFileImage Image;
        Bytes Data;
    };
    struct StoreScope
    {
        Identity Workspace, Store, Pending;
        File Header;
        Text StoreId;
        Array<Identity> Ancestors;
        size_t AncestorRoots = 0;
        bool bPending = false;
    };
    struct TreeNode
    {
        Text Relative;
        Identity Native, Parent;
        CookManagedFileImage Image;
        bool bDirectory = false;
    };
    struct OptionalFile
    {
        bool bPresent = false;
        File Value;
    };
    struct Operation
    {
        std::filesystem::path RuntimeLocator;
        const Probe* Test = nullptr;
        StoreScope Scope;
        CookManagedBootstrapOutcome Outcome;
        Text Error;
        bool bPendingPublished = false, bRetired = false, bCommitted = false, bConflict = false;
    };
    CookManagedObjectId Object(const Identity& id);
    bool Same(const CookManagedObjectId& a, const CookManagedObjectId& b);
    bool Same(const CookManagedFileImage& a, const CookManagedFileImage& b);
    bool Same(const Identity& actual, const CookManagedObjectId& expected);
    std::filesystem::path Leaf(View text);
    ByteView BytesOf(const Text& text);
    bool EqualBytes(ByteView a, ByteView b);
    bool RecheckDirectory(const Identity& expected, Text& error);
    bool Find(const Identity& parent, const std::filesystem::path& leaf, const N::Entry*& found, N::Entries& entries,
              Text& error);
    bool Absent(const Identity& parent, const std::filesystem::path& leaf, Text& error);
    bool ReadChild(const Identity& parent, const std::filesystem::path& leaf, size_t limit, File& out, Text& error,
                   bool bEnumerate = true);
    bool NewFile(const Identity& parent, const std::filesystem::path& leaf, ByteView bytes, File& out, Text& error);
    bool NewDirectory(const Identity& parent, const std::filesystem::path& leaf, Identity& out, Text& error);
    bool Rename(const Identity& from, const std::filesystem::path& oldLeaf, const CookManagedObjectId& object,
                const Identity& to, const std::filesystem::path& newLeaf, bool bDirectory,
                const CookManagedFileImage* file, Text& error, bool* bMoved = nullptr);
    bool Token(Text& out, Text& error);
    bool LoadManifest(ByteView bytes, Core::Asset::AssetManifest& out, Text& error);
    bool VerifyClaims(const Identity& workspace, const Identity& store, const CookManagedStoreIndex& index,
                      const Array<Identity>& chain, Text& error);
    bool OpenStore(const CookDestinationLockContext& lock, StoreScope& out, Text& error);
    bool ReadSpec(const CookResolvedOwnerBinding& owner, File& out, Text& error);
    bool ScanTree(const Identity& root, Array<TreeNode>& out, const CookManagedIntentDraft* expected, Text& error);
    bool DeleteKnown(const Identity& parent, const std::filesystem::path& leaf, const CookManagedObjectId& object,
                     bool bDirectory, const CookManagedFileImage* file, Text& error);
    bool ReadOptional(const Identity& parent, const std::filesystem::path& leaf, size_t limit, OptionalFile& out,
                      Text& error);
    bool KnownDirectory(const Identity& parent, const std::filesystem::path& leaf, const CookManagedObjectId& expected,
                        bool& bPresent, Identity& out, Text& error);
    std::filesystem::path StateLeaf(View claim);
    bool Checkpoint(Operation& op, Point point, size_t ordinal = 0);
    bool ValidatePrepared(Operation& op);
    bool PublishBootstrap(Operation& op);
    bool PublishUpdate(Operation& op);
    std::filesystem::path PackageSlot(bool bBefore, size_t ordinal);
    bool ExistingParent(const Identity& root, View relative, Identity& parent, std::filesystem::path& leaf,
                        Text& error);
#endif
} // 名前空間 NorvesLib::Tools::AssetCook::Detail::ManagedTransaction
