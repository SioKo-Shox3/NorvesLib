#pragma once

#include "Container/String.h"

namespace NorvesLib::Core::Platform
{
    /** @brief 利用者のデータ保存先を作成し、末尾が逆斜線の UTF-8 パスを返す。失敗時は空。 */
    Container::String GetUserDataDirectory(const Container::String& appName);

    /** @brief 指定した根の下に NorvesLib とアプリのディレクトリを作成する。失敗時は空。 */
    Container::String GetUserDataDirectoryUnder(const Container::String& rootDirectory, const Container::String& appName);
} // namespace NorvesLib::Core::Platform
