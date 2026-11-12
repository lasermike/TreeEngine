#include "pch.h"
#include "GameLoader.h"
#include "Tree.h"
#include "Primitive.h"
#include "SceneRoot.h"
#include "LSystemModelGenerator.h"
#include <time.h>
#include "Player.h"
#include "Graph.h"
#include <directxcolors.h>
#include "SceneDefinition.h"
#include "SceneInstantiation.h"
#include "SceneRenderSettings.h"
#include <stdexcept>
#include "SceneLoadProfile.h"

void GameData::ResetToDefaults()
{
    useShadowMaps = true;
    useAlphaBlendedRenderTarget = true;
    XMStoreFloat4(&clearColor, Colors::SkyBlue);
}

GameLoader::GameLoader()
{
    _currentSeed = 0;
}

namespace
{
    std::string SceneFilename(const std::string& filename)
    {
        return SceneData::ResolveSceneAsset(filename, {});
    }
}

void GameLoader::Regenerate(SceneRoot* pScene, SceneLoadProfile* profile)
{
    if ((unsigned int)_currentSeed + 1 > _seeds.size())
    {
        _seeds.push_back(0); //(unsigned int)time(NULL));
    }

    int treeNum = 0;
    for (WorldObject* obj : pScene->Children())
    //for (auto& t = pScene->Children().begin(); t != pScene->Children().end(); t++)
    {
        treeNum++;

        // Instances reuse their source's model.
        if (obj->IsInstance())
            continue;

        SceneLoadScope objectTimer(profile, "generation.object/" + obj->GetParams().name);

        GeneratorType genType = obj->GetParams().generatorType;
        if (genType == LSystemGeneratorType)
        {
            WorldObjectParameters<LSystemParams>& wop = obj->GetParams<LSystemParams>();

            LSystemModelGenerator generater(wop.GetGeneratorParameters(), profile);
            obj->Create(&generater);
        }
        else if (genType == FixedTreeGeneratorType)
        {
            FixedTreeModelGenerator generator(_seeds[_currentSeed] * treeNum);
            obj->Create(&generator);
        }
        else if (genType == PrimitiveGeneratorType)
        {
            PrimitiveModelGenerator planeGen(obj->GetParams().primitiveType);
            obj->Create(&planeGen);
        }
        else if (genType == GraphGeneratorType)
        {
            WorldObjectParameters<GraphParams>& wop = obj->GetParams<GraphParams>();
            GraphModelGenerator graphGen(wop.GetGeneratorParameters());
            obj->Create(&graphGen);
        }
        else if (genType == ModelGeneratorType)
        {
            continue;
        }
        else
        {
            ASSERT(0);
        }
    }

}

namespace
{
    SceneData::SceneDefinition LoadJsonScene(
        const char* filename,
        SceneRoot* scene,
        SceneRenderSettings* renderSettings,
        Player* player,
        GameData* gameData)
    {
        auto definition = SceneData::ReadScene(SceneFilename(filename));
#if ALT_POSITION
        if (definition.id == "LoadTreeScene" || definition.id == "LoadAITree")
        {
            definition.camera.position = XMFLOAT3(0, 1.5f, -5);
            definition.camera.rotation = XMFLOAT4(0, 0, 0, 1);
        }
#endif
        InstantiateScene(definition, *scene, *renderSettings, *player, *gameData, filename);
        return definition;
    }
}

int GameLoader::GetNumScenes()
{
    return static_cast<int>(m_catalog.Entries().size());
}

const char* GameLoader::GetSceneName(int sceneNum) const
{
    return sceneNum >= 0 && sceneNum < static_cast<int>(m_catalog.Entries().size())
        ? m_catalog.Entries()[sceneNum].name.c_str() : nullptr;
}

HRESULT GameLoader::InitializeScenes(const std::string& contentDirectory)
{
    m_catalog = SceneData::SceneCatalog(contentDirectory + "\\Scenes", contentDirectory + "\\UserScenes");
    return RefreshScenes();
}

HRESULT GameLoader::RefreshScenes()
{
    m_lastError.clear();
    m_catalog.Refresh();
    if (m_catalog.Entries().empty())
    {
        m_lastError = "No valid scenes were found in Scenes or UserScenes beside the executable.";
        OutputDebugStringA((m_lastError + "\n").c_str());
        return E_FAIL;
    }
    return S_OK;
}

HRESULT GameLoader::Load(int sceneNum, SceneRoot* scene, SceneRenderSettings* renderSettings, Player* player, GameData* gameData,
    SceneData::SceneDefinition* definition, std::string* filename)
{
    m_lastError.clear();
    if (sceneNum < 0 || sceneNum >= GetNumScenes())
    {
        m_lastError = "Scene index is outside the catalog";
        return E_INVALIDARG;
    }
    return Load(m_catalog.Entries()[sceneNum].filename.c_str(), scene, renderSettings, player, gameData, definition, filename);
}

HRESULT GameLoader::Load(const char* name, SceneRoot* scene, SceneRenderSettings* renderSettings, Player* player, GameData* gameData,
    SceneData::SceneDefinition* definition, std::string* filename)
{
    m_lastError.clear();
    if (!name || !*name)
    {
        m_lastError = "A scene name or JSON filename is required";
        return E_INVALIDARG;
    }
    try
    {
        const int index = m_catalog.Find(name);
        const auto source = index >= 0 ? m_catalog.Entries()[index].filename : SceneFilename(name);
        auto parsed = LoadJsonScene(source.c_str(), scene, renderSettings, player, gameData);
        if (definition) *definition = std::move(parsed);
        if (filename) *filename = source;
        return S_OK;
    }
    catch (const std::exception& error)
    {
        m_lastError = error.what();
        OutputDebugStringA((m_lastError + "\n").c_str());
        return E_FAIL;
    }
}
