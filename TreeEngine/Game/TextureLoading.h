#pragma once

#include <objbase.h>
#include <cwchar>

inline bool IsWicTextureFile(const wchar_t* filename)
{
    const wchar_t* extension = filename ? std::wcsrchr(filename, L'.') : nullptr;
    return extension && (_wcsicmp(extension, L".png") == 0
        || _wcsicmp(extension, L".jpg") == 0
        || _wcsicmp(extension, L".jpeg") == 0);
}

// WIC needs COM on the loading thread. Balance only our own initialization,
// and accept an apartment already initialized with a different threading model.
class TextureCOMScope
{
public:
    TextureCOMScope() : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~TextureCOMScope()
    {
        if (SUCCEEDED(m_result))
            CoUninitialize();
    }

    HRESULT Result() const { return m_result == RPC_E_CHANGED_MODE ? S_OK : m_result; }

    TextureCOMScope(const TextureCOMScope&) = delete;
    TextureCOMScope& operator=(const TextureCOMScope&) = delete;

private:
    HRESULT m_result;
};
