#include "pch.h"
#include "SceneCatalog.h"
#include "SceneDefinition.h"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <cwctype>

namespace SceneData
{
    namespace
    {
        std::wstring Lower(const std::string& text)
        {
            auto result = WidePath(text);
            std::transform(result.begin(), result.end(), result.begin(), towlower);
            return result;
        }
    }

    std::string SceneEntry::Label() const
    {
        const auto slash = filename.find_last_of("\\/");
        return name + " [" + (user ? "User" : "Built-in") + ": " + filename.substr(slash + 1) + "]";
    }

    SceneCatalog::SceneCatalog(std::string builtinDirectory, std::string userDirectory)
        : m_builtinDirectory(AbsolutePath(builtinDirectory)), m_userDirectory(AbsolutePath(userDirectory))
    {
    }

    bool SceneCatalog::Scan(const std::string& directory, bool user)
    {
        if (directory.empty()) throw std::runtime_error("Scene directories have not been initialized");
        WIN32_FIND_DATAW data = {};
        HANDLE handle = FindFirstFileW(WidePath(directory + "\\*.scene.json").c_str(), &data);
        if (handle == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || (user && error == ERROR_PATH_NOT_FOUND)) return true;
            m_diagnostics.push_back(directory + ": cannot enumerate scenes (Windows error " + std::to_string(error) + ")");
            return false;
        }
        struct FindHandle { HANDLE value; ~FindHandle() { FindClose(value); } } guard{ handle };
        do
        {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const auto filename = directory + "\\" + Utf8Path(data.cFileName);
            try
            {
                auto scene = ReadScene(filename);
                std::string id = scene.id;
                if (id.empty())
                {
                    id = Utf8Path(data.cFileName);
                    id.resize(id.size() - std::string(".scene.json").size());
                    if (id.empty())
                        throw std::runtime_error(filename + ": filename has no scene ID; provide an explicit 'id'");
                }
                m_entries.push_back({ id, scene.name, filename, user });
            }
            catch (const std::runtime_error& error)
            {
                m_diagnostics.push_back(error.what());
            }
        } while (FindNextFileW(handle, &data));
        DWORD error = GetLastError();
        if (error != ERROR_NO_MORE_FILES)
        {
            m_diagnostics.push_back(directory + ": scene enumeration interrupted (Windows error " + std::to_string(error) + ")");
            return false;
        }
        return true;
    }

    void SceneCatalog::Refresh()
    {
        m_entries.clear();
        m_diagnostics.clear();
        Scan(m_builtinDirectory, false);
        Scan(m_userDirectory, true);
        std::map<std::string, int> counts;
        for (const auto& entry : m_entries) ++counts[entry.Key()];
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&](const SceneEntry& entry) {
            if (counts[entry.Key()] < 2) return false;
            m_diagnostics.push_back(entry.filename + ": duplicate scene ID '" + entry.Key() + "'");
            return true;
        }), m_entries.end());
        std::sort(m_entries.begin(), m_entries.end(), [](const SceneEntry& a, const SceneEntry& b) {
            const auto an = Lower(a.name), bn = Lower(b.name);
            if (an != bn) return an < bn;
            if (a.user != b.user) return a.user < b.user;
            return a.filename < b.filename;
        });
        std::sort(m_diagnostics.begin(), m_diagnostics.end());
        for (const auto& diagnostic : m_diagnostics) OutputDebugStringA((diagnostic + "\n").c_str());
    }

    int SceneCatalog::Find(const std::string& name) const
    {
        if (name.empty()) throw std::runtime_error("A scene name, ID, or filename is required");
        for (size_t i = 0; i < m_entries.size(); ++i)
            if (m_entries[i].Key() == name || (!m_entries[i].user && m_entries[i].id == name))
                return static_cast<int>(i);
        int found = -1;
        for (size_t i = 0; i < m_entries.size(); ++i)
            if (m_entries[i].name == name || (m_entries[i].user && m_entries[i].id == name))
            {
                if (found >= 0) throw std::runtime_error("Ambiguous scene '" + name + "'; use its source-qualified ID or full filename");
                found = static_cast<int>(i);
            }
        return found;
    }

    int SceneCatalog::FindPath(const std::string& filename) const
    {
        const auto path = Lower(AbsolutePath(filename));
        for (size_t i = 0; i < m_entries.size(); ++i)
            if (Lower(m_entries[i].filename) == path) return static_cast<int>(i);
        return -1;
    }

    std::string SceneCatalog::UserFilename(const std::string& stem) const
    {
        const auto wide = WidePath(stem);
        if (wide.empty() || wide.size() > 200 || wide.back() == L'.' || wide.back() == L' ')
            throw std::runtime_error("Scene filename must have 1-200 characters and must not end in a dot or space");
        for (wchar_t c : wide)
            if (c < 32 || std::wstring(L"<>:\"/\\|?*").find(c) != std::wstring::npos)
                throw std::runtime_error("Scene filename contains an invalid Windows filename character");
        auto base = wide.substr(0, wide.find(L'.'));
        while (!base.empty() && base.back() == L' ') base.pop_back();
        std::transform(base.begin(), base.end(), base.begin(), towupper);
        if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL" ||
            base == L"CONIN$" || base == L"CONOUT$" ||
            (base.size() == 4 && (base.substr(0, 3) == L"COM" || base.substr(0, 3) == L"LPT") &&
                ((base[3] >= L'1' && base[3] <= L'9') || base[3] == 0x00b9 || base[3] == 0x00b2 || base[3] == 0x00b3)))
            throw std::runtime_error("Scene filename is a reserved Windows device name");
        return m_userDirectory + "\\" + stem + ".scene.json";
    }

    void SceneCatalog::CheckUserIdentity(const std::string& id, const std::string& filename) const
    {
        SceneCatalog current(m_builtinDirectory, m_userDirectory);
        if (!current.Scan(m_userDirectory, true))
            throw std::runtime_error(current.Diagnostics().front());
        auto effectiveId = id;
        if (effectiveId.empty())
        {
            effectiveId = filename.substr(filename.find_last_of("\\/") + 1);
            effectiveId.resize(effectiveId.size() - std::string(".scene.json").size());
        }
        const auto target = Lower(AbsolutePath(filename));
        for (const auto& entry : current.Entries())
            if (entry.id == effectiveId && Lower(entry.filename) != target)
                throw std::runtime_error(entry.filename + ": another user scene already uses ID '" + effectiveId + "'");
    }

    void SceneCatalog::EnsureUserDirectory() const
    {
        const auto path = WidePath(m_userDirectory);
        if (!CreateDirectoryW(path.c_str(), nullptr))
        {
            DWORD error = GetLastError();
            DWORD attributes = GetFileAttributesW(path.c_str());
            if (error != ERROR_ALREADY_EXISTS || attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                throw std::runtime_error(m_userDirectory + ": cannot create user scene directory (Windows error " + std::to_string(error) + ")");
        }
    }
}
