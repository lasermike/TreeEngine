#pragma once
#include "SceneDefinition.h"

class SceneRoot;
class Player;
struct SceneRenderSettings;
struct GameData;

// Build objects first, then apply scene settings. SceneRoot owns the result.
void InstantiateScene(const SceneData::SceneDefinition& definition, SceneRoot& scene,
    SceneRenderSettings& renderSettings, Player& player, GameData& gameData);
