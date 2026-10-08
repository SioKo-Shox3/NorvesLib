// Resource基底クラスと読込APIを同じ翻訳単位で利用できることを検証する。
#include "Object/Resource.h"
#include "Resource/GLTFAnalyzer.h"
#include "Animation/SkeletonResource.h"
#include <type_traits>

static_assert(std::is_class_v<NorvesLib::Core::Resource>);
static_assert(std::is_class_v<NorvesLib::Core::ResourceIO::GLTFAnalyzer>);
static_assert(std::is_base_of_v<NorvesLib::Core::Resource,
    NorvesLib::Core::SkeletonResource>);

int main()
{
    return 0;
}
