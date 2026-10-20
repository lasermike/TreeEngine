#pragma once
#include "SceneCatalog.h"
#include "SceneDefinition.h"

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

    HRESULT InitializeScenes(const std::string& contentDirectory);
    HRESULT RefreshScenes();
    const SceneData::SceneCatalog& Catalog() const { return m_catalog; }
    HRESULT Load(const char* name, SceneRoot* scene, SceneRenderSettings* renderSettings, Player* player, GameData* gameData,
        SceneData::SceneDefinition* definition = nullptr, std::string* filename = nullptr);
    HRESULT Load(int sceneNum, SceneRoot* scene, SceneRenderSettings* renderSettings, Player* player, GameData* gameData,
        SceneData::SceneDefinition* definition = nullptr, std::string* filename = nullptr);
    void Regenerate(SceneRoot* pScene);

    int _currentSeed;
    int GetNumScenes();
    const char* GetSceneName(int sceneNum) const;
    const std::string& GetLastError() const { return m_lastError; }

private:
    std::string m_lastError;
    SceneData::SceneCatalog m_catalog;
};
