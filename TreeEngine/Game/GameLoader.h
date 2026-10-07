#pragma once

class SceneRoot;
struct SceneRenderSettings;
class Player;

struct GameData
{
    bool useShadowMaps;
    bool useAlphaBlendedRenderTarget;
    XMFLOAT4 clearColor;

    GameData()
    {
        ResetToDefaults();
    }

    void ResetToDefaults();
};

class GameLoader
{
    std::vector<unsigned int> _seeds;

public:
    GameLoader();

    HRESULT Load(const char* name, SceneRoot* scene, SceneRenderSettings* renderSettings, Player* player, GameData* gameData);
    HRESULT Load(int sceneNum, SceneRoot* scene, SceneRenderSettings* renderSettings, Player* player, GameData* gameData);
    void Regenerate(SceneRoot* pScene);

    int _currentSeed;
    int GetNumScenes();
    const char* GetSceneName(int sceneNum) const;
    const std::string& GetLastError() const { return m_lastError; }

private:
    std::string m_lastError;
};
