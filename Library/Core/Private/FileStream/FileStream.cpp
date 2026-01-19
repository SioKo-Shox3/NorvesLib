#include "FileStream/FileStream.h"
#include <iostream>
#include <sstream>

namespace NorvesLib::FileStream
{

    FileStream::~FileStream()
    {
        if (m_bIsOpen)
        {
            Close();
        }
    }

    FileStream::FileStream(FileStream &&other) noexcept
        : m_stream(std::move(other.m_stream)), m_filePath(std::move(other.m_filePath)), m_mode(other.m_mode), m_access(other.m_access), m_bIsOpen(other.m_bIsOpen)
    {
        other.m_bIsOpen = false;
    }

    FileStream &FileStream::operator=(FileStream &&other) noexcept
    {
        if (this != &other)
        {
            if (m_bIsOpen)
            {
                Close();
            }

            m_stream = std::move(other.m_stream);
            m_filePath = std::move(other.m_filePath);
            m_mode = other.m_mode;
            m_access = other.m_access;
            m_bIsOpen = other.m_bIsOpen;

            other.m_bIsOpen = false;
        }
        return *this;
    }

    FileStreamPtr FileStream::Create(const String &filePath, FileMode mode, FileAccess access, FileShare share)
    {
        auto fileStream = MakeShared<FileStream>();
        if (fileStream->Open(filePath, mode, access, share))
        {
            return fileStream;
        }
        return nullptr;
    }

    FileStreamUniquePtr FileStream::CreateUnique(const String &filePath, FileMode mode, FileAccess access, FileShare share)
    {
        auto fileStream = MakeUnique<FileStream>();
        if (fileStream->Open(filePath, mode, access, share))
        {
            return fileStream;
        }
        return nullptr;
    }
    bool FileStream::Open(const String &filePath, FileMode mode, FileAccess access, FileShare share)
    {
        // shareパラメータは現在未使用ですが、将来の拡張のために保持
        (void)share;

        if (m_bIsOpen)
        {
            Close();
        }

        m_filePath = filePath;
        m_mode = mode;
        m_access = access;

        auto openMode = ConvertToStdMode(mode, access);
        m_stream.open(filePath.c_str(), openMode);

        m_bIsOpen = m_stream.is_open();
        return m_bIsOpen;
    }

    void FileStream::Close()
    {
        if (m_bIsOpen)
        {
            m_stream.close();
            m_bIsOpen = false;
        }
    }

    bool FileStream::IsOpen() const
    {
        return m_bIsOpen && m_stream.is_open();
    }

    size_t FileStream::Read(void *buffer, size_t size)
    {
        if (!IsOpen() || !buffer)
        {
            return 0;
        }

        m_stream.read(static_cast<char *>(buffer), static_cast<std::streamsize>(size));
        return static_cast<size_t>(m_stream.gcount());
    }

    size_t FileStream::Write(const void *buffer, size_t size)
    {
        if (!IsOpen() || !buffer)
        {
            return 0;
        }

        auto currentPos = m_stream.tellp();
        m_stream.write(static_cast<const char *>(buffer), static_cast<std::streamsize>(size));

        if (m_stream.good())
        {
            return size;
        }

        return static_cast<size_t>(m_stream.tellp() - currentPos);
    }

    int64_t FileStream::Seek(int64_t offset, SeekOrigin origin)
    {
        if (!IsOpen())
        {
            return -1;
        }

        std::ios_base::seekdir dir;
        switch (origin)
        {
        case SeekOrigin::Begin:
            dir = std::ios_base::beg;
            break;
        case SeekOrigin::Current:
            dir = std::ios_base::cur;
            break;
        case SeekOrigin::End:
            dir = std::ios_base::end;
            break;
        default:
            return -1;
        }

        if (m_access == FileAccess::Read)
        {
            m_stream.seekg(offset, dir);
            return static_cast<int64_t>(m_stream.tellg());
        }
        else
        {
            m_stream.seekp(offset, dir);
            return static_cast<int64_t>(m_stream.tellp());
        }
    }

    int64_t FileStream::Tell() const
    {
        if (!IsOpen())
        {
            return -1;
        }

        if (m_access == FileAccess::Read)
        {
            return static_cast<int64_t>(m_stream.tellg());
        }
        else
        {
            return static_cast<int64_t>(m_stream.tellp());
        }
    }
    int64_t FileStream::GetSize() const
    {
        if (!IsOpen())
        {
            return -1;
        }

        auto currentPos = Tell();
        const_cast<FileStream *>(this)->Seek(0, SeekOrigin::End);
        auto size = Tell();
        const_cast<FileStream *>(this)->Seek(currentPos, SeekOrigin::Begin);

        return size;
    }

    bool FileStream::IsEOF() const
    {
        return !IsOpen() || m_stream.eof();
    }

    void FileStream::Flush()
    {
        if (IsOpen())
        {
            m_stream.flush();
        }
    }

    String FileStream::ReadString()
    {
        if (!IsOpen())
        {
            return String{};
        }

        std::ostringstream oss;
        oss << m_stream.rdbuf();
        return String(oss.str());
    }

    String FileStream::ReadLine()
    {
        if (!IsOpen())
        {
            return String{};
        }

        std::string line;
        if (std::getline(m_stream, line))
        {
            return String(line);
        }

        return String{};
    }

    size_t FileStream::WriteString(const String &str)
    {
        if (!IsOpen())
        {
            return 0;
        }

        return Write(str.c_str(), str.length());
    }

    size_t FileStream::WriteLine(const String &line)
    {
        if (!IsOpen())
        {
            return 0;
        }

        String lineWithNewline = line + "\n";
        return Write(lineWithNewline.c_str(), lineWithNewline.length());
    }
    std::ios_base::openmode FileStream::ConvertToStdMode(FileMode mode, FileAccess access) const
    {
        // accessパラメータは現在未使用ですが、将来の拡張のために保持
        (void)access;

        std::ios_base::openmode openMode = std::ios_base::binary;

        switch (mode)
        {
        case FileMode::Read:
            openMode |= std::ios_base::in;
            break;
        case FileMode::Write:
            openMode |= std::ios_base::out | std::ios_base::trunc;
            break;
        case FileMode::Append:
            openMode |= std::ios_base::out | std::ios_base::app;
            break;
        case FileMode::ReadWrite:
            openMode |= std::ios_base::in | std::ios_base::out;
            break;
        }

        return openMode;
    }

} // namespace NorvesLib::FileStream
