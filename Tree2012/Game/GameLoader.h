#pragma once
#include "OrbitCamera.h"

class SceneRoot;
struct RenderData;
class Player;

class GameLoader
{
	std::vector<unsigned int> _seeds;

	void LoadTrees(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer);
	void LoadTrees2(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer);
	void LoadTrees3(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer);
	void LoadTestBlock(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer);

	void LoadGraph(SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer);

public:
	GameLoader();

	void Load(char* name, SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer);
	void Load(int sceneNum, SceneRoot* pScene, RenderData* pRenderData, Player* pPlayer);
	void Regenerate(SceneRoot* pScene);

	int						  _currentSeed;
	int GetNumScenes() { return 3; }
};
