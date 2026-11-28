#pragma once

// A terrain package's roads file (Tools/terrain/import_roads.py writes it; Tools/terrain/README.md documents it):
// GeoJSON in the package's CRS, with road and driveway centerlines and roadside props. Loading converts its
// coordinates to the package's: x east and z north in meters from the center of the package's source bounds.

#include <DirectXMath.h>
#include <functional>
#include <string>
#include <vector>

namespace Roads
{
    // Trails are footpaths drawn by hand: narrow, their own material, laid on the ground as it is (not graded).
    enum class Kind { Road, Driveway, Trail };

    constexpr float TrailWidth = 1.0f;          // Meters: a trail's tread unless its width says otherwise.

    // A road or driveway surface a roads file may name (Line::surface).
    bool IsSurface(const std::string& surface);

    struct Line
    {
        Kind kind = Kind::Road;
        std::string id;
        std::string source;                 // county, osm, generated, ...
        bool edited = false;
        std::string name;
        // paved (lane markings on county roads), paved-unmarked, chip-seal (markings as paved), gravel, gravel-patchy
        // (worn, dirt showing through), pad-gravel (large compacted stone, trailer pads) or dirt (packed, a few weeds);
        // trails: their own surface, the property unused.
        std::string surface = "paved";
        std::string owner;                  // county or private (roads)
        float width = 6;                    // Meters, the traveled surface.
        float shoulder = 0;                 // Meters each side.
        float ditchDepth = 0, ditchWidth = 0;
        int speedLimit = 0;                 // mph, 0 = unknown.
        std::vector<DirectX::XMFLOAT2> points;  // Package x, z.
    };

    // Roadside props: a model on the ground at a point, turned to face its heading.
    enum class PropModel { Mailbox, Post, StopSign, StreetNameSign, Count };
    const char* PropModelName(PropModel model);        // "mailbox", "post", "stop-sign", "street-name-sign".

    struct Prop
    {
        PropModel model = PropModel::Post;
        std::string id;
        std::string source;
        bool edited = false;
        std::string text;                   // An address, or a sign's street names (lower blade first, | between).
        float heading = 0;                  // Degrees clockwise from north (+z) that its front faces.
        DirectX::XMFLOAT2 point = {};       // Package x, z.
    };

    struct Network
    {
        std::string crs;                    // As "EPSG:32610".
        std::vector<Line> lines;
        std::vector<Prop> props;
    };

    // The origin of a package's coordinates in its CRS, and the CRS, from its manifest's source JSON and world size;
    // false with `error` when the source has no CRS and bounds. TerrainTool pack puts the source raster's southwest
    // pixel center at the package's -x/-z corner and pads north and east, so the origin is that pixel center plus
    // half the world (not the bounds' center).
    bool PackageFrame(const std::string& sourceJson, double worldSize, std::string& crs, double& originX, double& originY,
        std::string& error);

    // Centerlines as the mesh and the terrain grading follow them.
    constexpr int SmoothingPasses = 2;          // Corner cutting on each centerline before sampling it,
    constexpr float SmoothingPiece = 3.0f;      // on pieces at most this long (meters), which bounds how far a bend
                                                // moves: under 0.6 m at a right angle, 0.69 m at worst in Hungerford.
    constexpr float SampleSpacing = 1.0f;       // Meters between samples along a line.

    struct CenterlineSample
    {
        DirectX::XMFLOAT2 point;    // x, z.
        DirectX::XMFLOAT2 side;     // Unit vector to the line's left (perpendicular to its direction).
        float along;                // Meters from the line's start.
    };

    // A centerline split into pieces of at most SmoothingPiece meters, then corner-cut SmoothingPasses times (Chaikin).
    std::vector<DirectX::XMFLOAT2> SmoothCenterline(const std::vector<DirectX::XMFLOAT2>& line);
    // The smoothed centerline sampled evenly, about SampleSpacing apart, ends included, with sideways directions.
    std::vector<CenterlineSample> SampleCenterline(const std::vector<DirectX::XMFLOAT2>& line);

    // Parses a roads file's text. Its CRS must be `crs`; coordinates become package coordinates about the origin.
    // Throws std::runtime_error naming the feature and property at fault.
    Network Parse(const std::string& text, const std::string& crs, double originX, double originY);

    // A prop changed in the engine: moved or turned (point in package coordinates, heading in degrees), or removed.
    struct PropEdit
    {
        std::string id;
        DirectX::XMFLOAT2 point = {};
        float heading = 0;
        bool removed = false;
    };
    // The roads file's text with each edited prop's line rewritten: its point (back in the CRS, to the centimeter),
    // heading (to a tenth of a degree), edited true, and removed true when it's removed; its other properties, and
    // every other line, as they were. The file is a feature per line (import_roads.py's layout), and rewritten lines
    // keep it (Python's json.dumps spacing). Throws std::runtime_error when an edited prop's line isn't there.
    std::string UpdateProps(const std::string& text, const std::vector<PropEdit>& edits, double originX, double originY);

    // The source file a loaded roads file was copied from by the build, when it was: `loaded` must be the file the
    // scene's `authored` path (relative, no ..) names in the executable's folder, and the source is
    // Game\Resources\<authored> in the nearest folder above that `exists` finds. Empty otherwise (an absolute or
    // escaping path, a file loaded from elsewhere, no source found), so nothing outside those folders is mirrored.
    std::wstring BuildSource(const std::wstring& loaded, const std::wstring& authored, const std::wstring& executableFolder,
        const std::function<bool(const std::wstring&)>& exists);

    // Where a ray (origin, unit direction) enters an axis-aligned box: the distance along it, or a negative value when
    // it misses (or the box is behind it). From inside, 0.
    float RayBox(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& direction, const DirectX::XMFLOAT3& low,
        const DirectX::XMFLOAT3& high);
}
