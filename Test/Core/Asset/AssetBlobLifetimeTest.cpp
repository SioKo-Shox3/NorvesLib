#include "Asset/AssetBlob.h"
#include <cassert>
#include <iostream>
#include <type_traits>
#include <utility>

using namespace NorvesLib::Core::Asset;
using NorvesLib::Core::Container::MakeShared;
using NorvesLib::Core::Container::Span;
using NorvesLib::Core::Container::TSharedPtr;
using NorvesLib::Core::Container::VariableArray;

int main()
{
    std::cout << "AssetBlobLifetimeTest start\n";

    using ByteSpan = Span<const uint8_t>;
    static_assert(!std::is_constructible_v<AssetBlob, ByteSpan>);
    static_assert(!std::is_convertible_v<ByteSpan, AssetBlob>);

    uint8_t sourceBytes[] = {1, 2, 3, 4};
    AssetBlob copied = AssetBlob::CopyBytes(ByteSpan{sourceBytes, 4}, "copy.bin");
    assert(copied.IsValid());
    assert(!copied.IsEmpty());
    assert(copied.GetSize() == 4);
    assert(copied.GetData()[0] == 1);
    assert(copied.GetData()[3] == 4);
    assert(copied.GetStorageKind() == AssetBlob::StorageKind::OwnedBytes);
    assert(copied.GetSourcePath() == "copy.bin");

    sourceBytes[0] = 99;
    assert(copied.GetData()[0] == 1);

    AssetBlob copiedAgain = copied;
    AssetBlob moved = std::move(copiedAgain);
    AssetBlob subBlob = moved.TryCreateSubBlob(1, 2);
    assert(subBlob.IsValid());
    assert(subBlob.GetStorageKind() == AssetBlob::StorageKind::OwnedRange);
    assert(subBlob.GetSize() == 2);
    assert(subBlob.GetData()[0] == 2);
    assert(subBlob.GetData()[1] == 3);
    assert(subBlob.GetSourcePath() == "copy.bin");

    TSharedPtr<VariableArray<uint8_t>> mutableOwner = MakeShared<VariableArray<uint8_t>>();
    mutableOwner->push_back(10);
    mutableOwner->push_back(11);
    mutableOwner->push_back(12);
    mutableOwner->push_back(13);
    TSharedPtr<const VariableArray<uint8_t>> owner = mutableOwner;

    AssetBlob ownedRange = AssetBlob::FromOwnedRange(owner, 1, 2, "owned-range.bin");
    AssetBlob invalidOverflowRange = AssetBlob::FromOwnedRange(owner, static_cast<size_t>(-1), 1, "overflow.bin");
    assert(!invalidOverflowRange.IsValid());

    mutableOwner.reset();
    owner.reset();
    assert(ownedRange.IsValid());
    assert(ownedRange.GetSize() == 2);
    assert(ownedRange.GetData()[0] == 11);
    assert(ownedRange.GetData()[1] == 12);

    AssetBlob invalidRange = ownedRange.TryCreateSubBlob(2, 1);
    assert(!invalidRange.IsValid());
    assert(invalidRange.IsEmpty());
    assert(invalidRange.GetData() == nullptr);
    assert(invalidRange.GetSpan().empty());

    auto zeroOwner = MakeShared<VariableArray<uint8_t>>();
    TSharedPtr<const VariableArray<uint8_t>> constZeroOwner = zeroOwner;
    AssetBlob zeroSize = AssetBlob::FromOwnedBytes(constZeroOwner, "empty.bin");
    assert(zeroSize.IsValid());
    assert(zeroSize.IsEmpty());
    assert(zeroSize.GetSize() == 0);
    assert(zeroSize.GetSpan().empty());

    AssetBlob zeroSubBlob = zeroSize.TryCreateSubBlob(0, 0);
    assert(zeroSubBlob.IsValid());
    assert(zeroSubBlob.IsEmpty());

    AssetBlob invalidSubBlob = moved.TryCreateSubBlob(5, 1);
    assert(!invalidSubBlob.IsValid());

    std::cout << "AssetBlobLifetimeTest passed\n";
    return 0;
}
