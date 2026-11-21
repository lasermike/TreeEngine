#pragma once
#include <chrono>
#include <map>
#include <string>

// Inclusive wall-clock scopes, collected only during a profiled scene load on the loading thread.
// Parent and child scopes overlap; sum siblings, not every entry in the table.
struct SceneLoadProfile
{
    struct Sample
    {
        double milliseconds = 0;
        unsigned calls = 0;
    };
    bool enabled = false;
    std::map<std::string, Sample> scopes;
};

class SceneLoadScope
{
    SceneLoadProfile* m_profile;
    std::string m_name;
    std::chrono::steady_clock::time_point m_start;
public:
    SceneLoadScope(SceneLoadProfile* profile, const std::string& name)
        : m_profile(profile && profile->enabled ? profile : nullptr), m_name(name)
    {
        if (m_profile) m_start = std::chrono::steady_clock::now();
    }
    ~SceneLoadScope()
    {
        Finish();
    }
    void Finish()
    {
        if (!m_profile) return;
        const double elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - m_start).count();
        auto& sample = m_profile->scopes[m_name];
        sample.milliseconds += elapsed;
        ++sample.calls;
        m_profile = nullptr;
    }
    SceneLoadScope(const SceneLoadScope&) = delete;
    SceneLoadScope& operator=(const SceneLoadScope&) = delete;
};
