#pragma once

// A terrain package's roads file (Tools/terrain/import_roads.py writes it; Tools/terrain/README.md documents it):
// GeoJSON in the package's CRS, with road and driveway centerlines and, later, props. Loading converts its
// coordinates to the package's: x east and z north in meters from the center of the package's source bounds.

#include <DirectXMath.h>
#include <string>
#include <vector>

namespace Roads
{
    enum class Kind { Road, Driveway };

    struct Line
    {
        Kind kind = Kind::Road;
        std::string id;
        std::string source;                 // county, osm, generated, ...
        bool edited = false;
        std::string name;
        std::string surface = "paved";      // paved or gravel
        std::string owner;                  // county or private (roads)
        float width = 6;                    // Meters, the traveled surface.
        float shoulder = 0;                 // Meters each side.
        float ditchDepth = 0, ditchWidth = 0;
        int speedLimit = 0;                 // mph, 0 = unknown.
        std::vector<DirectX::XMFLOAT2> points;  // Package x, z.
    };

    struct Network
    {
        std::string crs;                    // As "EPSG:32610".
        std::vector<Line> lines;
    };

    // The origin of a package's coordinates in its CRS, and the CRS, from its manifest's source JSON and world size;
    // false with `error` when the source has no CRS and bounds. TerrainTool pack puts the source raster's southwest
    // pixel center at the package's -x/-z corner and pads north and east, so the origin is that pixel center plus
    // half the world (not the bounds' center).
    bool PackageFrame(const std::string& sourceJson, double worldSize, std::string& crs, double& originX, double& originY,
        std::string& error);

    // Parses a roads file's text. Its CRS must be `crs`; coordinates become package coordinates about the origin.
    // Throws std::runtime_error naming the feature and property at fault.
    Network Parse(const std::string& text, const std::string& crs, double originX, double originY);
}
