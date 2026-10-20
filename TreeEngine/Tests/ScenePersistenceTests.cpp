#include "pch.h"
#include "SceneDefinition.h"
#include "SceneCatalog.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

int RunScenePersistenceTests()
{
    using namespace SceneData;
    using Json = nlohmann::json;
    int checks = 0;
    auto check = [&](bool condition, const char* message) {
        ++checks;
        if (!condition) throw std::runtime_error(message);
    };
    auto reject = [&](const std::function<void()>& action) {
        bool failed = false;
        try { action(); }
        catch (const std::exception&) { failed = true; }
        check(failed, "Expected scene operation to fail");
    };
    wchar_t temporaryDirectory[32768] = {};
    DWORD length = GetTempPathW(_countof(temporaryDirectory), temporaryDirectory);
    check(length > 0 && length < _countof(temporaryDirectory), "Cannot locate temporary directory");
    const auto root = Utf8Path(temporaryDirectory) + "TreeEngine-scenes-" + std::to_string(GetCurrentProcessId()) +
        "-" + std::to_string(GetTickCount64());
    check(CreateDirectoryW(WidePath(root).c_str(), nullptr) != 0, "Cannot create test directory");
    struct Cleanup
    {
        std::string root;
        std::vector<std::string> files;
        ~Cleanup()
        {
            for (const auto& file : files)
                if (!DeleteFileW(WidePath(file).c_str()))
                    OutputDebugStringA(("Cannot remove scene test file: " + file + "\n").c_str());
            RemoveDirectoryW(WidePath(root + "\\User Scenes").c_str());
            RemoveDirectoryW(WidePath(root + "\\Built-in Scenes").c_str());
            RemoveDirectoryW(WidePath(root).c_str());
        }
    } cleanup{ root };
    const auto builtin = root + "\\Built-in Scenes", user = root + "\\User Scenes";
    check(CreateDirectoryW(WidePath(builtin).c_str(), nullptr) != 0, "Cannot create built-in fixture directory");
    auto path = [&](const std::string& directory, const std::string& name) {
        auto filename = directory + "\\" + name;
        cleanup.files.push_back(filename);
        return filename;
    };
    const std::string allKinds = R"({
        "version":1,"id":"roundtrip","name":"Round trip",
        "camera":{"position":[2,3,4],"rotation":[0.1,0.2,0.3,1]},
        "light":{"direction":[0,-1,0],"shadowPosition":[4,5,6]},
        "pointLightCount":1,"pointLight":{"position":[1,2,3],"range":8,"attenuation":[1,0.1,0]},
        "materials":{"shared":{"diffuse":[0.2,0.3,0.4,1],"textureCoordScale":[3,4]}},
        "objects":[
            {"type":"primitive","id":"default"},
            {"type":"primitive","id":"box","shape":"cylinderHD","material":"shared","textureCoordScale":[2,3]},
            {"type":"tree","id":"tree","materials":{"trunk":"shared"},"generator":{
                "axiom":"F","constants":"X","iterations":2,"angle":0.7,"segmentLength":0.2,
                "thickness":0.03,"initialDirection":[0,2,0],"segmentLengthBehavior":"randomAddition",
                "rules":[{"input":"F","output":"FF","iterations":3}]}},
            {"type":"fixedTree","id":"fixed","materials":{"leaf":"shared"}},
            {"type":"graph","id":"graph","dataFile":"..\\graphdata.txt","materials":{"trunk":"shared"}},
            {"type":"model","id":"model","source":"..\\Models\\tree.glb","forceLit":true}
        ]})";
    auto scene = ParseScene(allKinds, "roundtrip fixture");
    for (const auto& id : { "user:collision", "path/id", "path\\id", "control\nid" })
    {
        auto invalidId = Json::parse(allKinds);
        invalidId["id"] = id;
        reject([&] { ParseScene(invalidId.dump(), "invalid identity"); });
    }
    const auto encoded = SerializeScene(scene);
    auto decoded = ParseScene(encoded, "serialized fixture");
    check(SerializeScene(decoded) == encoded, "Serialization is not a stable semantic round trip");
    const auto json = Json::parse(encoded);
    check(json["objects"].size() == 6 && !json["objects"][0].contains("material"), "Material omission was lost");
    check(json["objects"][1]["textureCoordScale"][0] == 2 &&
        json["objects"][1]["material"]["textureCoordScale"][0] == 3, "UV scales were compounded");
    check(json["objects"][2]["generator"]["rules"][0]["iterations"] == 3, "Rule iteration setting was lost");
    check(json["objects"][3].contains("materials") && json["objects"][4].contains("materials"), "Procedural materials were lost");
    check(json["objects"][4]["dataFile"] == "..\\graphdata.txt" &&
        json["objects"][5]["source"] == "..\\Models\\tree.glb", "External asset references changed");
    check(json["objects"][5]["forceLit"] == true && decoded.hasShadowLightPosition &&
        decoded.pointLight.range == 8 && decoded.camera.rotation.w == 1, "Camera, model or lighting values changed");
    auto& material = static_cast<PrimitiveDefinition&>(*decoded.objects[1]).material;
    material.diffuse.x = 0.9f;
    decoded.camera.position.z = 17;
    static_cast<TreeDefinition&>(*decoded.objects[2]).generator.rules[0].output = "F[+F]";
    auto edited = ParseScene(SerializeScene(decoded), "edited fixture");
    check(static_cast<TreeDefinition&>(*edited.objects[2]).trunk.diffuse.x == 0.2f &&
        static_cast<PrimitiveDefinition&>(*edited.objects[1]).material.diffuse.x == 0.9f, "Independent material edits leaked or were lost");
    check(edited.camera.position.z == 17 && static_cast<TreeDefinition&>(*edited.objects[2]).generator.rules[0].output == "F[+F]",
        "Edited camera/rules did not persist");
    material.diffuse.x = std::numeric_limits<float>::quiet_NaN();
    reject([&] { SerializeScene(decoded); });

    const auto filename = path(builtin, "roundtrip.scene.json");
    WriteScene(scene, filename);
    check(SerializeScene(ReadScene(filename)) == encoded, "Scene write/read did not round-trip");
    scene.name = "Replacement";
    reject([&] { WriteScene(scene, filename); });
    check(ReadScene(filename).name == "Round trip", "Declined overwrite changed the existing file");
    WriteScene(scene, filename, true);
    check(ReadScene(filename).name == "Replacement", "Confirmed overwrite did not replace the scene");
    HANDLE locked = CreateFileW(WidePath(filename).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "Cannot lock overwrite fixture");
    scene.name = "Must not replace";
    reject([&] { WriteScene(scene, filename, true); });
    CloseHandle(locked);
    check(ReadScene(filename).name == "Replacement", "Failed overwrite damaged the original file");
    reject([&] { WriteScene(scene, root + "\\missing\\test.scene.json"); });
    scene.camera.scale.x = 0;
    reject([&] { WriteScene(scene, filename, true); });
    check(ReadScene(filename).name == "Replacement", "Invalid data overwrote a valid file");
    scene.camera.scale.x = 1;
    WIN32_FIND_DATAW found = {};
    HANDLE leftovers = FindFirstFileW(WidePath(builtin + "\\*.tmp.*").c_str(), &found);
    if (leftovers != INVALID_HANDLE_VALUE) FindClose(leftovers);
    check(leftovers == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND, "Save leaked temporary files");

    SceneCatalog catalog(builtin, user);
    catalog.Refresh();
    check(catalog.Entries().size() == 1 && catalog.Diagnostics().empty(), "Missing user directory was treated as an error");
    catalog.EnsureUserDirectory();
    for (const auto& stem : { "", "..", "bad\\name", "bad/name", "NUL", "con.txt", "LPT1", "CON .txt",
        "LPT1 .data", "CONIN$", "CONOUT$", "bad:", "trailing.", "trailing " })
        reject([&] { catalog.UserFilename(stem); });
    const auto unicode = Utf8Path(L"\u6811");
    const auto unicodeFile = path(user, unicode + ".scene.json");
    scene.id.clear();
    scene.name = "A user scene";
    WriteScene(scene, unicodeFile);
    catalog.Refresh();
    check(catalog.Entries().size() == 2 && catalog.Entries()[0].user &&
        catalog.Entries()[0].id == unicode, "Unicode file or filename-derived identity failed");
    check(catalog.UserFilename(unicode) == unicodeFile, "Unicode filename validation failed");
    check(ResolveSceneAsset("roundtrip.scene.json", filename) == filename,
        "Scene-relative asset resolution depends on the working directory");
    check(ResolveSceneAsset("..\\Built-in Scenes\\roundtrip.scene.json", unicodeFile) == filename,
        "Asset reference from a Unicode user scene did not resolve");
    scene.name = "Replacement";
    scene.id = "roundtrip";
    const auto shadow = path(user, "shadow.scene.json");
    WriteScene(scene, shadow);
    reject([&] { catalog.CheckUserIdentity("roundtrip", user + "\\another.scene.json"); });
    catalog.CheckUserIdentity("roundtrip", shadow);
    catalog.Refresh();
    check(!catalog.Entries().at(catalog.Find("roundtrip")).user, "User scene shadowed a built-in ID");
    reject([&] { catalog.Find("Replacement"); });
    check(catalog.Find("user:roundtrip") >= 0 && catalog.Find("builtin:roundtrip") >= 0, "Qualified IDs failed");
    const auto extra = path(builtin, "extra.scene.json");
    scene.id = "new";
    scene.name = "B added without catalog changes";
    WriteScene(scene, extra);
    catalog.Refresh();
    check(catalog.Entries().size() == 4 && catalog.Entries()[1].id == "new", "New scene was not discovered in sorted order");
    std::vector<std::string> order;
    for (const auto& entry : catalog.Entries()) order.push_back(entry.filename);
    catalog.Refresh();
    for (size_t i = 0; i < order.size(); ++i) check(order[i] == catalog.Entries()[i].filename, "Enumeration order was unstable");
    const auto malformed = path(builtin, "broken.scene.json");
    { std::ofstream output(WidePath(malformed).c_str()); output << "{broken"; }
    const auto duplicate = path(builtin, "duplicate.scene.json");
    WriteScene(scene, duplicate);
    catalog.Refresh();
    check(catalog.Entries().size() == 3 && catalog.Diagnostics().size() == 3, "Malformed/duplicate files were not excluded with diagnostics");
    check(catalog.Find("new") == -1, "Duplicate ID was resolved arbitrarily");
    auto anonymous = ParseScene(allKinds, "anonymous fixture");
    anonymous.id.clear();
    const auto anonymousFile = path(builtin, ".scene.json");
    WriteScene(anonymous, anonymousFile);
    catalog.Refresh();
    check(catalog.FindPath(anonymousFile) == -1, "Empty filename-derived scene ID was accepted");
    const auto conflictingUser = path(user, "conflicting.scene.json");
    scene.id = "roundtrip";
    WriteScene(scene, conflictingUser);
    catalog.Refresh();
    check(catalog.Find("user:roundtrip") == -1, "Conflicting user IDs were not excluded");
    reject([&] { catalog.CheckUserIdentity("roundtrip", shadow); });
    SceneCatalog empty(root + "\\missing", root + "\\also missing");
    empty.Refresh();
    check(empty.Entries().empty() && !empty.Diagnostics().empty(), "Empty/missing catalog did not provide diagnostics");
    check(catalog.FindPath(unicodeFile) >= 0, "Path identity failed after refresh");
    reject([&] { ReadScene(std::string("bad\0path", 8)); });
    std::vector<wchar_t> executable(32768);
    DWORD executableLength = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    check(executableLength > 0 && executableLength < executable.size(), "Cannot locate deployed scene fixtures");
    SceneCatalog deployed(ParentPath(Utf8Path(executable.data())) + "TestData", root + "\\unused");
    deployed.Refresh();
    check(!deployed.Entries().empty() && deployed.Diagnostics().empty(), "Deployed built-in scene fixtures are missing or invalid");
    for (const auto& entry : deployed.Entries())
    {
        const auto serialized = SerializeScene(ReadScene(entry.filename));
        check(SerializeScene(ParseScene(serialized, entry.filename)) == serialized,
            "Built-in scene did not survive a semantic serialization round trip");
    }
    std::cout << checks << " scene persistence checks passed\n";
    return checks;
}
