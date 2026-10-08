#include "Input/IInputBindingStore.h"
#include "Input/InputBindingJson.h"
#include "Container/VariableArray.h"
#include <cwchar>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#endif

namespace NorvesLib::Core::Input
{
#if defined(_WIN32)
    namespace
    {
        struct NativeHandle
        {
            HANDLE Value=INVALID_HANDLE_VALUE;
            explicit NativeHandle(HANDLE value=INVALID_HANDLE_VALUE):Value(value) {}
            NativeHandle(const NativeHandle&)=delete;
            NativeHandle& operator=(const NativeHandle&)=delete;
            ~NativeHandle() { (void)Close(); }
            bool Close()
            {
                if(Value==INVALID_HANDLE_VALUE) return true;
                const HANDLE handle=Value;Value=INVALID_HANDLE_VALUE;
                return CloseHandle(handle)!=0;
            }
        };
        struct PendingTemporary
        {
            PendingTemporary()=default;
            PendingTemporary(const PendingTemporary&)=delete;
            PendingTemporary& operator=(const PendingTemporary&)=delete;
            Container::WideString Path;
            bool Owned=false;
            ~PendingTemporary() { if(Owned && !Path.empty()) DeleteFileW(Path.c_str()); }
        };
        InputBindingStoreLoadResult LoadError(const char* message,uint32_t error=0)
        {
            InputBindingStoreLoadResult result;result.Error=message;result.NativeError=error;return result;
        }
        InputBindingStoreSaveResult SaveError(const char* message,uint32_t error=0)
        {
            InputBindingStoreSaveResult result;result.Error=message;result.NativeError=error;return result;
        }
        Container::WideString ResolveWorkingPath(uint32_t& error)
        {
            // 相対pathの解決はこの1回だけ。後のprocess CWD変更に追従させない。
            Container::VariableArray<wchar_t> buffer(32768);
            const DWORD count=GetFullPathNameW(L"InputBindings.json",static_cast<DWORD>(buffer.size()),buffer.data(),nullptr);
            if(count==0) { error=GetLastError();return {}; }
            if(count>=buffer.size()) { error=ERROR_FILENAME_EXCED_RANGE;return {}; }
            Container::WideString path;path.append(buffer.data(),count);
            if(path.size()>=4 && path[0]==L'\\' && path[1]==L'\\' && path[2]==L'?' && path[3]==L'\\') return path;
            Container::WideString extended;
            if(path.size()>=2 && path[0]==L'\\' && path[1]==L'\\')
            {
                extended=L"\\\\?\\UNC\\";extended.append(path.data()+2,path.size()-2);
            }
            else
            {
                extended=L"\\\\?\\";extended.append(path);
            }
            if(extended.size()>=32767) { error=ERROR_FILENAME_EXCED_RANGE;return {}; }
            return extended;
        }
        class WorkingDirectoryInputBindingStore final : public IInputBindingStore
        {
        public:
            WorkingDirectoryInputBindingStore() { m_Path=ResolveWorkingPath(m_PathError); }
            InputBindingStoreLoadResult Load() override
            {
                if(m_Path.empty()) return LoadError("入力設定の保存先を解決できません",m_PathError);
                NativeHandle file(CreateFileW(m_Path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,
                    nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
                if(file.Value==INVALID_HANDLE_VALUE)
                {
                    const DWORD error=GetLastError();
                    if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND)
                    {
                        InputBindingStoreLoadResult result;result.Status=EInputBindingStoreLoadStatus::Missing;return result;
                    }
                    return LoadError("入力設定ファイルを開けません",error);
                }
                if(GetFileType(file.Value)!=FILE_TYPE_DISK) return LoadError("入力設定が通常ファイルではありません",ERROR_INVALID_DATA);
                LARGE_INTEGER size{};
                if(!GetFileSizeEx(file.Value,&size)) return LoadError("入力設定のサイズを取得できません",GetLastError());
                if(size.QuadPart<0 || static_cast<uint64_t>(size.QuadPart)>InputBindingJson::MaximumTextBytes)
                    return LoadError("入力設定がサイズ上限を超えています",ERROR_FILE_TOO_LARGE);
                Container::VariableArray<char> bytes(static_cast<size_t>(size.QuadPart));
                size_t offset=0;
                while(offset<bytes.size())
                {
                    DWORD read=0;
                    if(!ReadFile(file.Value,bytes.data()+offset,static_cast<DWORD>(bytes.size()-offset),&read,nullptr))
                        return LoadError("入力設定を読み込めません",GetLastError());
                    if(read==0) return LoadError("入力設定が読み込み途中で終わりました",ERROR_HANDLE_EOF);
                    offset+=read;
                }
                char extra=0;DWORD read=0;
                if(!ReadFile(file.Value,&extra,1,&read,nullptr)) return LoadError("入力設定の終端を確認できません",GetLastError());
                if(read!=0) return LoadError("入力設定のサイズが読込中に変わりました",ERROR_INVALID_DATA);
                InputBindingStoreLoadResult result;result.Status=EInputBindingStoreLoadStatus::Loaded;
                if(!bytes.empty()) result.Text.append(bytes.data(),bytes.size());
                return result;
            }
            InputBindingStoreSaveResult Save(const Container::String& text) override
            {
                if(m_Path.empty()) return SaveError("入力設定の保存先を解決できません",m_PathError);
                if(text.empty() || text.size()>InputBindingJson::MaximumTextBytes) return SaveError("保存textが空またはサイズ上限外です");
                // 宣言順により、失敗時はhandleを閉じてから自分のtempだけを消す。
                PendingTemporary temporary;
                NativeHandle file;
                for(unsigned attempt=0;attempt<64;++attempt)
                {
                    wchar_t suffix[96]{};
                    const int count=std::swprintf(suffix,sizeof(suffix)/sizeof(suffix[0]),L".tmp.%lu.%llu.%llu",
                        static_cast<unsigned long>(GetCurrentProcessId()),static_cast<unsigned long long>(GetTickCount64()),
                        static_cast<unsigned long long>(++m_Sequence));
                    if(count<0 || static_cast<size_t>(count)>=sizeof(suffix)/sizeof(suffix[0]) || m_Path.size()+static_cast<size_t>(count)>=32767) return SaveError("一時保存pathが長すぎます",ERROR_FILENAME_EXCED_RANGE);
                    Container::WideString candidate=m_Path;candidate.append(suffix,static_cast<size_t>(count));
                    const HANDLE handle=CreateFileW(candidate.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
                    if(handle!=INVALID_HANDLE_VALUE)
                    {
                        file.Value=handle;temporary.Path=std::move(candidate);temporary.Owned=true;break;
                    }
                    const DWORD error=GetLastError();
                    if(error!=ERROR_FILE_EXISTS && error!=ERROR_ALREADY_EXISTS) return SaveError("一時保存ファイルを作成できません",error);
                }
                if(file.Value==INVALID_HANDLE_VALUE) return SaveError("一時保存ファイル名の衝突を解消できません",ERROR_FILE_EXISTS);
                size_t offset=0;
                while(offset<text.size())
                {
                    DWORD written=0;
                    if(!WriteFile(file.Value,text.data()+offset,static_cast<DWORD>(text.size()-offset),&written,nullptr))
                        return SaveError("入力設定の書き込みに失敗しました",GetLastError());
                    if(written==0) return SaveError("入力設定の書き込みが進みません",ERROR_WRITE_FAULT);
                    offset+=written;
                }
                if(!FlushFileBuffers(file.Value)) return SaveError("入力設定をflushできません",GetLastError());
                if(!file.Close()) return SaveError("一時保存ファイルを閉じられません",GetLastError());
                // 同directory内の置換。COPY_ALLOWEDを付けず、元targetを先にtruncateしない。
                if(!MoveFileExW(temporary.Path.c_str(),m_Path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                    return SaveError("入力設定ファイルを置換できません",GetLastError());
                temporary.Owned=false;
                InputBindingStoreSaveResult result;result.Success=true;return result;
            }
        private:
            uint32_t m_PathError=0;
            Container::WideString m_Path;
            uint64_t m_Sequence=0;
        };
    }
#endif
    Container::TUniquePtr<IInputBindingStore> CreateWorkingDirectoryInputBindingStore()
    {
#if defined(_WIN32)
        return Container::MakeUnique<WorkingDirectoryInputBindingStore>();
#else
        return {};
#endif
    }
}
