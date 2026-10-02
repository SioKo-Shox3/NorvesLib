#include "Input/IInputBindingStore.h"
#include "Input/InputBindingJson.h"
#include "Container/VariableArray.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#if defined(_WIN32)
#include <Windows.h>
#endif
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
#if defined(_WIN32)
namespace
{
    Container::WideString CurrentDirectory()
    {
        Container::VariableArray<wchar_t> buffer(32768);
        const DWORD size=GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()),buffer.data());
        assert(size>0 && size<buffer.size());
        Container::WideString result;result.append(buffer.data(),size);return result;
    }
    Container::WideString Child(const Container::WideString& root,const wchar_t* name)
    {
        Container::WideString result=root;result.append(L"\\");result.append(name);return result;
    }
    Container::WideString CreateRoot()
    {
        wchar_t directory[MAX_PATH]{};const DWORD length=GetTempPathW(MAX_PATH,directory);
        assert(length>0 && length<MAX_PATH);
        wchar_t unique[MAX_PATH]{};assert(GetTempFileNameW(directory,L"nib",0,unique)!=0);
        assert(DeleteFileW(unique));assert(CreateDirectoryW(unique,nullptr));return Container::WideString(unique);
    }
    void NoTemporaryFiles()
    {
        WIN32_FIND_DATAW data{};const HANDLE search=FindFirstFileW(L"InputBindings.json.tmp.*",&data);
        if(search!=INVALID_HANDLE_VALUE) { FindClose(search);assert(false); }
        assert(GetLastError()==ERROR_FILE_NOT_FOUND);
    }
}
#endif
int main()
{
#if defined(_WIN32)
    // 実行者の設定に触らず、このtest専用temporary directory内だけを使う。
    const auto original=CurrentDirectory();const auto root=CreateRoot();
    const auto other=Child(root,L"other"),unicode=Child(root,L"入力");
    assert(CreateDirectoryW(other.c_str(),nullptr));assert(CreateDirectoryW(unicode.c_str(),nullptr));
    assert(SetCurrentDirectoryW(root.c_str()));
    auto store=CreateWorkingDirectoryInputBindingStore();assert(store);
    assert(store->Load().Status==EInputBindingStoreLoadStatus::Missing);
    const Container::String first=R"({"schema":"bindings.v1","contexts":[]})";
    const Container::String second=R"({"schema":"bindings.v1","contexts":[],"future":1})";
    assert(store->Save(first).Success);
    auto loaded=store->Load();assert(loaded.Status==EInputBindingStoreLoadStatus::Loaded && loaded.Text==first);
    assert(store->Save(second).Success);assert(store->Load().Text==second);NoTemporaryFiles();
    assert(!store->Save({}).Success);assert(store->Load().Text==second);
    const Container::String tooLarge(InputBindingJson::MaximumTextBytes+1,'x');
    assert(!store->Save(tooLarge).Success);assert(store->Load().Text==second);NoTemporaryFiles();
    Container::String exact=first;exact.append(InputBindingJson::MaximumTextBytes-exact.size(),' ');
    assert(store->Save(exact).Success);
    loaded=store->Load();assert(loaded.Status==EInputBindingStoreLoadStatus::Loaded && loaded.Text==exact);
    assert(store->Save(second).Success);NoTemporaryFiles();
    // cleanupがglob削除になっていないことを、無関係なtempを残して検証する。
    HANDLE foreign=CreateFileW(L"InputBindings.json.tmp.foreign",GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    assert(foreign!=INVALID_HANDLE_VALUE);const char marker[]="owned-by-test";DWORD markerBytes=0;
    assert(WriteFile(foreign,marker,sizeof(marker),&markerBytes,nullptr) && markerBytes==sizeof(marker));assert(CloseHandle(foreign));
    // targetをdelete共有なしで開いて、置換失敗時に元の内容を保つことを確認する。
    HANDLE locked=CreateFileW(L"InputBindings.json",GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    assert(locked!=INVALID_HANDLE_VALUE);
    const auto failed=store->Save(first);assert(!failed.Success && !failed.Error.empty());
    assert(CloseHandle(locked));assert(store->Load().Text==second);
    foreign=CreateFileW(L"InputBindings.json.tmp.foreign",GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    assert(foreign!=INVALID_HANDLE_VALUE);char preserved[sizeof(marker)]{};DWORD readMarker=0;
    assert(ReadFile(foreign,preserved,sizeof(preserved),&readMarker,nullptr) && readMarker==sizeof(marker));assert(CloseHandle(foreign));
    for(size_t i=0;i<sizeof(marker);++i) assert(preserved[i]==marker[i]);
    assert(DeleteFileW(L"InputBindings.json.tmp.foreign"));NoTemporaryFiles();
    // 読み取り共有も拒否した場合、MissingではなくErrorを返す。
    locked=CreateFileW(L"InputBindings.json",GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    assert(locked!=INVALID_HANDLE_VALUE);assert(store->Load().Status==EInputBindingStoreLoadStatus::Error);assert(CloseHandle(locked));
    // Store生成後にCWDを変更しても、当初の絶対pathへ保存する。
    assert(SetCurrentDirectoryW(other.c_str()));assert(store->Save(first).Success);
    auto otherStore=CreateWorkingDirectoryInputBindingStore();assert(otherStore->Load().Status==EInputBindingStoreLoadStatus::Missing);
    assert(SetCurrentDirectoryW(root.c_str()));assert(store->Load().Text==first);
    // サイズ上限はread buffer確保より前に拒否する。
    HANDLE oversized=CreateFileW(L"InputBindings.json",GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    assert(oversized!=INVALID_HANDLE_VALUE);LARGE_INTEGER size{};size.QuadPart=InputBindingJson::MaximumTextBytes+1;
    assert(SetFilePointerEx(oversized,size,nullptr,FILE_BEGIN));assert(SetEndOfFile(oversized));assert(CloseHandle(oversized));
    loaded=store->Load();assert(loaded.Status==EInputBindingStoreLoadStatus::Error && !loaded.Error.empty());
    assert(store->Save(first).Success);
    HANDLE empty=CreateFileW(L"InputBindings.json",GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    assert(empty!=INVALID_HANDLE_VALUE && CloseHandle(empty));
    loaded=store->Load();assert(loaded.Status==EInputBindingStoreLoadStatus::Loaded && loaded.Text.empty());
    assert(store->Save(first).Success);
    assert(SetCurrentDirectoryW(unicode.c_str()));
    auto unicodeStore=CreateWorkingDirectoryInputBindingStore();assert(unicodeStore && unicodeStore->Save(second).Success);
    assert(unicodeStore->Load().Text==second);NoTemporaryFiles();
    unicodeStore.reset();otherStore.reset();store.reset();
    assert(SetCurrentDirectoryW(original.c_str()));
    assert(DeleteFileW(Child(unicode,L"InputBindings.json").c_str()));assert(RemoveDirectoryW(unicode.c_str()));
    assert(RemoveDirectoryW(other.c_str()));
    assert(DeleteFileW(Child(root,L"InputBindings.json").c_str()));assert(RemoveDirectoryW(root.c_str()));
    std::cout << "WorkingDirectoryInputBindingStoreTest passed\n";
    return 0;
#else
    std::cout << "WorkingDirectoryInputBindingStoreTest unsupported platform\n";
    return 125;
#endif
}
