#pragma once
#include "OrbitCamera.h"

class SceneRoot;
struct RenderData;
class Player;

struct GameData
{
    bool useShadowMaps;
    bool useAlphaBlendedRenderTarget;
    XMVECTORF32 clearColor;

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

    void Load(char* name, SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData);
    void Load(int sceneNum, SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData);
    void Regenerate(SceneRoot* pScene);

    int _currentSeed;
    int GetNumScenes() { return 4; }
};
