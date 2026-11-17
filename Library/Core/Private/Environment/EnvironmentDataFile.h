// 環境データの JSON ファイルを UTF-8 BOM を除いたバイト列として読み込む。
#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::Core::Environment
{
    /** @brief 根が空なら作業ディレクトリを使う。失敗時は出力バイト列を変更しない。 */
    bool ReadEnvironmentDataFile(const Container::String& rootDirectory, const Container::String& relativePath,
                                 Container::VariableArray<uint8_t>& outBytes, Container::String* pOutError);
} // 名前空間 NorvesLib::Core::Environment
