#pragma once
#include "OrbitCamera.h"

class SceneRoot;
struct RenderData;

class GameLoader
{
	std::vector<unsigned int> _seeds;

public:
	GameLoader();

	void Load(char* name, SceneRoot* pScene, RenderData* pRenderData, XSF::OrbitCamera* pCamera);
	void Regenerate(SceneRoot* pScene);

	int						  _currentSeed;
};
