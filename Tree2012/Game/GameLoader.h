#pragma once

class SceneRoot;

class GameLoader
{
public:
	GameLoader() { }

	void Load(char* name, SceneRoot* pScene);
};
