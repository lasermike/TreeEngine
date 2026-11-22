#include "pch.h"
#include "RoadNetwork.h"
#include "ThirdParty/nlohmann/json.hpp"

#include <cmath>
#include <stdexcept>

namespace
{
    using Json = nlohmann::json;

    [[noreturn]] void Fail(const std::string& where, const std::string& what)
    {
        throw std::runtime_error("roads " + where + ": " + what);
    }

    // "urn:ogc:def:crs:EPSG::32610", "EPSG:32610" -> "EPSG:32610".
    std::string NormalizeCrs(const std::string& name)
    {
        const size_t epsg = name.find("EPSG");
        if (epsg == std::string::npos)
            return name;
        const size_t code = name.find_last_of(':');
        return code == std::string::npos ? name : "EPSG:" + name.substr(code + 1);
    }

    std::string Text(const Json& props, const char* key, const std::string& where, const std::string& fallback = "")
    {
        if (!props.contains(key) || props[key].is_null())
            return fallback;
        if (!props[key].is_string())
            Fail(where + "." + key, "expected a string");
        return props[key].get<std::string>();
    }

    float Number(const Json& props, const char* key, const std::string& where, float fallback, float low, float high)
    {
        if (!props.contains(key) || props[key].is_null())
            return fallback;
        if (!props[key].is_number())
            Fail(where + "." + key, "expected a number");
        const double value = props[key].get<double>();
        if (!(value >= low && value <= high))
            Fail(where + "." + key, "must be between " + std::to_string(low) + " and " + std::to_string(high));
        return static_cast<float>(value);
    }

    std::vector<DirectX::XMFLOAT2> Points(const Json& coordinates, const std::string& where, double originX, double originY)
    {
        if (!coordinates.is_array() || coordinates.size() < 2)
            Fail(where, "a line needs at least two points");
        std::vector<DirectX::XMFLOAT2> points;
        for (size_t i = 0; i < coordinates.size(); ++i)
        {
            const auto& point = coordinates[i];
            if (!point.is_array() || point.size() < 2 || !point[0].is_number() || !point[1].is_number())
                Fail(where + "[" + std::to_string(i) + "]", "expected [x, y]");
            const double x = point[0].get<double>() - originX, z = point[1].get<double>() - originY;
            if (!std::isfinite(x) || !std::isfinite(z) || std::abs(x) > 1e6 || std::abs(z) > 1e6)
                Fail(where + "[" + std::to_string(i) + "]", "is not in the package's CRS near the package");
            points.push_back(DirectX::XMFLOAT2(static_cast<float>(x), static_cast<float>(z)));
        }
        return points;
    }
}

namespace Roads
{
    bool PackageFrame(const std::string& sourceJson, std::string& crs, double& originX, double& originY, std::string& error)
    {
        const Json source = Json::parse(sourceJson, nullptr, false);
        if (source.is_discarded() || !source.is_object() || !source.contains("crs") || !source["crs"].is_string()
            || !source.contains("bounds") || !source["bounds"].is_array() || source["bounds"].size() != 4)
        {
            error = "the terrain package records no source CRS and bounds, so roads can't be placed on it";
            return false;
        }
        const auto& bounds = source["bounds"];
        for (const auto& value : bounds)
            if (!value.is_number())
            {
                error = "the terrain package's source bounds aren't numbers";
                return false;
            }
        crs = NormalizeCrs(source["crs"].get<std::string>());
        originX = (bounds[0].get<double>() + bounds[2].get<double>()) / 2;
        originY = (bounds[1].get<double>() + bounds[3].get<double>()) / 2;
        return true;
    }

    Network Parse(const std::string& text, const std::string& crs, double originX, double originY)
    {
        const Json root = Json::parse(text, nullptr, false);
        if (root.is_discarded())
            Fail("file", "is not valid JSON");
        if (!root.is_object() || root.value("type", "") != "FeatureCollection" || !root.contains("features")
            || !root["features"].is_array())
            Fail("file", "expected a GeoJSON FeatureCollection");
        Network network;
        if (!root.contains("crs") || !root["crs"].is_object() || !root["crs"].contains("properties")
            || !root["crs"]["properties"].is_object() || !root["crs"]["properties"].contains("name")
            || !root["crs"]["properties"]["name"].is_string())
            Fail("crs", "required: the package's CRS as {\"type\": \"name\", \"properties\": {\"name\": ...}}");
        network.crs = NormalizeCrs(root["crs"]["properties"]["name"].get<std::string>());
        if (network.crs != NormalizeCrs(crs))
            Fail("crs", "is " + network.crs + ", but the terrain package's is " + NormalizeCrs(crs));

        const auto& features = root["features"];
        for (size_t f = 0; f < features.size(); ++f)
        {
            const auto& feature = features[f];
            std::string where = "features[" + std::to_string(f) + "]";
            if (!feature.is_object() || !feature.contains("properties") || !feature["properties"].is_object())
                Fail(where, "expected a feature with properties");
            const auto& props = feature["properties"];
            const std::string id = Text(props, "id", where + ".properties");
            if (!id.empty())
                where += " (" + id + ")";
            const std::string kind = Text(props, "kind", where + ".properties");
            if (kind != "road" && kind != "driveway")
            {
                if (kind == "prop")
                    continue;       // Props come in a later version of the engine; older engines skip them.
                Fail(where + ".properties.kind", "must be road, driveway or prop");
            }
            if (!feature.contains("geometry") || !feature["geometry"].is_object())
                Fail(where + ".geometry", "required");
            const auto& geometry = feature["geometry"];
            const std::string type = geometry.value("type", "");
            std::vector<Json> parts;
            if (type == "LineString")
                parts.push_back(geometry["coordinates"]);
            else if (type == "MultiLineString" && geometry["coordinates"].is_array())
                for (const auto& part : geometry["coordinates"])
                    parts.push_back(part);
            else
                Fail(where + ".geometry", "a " + kind + " must be a LineString or MultiLineString");

            Line line;
            line.kind = kind == "road" ? Kind::Road : Kind::Driveway;
            line.id = id;
            line.source = Text(props, "source", where + ".properties");
            if (props.contains("edited") && !props["edited"].is_null())
            {
                if (!props["edited"].is_boolean())
                    Fail(where + ".properties.edited", "expected true or false");
                line.edited = props["edited"].get<bool>();
            }
            line.name = Text(props, "name", where + ".properties");
            line.surface = Text(props, "surface", where + ".properties", "paved");
            if (line.surface != "paved" && line.surface != "gravel")
                Fail(where + ".properties.surface", "must be paved or gravel");
            line.owner = Text(props, "owner", where + ".properties");
            line.width = Number(props, "width", where + ".properties", line.kind == Kind::Road ? 6.0f : 3.0f, 1, 40);
            line.shoulder = Number(props, "shoulder", where + ".properties", 0, 0, 10);
            line.ditchDepth = Number(props, "ditchDepth", where + ".properties", 0, 0, 5);
            line.ditchWidth = Number(props, "ditchWidth", where + ".properties", 0, 0, 20);
            line.speedLimit = static_cast<int>(Number(props, "speedLimit", where + ".properties", 0, 0, 100));
            for (size_t p = 0; p < parts.size(); ++p)
            {
                Line piece = line;
                piece.points = Points(parts[p], where + ".geometry.coordinates" + (parts.size() > 1 ? "[" + std::to_string(p) + "]" : ""),
                    originX, originY);
                network.lines.push_back(std::move(piece));
            }
        }
        return network;
    }
}
