#pragma once

// Reads byte ranges of an asset file for streaming. FileAssetReader reads with positional Win32 file reads, so several
// threads can read at once; a DirectStorage reader can implement the same interface later (queued requests, GDeflate
// decompression on the GPU) without changing the formats that use it (TerrainPackage).

#include <cstdint>
#include <string>

class IAssetReader
{
public:
    virtual ~IAssetReader() = default;
    // Reads `bytes` bytes at `offset` into `destination`; false on failure or a short read. Safe from several threads.
    virtual bool Read(uint64_t offset, void* destination, size_t bytes) const = 0;
    virtual uint64_t Size() const = 0;
};

class FileAssetReader : public IAssetReader
{
public:
    ~FileAssetReader() override { Close(); }

    bool Open(const std::wstring& path)
    {
        Close();
        m_file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
        LARGE_INTEGER size = {};
        if (m_file == INVALID_HANDLE_VALUE || !GetFileSizeEx(m_file, &size))
        {
            Close();
            return false;
        }
        m_size = static_cast<uint64_t>(size.QuadPart);
        return true;
    }

    void Close()
    {
        if (m_file != INVALID_HANDLE_VALUE)
            CloseHandle(m_file);
        m_file = INVALID_HANDLE_VALUE;
        m_size = 0;
    }

    bool IsOpen() const { return m_file != INVALID_HANDLE_VALUE; }
    uint64_t Size() const override { return m_size; }

    bool Read(uint64_t offset, void* destination, size_t bytes) const override
    {
        if (m_file == INVALID_HANDLE_VALUE || offset + bytes > m_size || bytes > MAXDWORD)
            return false;
        // A synchronous handle with an explicit offset: each call reads where it asks, whatever other threads do.
        OVERLAPPED position = {};
        position.Offset = static_cast<DWORD>(offset);
        position.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD read = 0;
        return ReadFile(m_file, destination, static_cast<DWORD>(bytes), &read, &position) && read == bytes;
    }

private:
    HANDLE m_file = INVALID_HANDLE_VALUE;
    uint64_t m_size = 0;
};
