#include "pch.h"
#include "RoadNetwork.h"
#include "ThirdParty/nlohmann/json.hpp"

#include <algorithm>
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
    using DirectX::XMFLOAT2;

    const char* PropModelName(PropModel model)
    {
        switch (model)
        {
        case PropModel::Mailbox: return "mailbox";
        case PropModel::Post: return "post";
        case PropModel::StopSign: return "stop-sign";
        case PropModel::StreetNameSign: return "street-name-sign";
        default: return "";
        }
    }

    // Chaikin corner cutting: each pass replaces every inner corner with two points a quarter of the way along its
    // segments, keeping the ends, so the polylines the importer simplified bend smoothly. The segments are first split
    // into pieces of at most SmoothingPiece meters, so a cut never reaches more than a quarter piece from a corner:
    // the bend moves well under a road's half-width, however long the importer's segments are.
    std::vector<XMFLOAT2> SmoothCenterline(const std::vector<XMFLOAT2>& input)
    {
        std::vector<XMFLOAT2> line;
        for (size_t i = 0; i + 1 < input.size(); ++i)
        {
            const XMFLOAT2 a = input[i], b = input[i + 1];
            const int pieces = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / SmoothingPiece)));
            for (int p = 0; p < pieces; ++p)
                line.push_back(XMFLOAT2(a.x + (b.x - a.x) * p / pieces, a.y + (b.y - a.y) * p / pieces));
        }
        if (!input.empty())
            line.push_back(input.back());
        for (int pass = 0; pass < SmoothingPasses && line.size() > 2; ++pass)
        {
            std::vector<XMFLOAT2> next = { line.front() };
            for (size_t i = 0; i + 1 < line.size(); ++i)
            {
                const XMFLOAT2 a = line[i], b = line[i + 1];
                if (i > 0)
                    next.push_back(XMFLOAT2(0.75f * a.x + 0.25f * b.x, 0.75f * a.y + 0.25f * b.y));
                if (i + 2 < line.size())
                    next.push_back(XMFLOAT2(0.25f * a.x + 0.75f * b.x, 0.25f * a.y + 0.75f * b.y));
            }
            next.push_back(line.back());
            line.swap(next);
        }
        return line;
    }

    std::vector<CenterlineSample> SampleCenterline(const std::vector<XMFLOAT2>& input)
    {
        // Evenly along the whole line, SampleSpacing apart or a little less, ends included: however finely smoothing
        // split the line, the cross-sections stay about a meter apart.
        const std::vector<XMFLOAT2> line = SmoothCenterline(input);
        std::vector<XMFLOAT2> points;
        std::vector<float> distance;
        for (const XMFLOAT2& q : line)
        {
            if (!points.empty() && std::hypot(q.x - points.back().x, q.y - points.back().y) < 1e-4f)
                continue;
            distance.push_back(points.empty() ? 0 : distance.back() + std::hypot(q.x - points.back().x, q.y - points.back().y));
            points.push_back(q);
        }
        std::vector<CenterlineSample> samples;
        if (points.size() < 2)
            return samples;
        const float length = distance.back();
        const int steps = std::max(1, static_cast<int>(std::ceil(length / SampleSpacing - 1e-4f)));
        size_t segment = 0;
        for (int s = 0; s <= steps; ++s)
        {
            const float along = length * s / steps;
            while (segment + 2 < points.size() && distance[segment + 1] < along)
                ++segment;
            const float span = distance[segment + 1] - distance[segment];
            const float t = span > 0 ? std::min(1.0f, std::max(0.0f, (along - distance[segment]) / span)) : 0;
            const XMFLOAT2& a = points[segment];
            const XMFLOAT2& b = points[segment + 1];
            samples.push_back({ XMFLOAT2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t), XMFLOAT2(0, 0), along });
        }
        // Sideways from the direction across each sample (central differences), so bends get mitered widths.
        for (size_t i = 0; i < samples.size(); ++i)
        {
            const XMFLOAT2& p = samples[i > 0 ? i - 1 : i].point;
            const XMFLOAT2& q = samples[i + 1 < samples.size() ? i + 1 : i].point;
            float dx = q.x - p.x, dz = q.y - p.y;
            const float length = std::hypot(dx, dz);
            if (length < 1e-6f)
            {
                samples[i].side = i > 0 ? samples[i - 1].side : XMFLOAT2(1, 0);
                continue;
            }
            dx /= length;
            dz /= length;
            samples[i].side = XMFLOAT2(-dz, dx);
        }
        return samples;
    }

    bool PackageFrame(const std::string& sourceJson, double worldSize, std::string& crs, double& originX, double& originY,
        std::string& error)
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
        const double xmin = bounds[0].get<double>(), ymin = bounds[1].get<double>();
        const double xmax = bounds[2].get<double>(), ymax = bounds[3].get<double>();
        double columns = 0, rows = 0;
        if (source.contains("size") && source["size"].is_array() && source["size"].size() == 2
            && source["size"][0].is_number() && source["size"][1].is_number())
        {
            columns = source["size"][0].get<double>();
            rows = source["size"][1].get<double>();
        }
        if (!(columns >= 1 && rows >= 1 && xmax > xmin && ymax > ymin && worldSize > 0))
        {
            error = "the terrain package's source has no raster size, so roads can't be placed on it";
            return false;
        }
        crs = NormalizeCrs(source["crs"].get<std::string>());
        originX = xmin + 0.5 * (xmax - xmin) / columns + worldSize / 2;
        originY = ymin + 0.5 * (ymax - ymin) / rows + worldSize / 2;
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
            if (kind != "road" && kind != "driveway" && kind != "prop")
                Fail(where + ".properties.kind", "must be road, driveway or prop");
            // A removed feature only keeps the tools from making it again.
            if (props.contains("removed") && !props["removed"].is_null())
            {
                if (!props["removed"].is_boolean())
                    Fail(where + ".properties.removed", "expected true or false");
                if (props["removed"].get<bool>())
                    continue;
            }
            if (!feature.contains("geometry") || !feature["geometry"].is_object())
                Fail(where + ".geometry", "required");
            const auto& geometry = feature["geometry"];
            const std::string type = geometry.contains("type") && geometry["type"].is_string() ? geometry["type"].get<std::string>() : "";
            if (kind == "prop")
            {
                Prop prop;
                prop.id = id;
                prop.source = Text(props, "source", where + ".properties");
                if (props.contains("edited") && !props["edited"].is_null())
                {
                    if (!props["edited"].is_boolean())
                        Fail(where + ".properties.edited", "expected true or false");
                    prop.edited = props["edited"].get<bool>();
                }
                const std::string model = Text(props, "prop", where + ".properties");
                int found = -1;
                for (int m = 0; m < static_cast<int>(PropModel::Count); ++m)
                    if (model == PropModelName(static_cast<PropModel>(m)))
                        found = m;
                if (found < 0)
                    Fail(where + ".properties.prop", "must be mailbox, post, stop-sign or street-name-sign");
                prop.model = static_cast<PropModel>(found);
                prop.heading = Number(props, "heading", where + ".properties", 0, -360, 720);
                prop.text = Text(props, "text", where + ".properties");
                if (type != "Point")
                    Fail(where + ".geometry", "a prop must be a Point");
                if (!geometry.contains("coordinates"))
                    Fail(where + ".geometry.coordinates", "required");
                // A point is a one-point line to Points, which wants two: check it as a pair of the same point.
                const Json pair = Json::array({ geometry["coordinates"], geometry["coordinates"] });
                prop.point = Points(pair, where + ".geometry.coordinates", originX, originY)[0];
                network.props.push_back(std::move(prop));
                continue;
            }
            if (type != "LineString" && type != "MultiLineString")
                Fail(where + ".geometry", "a " + kind + " must be a LineString or MultiLineString");
            if (!geometry.contains("coordinates") || !geometry["coordinates"].is_array())
                Fail(where + ".geometry.coordinates", "required, an array");
            const auto& coordinates = geometry["coordinates"];
            std::vector<Json> parts;
            if (type == "LineString")
                parts.push_back(coordinates);
            else
            {
                if (coordinates.empty())
                    Fail(where + ".geometry.coordinates", "a MultiLineString needs at least one line");
                for (const auto& part : coordinates)
                    parts.push_back(part);
            }

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
