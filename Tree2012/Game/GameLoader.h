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

	void LoadTrees(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer, GameData* gameData);
	void LoadTrees2(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer, GameData* gameData);
	void LoadTrees3(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer, GameData* gameData);
    //void LoadNewTrees(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData);
	void LoadTestBlock(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer, GameData* gameData);
	void LoadFSGraph(SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData);

	void LoadGraph(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer, GameData* gameData);

public:
	GameLoader();

	void Load(char* name, SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData);
	void Load(int sceneNum, SceneRoot* scene, RenderData* renderData, Player* player, GameData* gameData);
	void Regenerate(SceneRoot* pScene);

	int						  _currentSeed;
	int GetNumScenes() { return 4; }
};
