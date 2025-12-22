#include "AsyncFileStream.h"
#include "Thread/Public/JobSystem.h"
#include <algorithm>

namespace NorvesLib::FileStream
{

    AsyncFileStream::~AsyncFileStream()
    {
        CancelAllOperations();
        WaitForAllOperations();
        Close();
    }

    AsyncFileStreamPtr AsyncFileStream::Create(const String &filePath, FileMode mode,
                                               FileAccess access, FileShare share)
    {
        auto asyncStream = MakeShared<AsyncFileStream>();
        if (asyncStream->Open(filePath, mode, access, share))
        {
            return asyncStream;
        }
        return nullptr;
    }

    AsyncFileStreamUniquePtr AsyncFileStream::CreateUnique(const String &filePath, FileMode mode,
                                                           FileAccess access, FileShare share)
    {
        auto asyncStream = MakeUnique<AsyncFileStream>();
        if (asyncStream->Open(filePath, mode, access, share))
        {
            return asyncStream;
        }
        return nullptr;
    }

    bool AsyncFileStream::Open(const String &filePath, FileMode mode,
                               FileAccess access, FileShare share)
    {
        NorvesLib::Thread::ScopedLock lock(m_mutex);

        m_fileStream = FileStream::Create(filePath, mode, access, share);
        return m_fileStream != nullptr && m_fileStream->IsOpen();
    }

    void AsyncFileStream::Close()
    {
        NorvesLib::Thread::ScopedLock lock(m_mutex);

        if (m_fileStream)
        {
            m_fileStream->Close();
            m_fileStream.reset();
        }
    }

    bool AsyncFileStream::IsOpen() const
    {
        NorvesLib::Thread::ScopedLock lock(m_mutex);
        return m_fileStream && m_fileStream->IsOpen();
    }

    AsyncReadTask AsyncFileStream::ReadAsync(void *buffer, size_t size,
                                             std::function<void(const AsyncReadResult &)> callback)
    {
        if (!IsOpen() || !buffer || size == 0)
        {
            if (callback)
            {
                AsyncReadResult result;
                result.bSuccess = false;
                result.errorMessage = "Invalid parameters or file not open";
                callback(result);
            }
            return nullptr;
        }

        auto task = NorvesLib::Thread::Task::Create([this, buffer, size, callback]()
                                                    {
            AsyncReadResult result;
            
            if (m_bCancelling)
            {
                result.bSuccess = false;
                result.errorMessage = "Operation cancelled";
            }
            else
            {
                NorvesLib::Thread::ScopedLock lock(m_mutex);
                
                if (m_fileStream && m_fileStream->IsOpen())
                {
                    result.bytesRead = m_fileStream->Read(buffer, size);
                    result.bSuccess = true;
                }
                else
                {
                    result.bSuccess = false;
                    result.errorMessage = "File stream is not available";
                }
            }
              if (callback)
            {
                callback(result);
            } });

        AddActiveTask(task);
        NorvesLib::Thread::JobSystem::Get().SubmitTask(task);

        return task;
    }

    AsyncWriteTask AsyncFileStream::WriteAsync(const void *buffer, size_t size,
                                               std::function<void(const AsyncWriteResult &)> callback)
    {
        if (!IsOpen() || !buffer || size == 0)
        {
            if (callback)
            {
                AsyncWriteResult result;
                result.bSuccess = false;
                result.errorMessage = "Invalid parameters or file not open";
                callback(result);
            }
            return nullptr;
        }

        auto task = NorvesLib::Thread::Task::Create([this, buffer, size, callback]()
                                                    {
            AsyncWriteResult result;
            
            if (m_bCancelling)
            {
                result.bSuccess = false;
                result.errorMessage = "Operation cancelled";
            }
            else
            {
                NorvesLib::Thread::ScopedLock lock(m_mutex);
                
                if (m_fileStream && m_fileStream->IsOpen())
                {
                    result.bytesWritten = m_fileStream->Write(buffer, size);
                    result.bSuccess = result.bytesWritten == size;
                    if (!result.bSuccess)
                    {
                        result.errorMessage = "Failed to write all data";
                    }
                }
                else
                {
                    result.bSuccess = false;
                    result.errorMessage = "File stream is not available";
                }
            }
            
            if (callback)
            {
                callback(result);
            } });
        AddActiveTask(task);
        NorvesLib::Thread::JobSystem::Get().SubmitTask(task);

        return task;
    }

    AsyncReadTask AsyncFileStream::ReadStringAsync(std::function<void(const String &, bool)> callback)
    {
        if (!IsOpen())
        {
            if (callback)
            {
                callback(String{}, false);
            }
            return nullptr;
        }

        auto task = NorvesLib::Thread::Task::Create([this, callback]()
                                                    {
            String result;
            bool bSuccess = false;
            
            if (!m_bCancelling)
            {
                NorvesLib::Thread::ScopedLock lock(m_mutex);
                
                if (m_fileStream && m_fileStream->IsOpen())
                {
                    result = m_fileStream->ReadString();
                    bSuccess = true;
                }
            }
            
            if (callback)
            {
                callback(result, bSuccess);        } });

        AddActiveTask(task);
        NorvesLib::Thread::JobSystem::Get().SubmitTask(task);

        return task;
    }

    AsyncWriteTask AsyncFileStream::WriteStringAsync(const String &str,
                                                     std::function<void(const AsyncWriteResult &)> callback)
    {
        if (!IsOpen())
        {
            if (callback)
            {
                AsyncWriteResult result;
                result.bSuccess = false;
                result.errorMessage = "File not open";
                callback(result);
            }
            return nullptr;
        }

        auto task = NorvesLib::Thread::Task::Create([this, str, callback]()
                                                    {
            AsyncWriteResult result;
            
            if (m_bCancelling)
            {
                result.bSuccess = false;
                result.errorMessage = "Operation cancelled";
            }
            else
            {
                NorvesLib::Thread::ScopedLock lock(m_mutex);
                
                if (m_fileStream && m_fileStream->IsOpen())
                {
                    result.bytesWritten = m_fileStream->WriteString(str);
                    result.bSuccess = result.bytesWritten == str.length();
                    if (!result.bSuccess)
                    {
                        result.errorMessage = "Failed to write all string data";
                    }
                }
                else
                {
                    result.bSuccess = false;
                    result.errorMessage = "File stream is not available";
                }
            }
            
            if (callback)
            {
                callback(result);
            } });
        AddActiveTask(task);
        NorvesLib::Thread::JobSystem::Get().SubmitTask(task);

        return task;
    }

    AsyncReadTask AsyncFileStream::ReadLineAsync(std::function<void(const String &, bool)> callback)
    {
        if (!IsOpen())
        {
            if (callback)
            {
                callback(String{}, false);
            }
            return nullptr;
        }

        auto task = NorvesLib::Thread::Task::Create([this, callback]()
                                                    {
            String result;
            bool bSuccess = false;
            
            if (!m_bCancelling)
            {
                NorvesLib::Thread::ScopedLock lock(m_mutex);
                
                if (m_fileStream && m_fileStream->IsOpen())
                {
                    result = m_fileStream->ReadLine();
                    bSuccess = true;
                }
            }
            
            if (callback)
            {
                callback(result, bSuccess);
            } });

        AddActiveTask(task);
        NorvesLib::Thread::JobSystem::Get().SubmitTask(task);

        return task;
    }

    AsyncWriteTask AsyncFileStream::WriteLineAsync(const String &line,
                                                   std::function<void(const AsyncWriteResult &)> callback)
    {
        if (!IsOpen())
        {
            if (callback)
            {
                AsyncWriteResult result;
                result.bSuccess = false;
                result.errorMessage = "File not open";
                callback(result);
            }
            return nullptr;
        }

        auto task = NorvesLib::Thread::Task::Create([this, line, callback]()
                                                    {
            AsyncWriteResult result;
            
            if (m_bCancelling)
            {
                result.bSuccess = false;
                result.errorMessage = "Operation cancelled";
            }
            else
            {
                NorvesLib::Thread::ScopedLock lock(m_mutex);
                
                if (m_fileStream && m_fileStream->IsOpen())
                {
                    result.bytesWritten = m_fileStream->WriteLine(line);
                    result.bSuccess = result.bytesWritten > 0;
                    if (!result.bSuccess)
                    {
                        result.errorMessage = "Failed to write line";
                    }
                }
                else
                {
                    result.bSuccess = false;
                    result.errorMessage = "File stream is not available";
                }
            }
            
            if (callback)
            {
                callback(result);
            } });
        AddActiveTask(task);
        NorvesLib::Thread::JobSystem::Get().SubmitTask(task);

        return task;
    }

    int64_t AsyncFileStream::GetSize() const
    {
        NorvesLib::Thread::ScopedLock lock(m_mutex);

        if (m_fileStream && m_fileStream->IsOpen())
        {
            return m_fileStream->GetSize();
        }
        return -1;
    }

    void AsyncFileStream::WaitForAllOperations()
    {
        RemoveCompletedTasks();

        NorvesLib::Thread::ScopedLock lock(m_mutex);

        for (auto &task : m_activeTasks)
        {
            if (task)
            {
                task->Wait();
            }
        }
        m_activeTasks.clear();
    }

    void AsyncFileStream::CancelAllOperations()
    {
        m_bCancelling = true;

        NorvesLib::Thread::ScopedLock lock(m_mutex);

        for (auto &task : m_activeTasks)
        {
            if (task)
            {
                task->Cancel();
            }
        }
    }

    void AsyncFileStream::AddActiveTask(const NorvesLib::Thread::TaskPtr &task)
    {
        NorvesLib::Thread::ScopedLock lock(m_mutex);
        m_activeTasks.push_back(task);
    }

    void AsyncFileStream::RemoveCompletedTasks()
    {
        NorvesLib::Thread::ScopedLock lock(m_mutex);

        m_activeTasks.erase(
            std::remove_if(m_activeTasks.begin(), m_activeTasks.end(),
                           [](const NorvesLib::Thread::TaskPtr &task)
                           {
                               return !task || task->IsCompleted();
                           }),
            m_activeTasks.end());
    }

} // namespace NorvesLib::FileStream
