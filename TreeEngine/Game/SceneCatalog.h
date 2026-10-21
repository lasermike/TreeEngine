#pragma once

#include <string>
#include <vector>

namespace SceneData
{
    struct SceneEntry
    {
        std::string id;
        std::string name;
        std::string filename;
        bool user = false;
        int priority = 0;

        std::string Key() const { return (user ? "user:" : "builtin:") + id; }
        std::string Label() const;
    };

    class SceneCatalog
    {
    public:
        SceneCatalog() = default;
        SceneCatalog(std::string builtinDirectory, std::string userDirectory);
        void Refresh();
        int Find(const std::string& name) const;
        int FindPath(const std::string& filename) const;
        std::string UserFilename(const std::string& stem) const;
        void CheckUserIdentity(const std::string& id, const std::string& filename) const;
        void EnsureUserDirectory() const;

        const std::vector<SceneEntry>& Entries() const { return m_entries; }
        const std::vector<std::string>& Diagnostics() const { return m_diagnostics; }
        const std::string& UserDirectory() const { return m_userDirectory; }

    private:
        std::string m_builtinDirectory;
        std::string m_userDirectory;
        std::vector<SceneEntry> m_entries;
        std::vector<std::string> m_diagnostics;
        bool Scan(const std::string& directory, bool user);
    };
}
