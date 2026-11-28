// TerrainTool: builds terrain packages (Game/TerrainPackage.h) from procedural maps or height rasters, and inspects
// them. See README.md.
//
//   TerrainTool generate --out DIR [--preset valley|hills|flat] [--size 8192] [--spacing 1] [--seed 7] [--relief 600]
//   TerrainTool pack --out DIR (--raw FILE --width W --height H [--type f32|u16|i16] | --png FILE)
//                    [--spacing 1] [--height-scale 1] [--height-offset 0] [--nodata V] [--source-json JSON]
//                    [--water-level M [--seabed-raw FILE [--seabed-scale 1]] [--water-slope 0.05] [--water-depth 8]]
//                    (under flat water: the seabed from a bathymetry raster, else carved from the shore)
//                    [--roads FILE]   (grades road beds, shoulders and ditches; needs --source-json with crs, bounds, size)
//   TerrainTool info DIR
//   TerrainTool heights DIR --in POINTS --out HEIGHTS     (lines of "x z" in meters -> one height per line)
//   TerrainTool texture --out PREFIX --kind grass|rock|snow|dirt|asphalt|gravel|chipseal|patchygravel|padgravel|dirtweeds [--size 512] [--seed 1]
//                       (tileable PREFIX_color.png, PREFIX_normal.png and PREFIX_mr.png for terrain layers)
#include "pch.h"
#include "TerrainPackage.h"
#include "RoadGrading.h"
#include <sstream>

#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <thread>

namespace
{
    using Args = std::map<std::string, std::string>;

    std::wstring Wide(const std::string& text) { return std::wstring(text.begin(), text.end()); }

    Args ParseArgs(int argc, char** argv, int first, std::vector<std::string>& positional)
    {
        Args args;
        for (int i = first; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg.rfind("--", 0) == 0 && i + 1 < argc)
                args[arg.substr(2)] = argv[++i];
            else
                positional.push_back(arg);
        }
        return args;
    }

    double Number(const Args& args, const char* name, double fallback)
    {
        const auto found = args.find(name);
        return found == args.end() ? fallback : std::stod(found->second);
    }

    std::string Text(const Args& args, const char* name, const std::string& fallback = std::string())
    {
        const auto found = args.find(name);
        return found == args.end() ? fallback : found->second;
    }

    // Runs body(row) for rows [0, rows) across the machine's cores.
    template <class Body> void ParallelRows(int rows, Body body)
    {
        const unsigned workers = std::max(1u, std::thread::hardware_concurrency());
        std::atomic<int> next(0);
        std::vector<std::thread> threads;
        for (unsigned w = 0; w < workers; ++w)
            threads.emplace_back([&]() { for (int row = next++; row < rows; row = next++) body(row); });
        for (auto& thread : threads)
            thread.join();
    }

    // Seeded 2D gradient noise in [-1, 1] (Perlin-style, quintic fade).
    class Noise
    {
    public:
        explicit Noise(uint32_t seed)
        {
            for (int i = 0; i < 256; ++i) m_perm[i] = uint8_t(i);
            uint32_t state = seed * 2654435761u + 1013904223u;
            for (int i = 255; i > 0; --i)
            {
                state = state * 1664525u + 1013904223u;
                std::swap(m_perm[i], m_perm[(state >> 8) % uint32_t(i + 1)]);
            }
            for (int i = 0; i < 256; ++i) m_perm[256 + i] = m_perm[i];
        }
        double operator()(double x, double y) const
        {
            const double fx = std::floor(x), fy = std::floor(y);
            const int xi = int(fx) & 255, yi = int(fy) & 255;
            const double dx = x - fx, dy = y - fy;
            const auto fade = [](double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
            const auto grad = [&](int hash, double gx, double gy)
            {
                static const double dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{0.7071,0.7071},{-0.7071,0.7071},{0.7071,-0.7071},{-0.7071,-0.7071} };
                return dirs[hash & 7][0] * gx + dirs[hash & 7][1] * gy;
            };
            const int aa = m_perm[m_perm[xi] + yi], ab = m_perm[m_perm[xi] + yi + 1];
            const int ba = m_perm[m_perm[xi + 1] + yi], bb = m_perm[m_perm[xi + 1] + yi + 1];
            const double u = fade(dx), v = fade(dy);
            const double x1 = grad(aa, dx, dy) + u * (grad(ba, dx - 1, dy) - grad(aa, dx, dy));
            const double x2 = grad(ab, dx, dy - 1) + u * (grad(bb, dx - 1, dy - 1) - grad(ab, dx, dy - 1));
            return (x1 + v * (x2 - x1)) * 1.4142;
        }
        double Fbm(double x, double y, int octaves, double lacunarity = 2.0, double gain = 0.5) const
        {
            double sum = 0, amplitude = 1, norm = 0;
            for (int o = 0; o < octaves; ++o)
            {
                sum += amplitude * (*this)(x, y);
                norm += amplitude;
                x *= lacunarity; y *= lacunarity; amplitude *= gain;
                x += 17.3; y += 9.1;
            }
            return sum / norm;
        }
        double Ridged(double x, double y, int octaves) const
        {
            double sum = 0, amplitude = 1, norm = 0, weight = 1;
            for (int o = 0; o < octaves; ++o)
            {
                double r = 1 - std::fabs((*this)(x, y));
                r *= r * weight;
                weight = std::min(1.0, std::max(0.0, r * 2));
                sum += amplitude * r;
                norm += amplitude;
                x *= 2.03; y *= 2.03; amplitude *= 0.5;
                x += 5.7; y += 31.9;
            }
            return sum / norm;
        }
    private:
        uint8_t m_perm[512];
    };

    double Smoothstep(double edge0, double edge1, double x)
    {
        const double t = std::min(1.0, std::max(0.0, (x - edge0) / (edge1 - edge0)));
        return t * t * (3 - 2 * t);
    }

    // A procedural map, in meters. "valley": a winding river valley with a flat floor, foothills, and ridged mountains
    // rising away from it; "hills": rolling fBm hills; "flat": a gentle slope (tests).
    double ProceduralHeight(const std::string& preset, const Noise& noise, const Noise& warp, double x, double z,
        double relief, double worldSize)
    {
        if (preset == "flat")
            return 0.002 * x + 0.001 * z;
        const double wx = x + 120 * warp.Fbm(x / 900, z / 900, 3);
        const double wz = z + 120 * warp.Fbm(x / 900 + 31, z / 900 + 17, 3);
        const double hills = 0.12 * relief * noise.Fbm(wx / 1400, wz / 1400, 7);
        if (preset == "hills")
            return hills + 0.05 * relief * noise.Fbm(wx / 300, wz / 300, 5);
        // The river's path across x, and the distance from it.
        const double path = 0.12 * worldSize * std::sin(x / (0.16 * worldSize)) + 0.04 * worldSize * std::sin(x / (0.05 * worldSize) + 1.3);
        const double distance = std::fabs(z - path) + 60 * warp.Fbm(x / 500, z / 500, 2);
        const double mountains = relief * std::pow(noise.Ridged(wx / 2600 + 3.1, wz / 2600 + 7.7, 7), 1.6);
        const double rise = Smoothstep(350, 2400, distance);             // Foothills to mountains away from the river.
        const double floor = 1 - Smoothstep(90, 420, distance);          // The flat valley floor.
        double height = hills * (0.4 + 0.6 * rise) + mountains * rise * rise + 0.04 * relief * rise;
        height = height * (1 - floor) + (-8 + 3 * noise.Fbm(x / 200, z / 200, 3)) * floor;
        return height;
    }

    int Generate(const Args& args)
    {
        const std::string out = Text(args, "out");
        if (out.empty()) { fprintf(stderr, "generate needs --out DIR\n"); return 2; }
        const std::string preset = Text(args, "preset", "valley");
        const double spacing = Number(args, "spacing", 1.0);
        const double requested = Number(args, "size", 8192.0);
        const int samples = TerrainFormat::NodeQuads * (1 << (TerrainPackage::LevelsFor(int(requested / spacing) + 1) - 1)) + 1;
        const double worldSize = spacing * (samples - 1);
        const uint32_t seed = uint32_t(Number(args, "seed", 7));
        const double relief = Number(args, "relief", 600.0);
        const Noise noise(seed), warp(seed * 7919u + 13u);
        std::vector<float> heights(size_t(samples) * samples);
        fprintf(stderr, "Generating %s: %d x %d samples, %.0f m at %.2f m, seed %u\n", preset.c_str(), samples, samples,
            worldSize, spacing, seed);
        ParallelRows(samples, [&](int j)
        {
            const double z = -worldSize / 2 + j * spacing;
            for (int i = 0; i < samples; ++i)
                heights[size_t(j) * samples + i] = float(ProceduralHeight(preset, noise, warp, -worldSize / 2 + i * spacing, z, relief, worldSize));
        });
        std::ostringstream source;
        source << "{\"generator\":\"TerrainTool generate\",\"preset\":\"" << preset << "\",\"seed\":" << seed
               << ",\"relief\":" << relief << "}";
        std::string error;
        if (!TerrainPackage::Write(Wide(out), heights, samples, spacing, source.str(), error))
        {
            fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        fprintf(stderr, "Wrote %s\n", out.c_str());
        return 0;
    }

    bool ReadPng(const std::string& path, std::vector<float>& values, int& width, int& height, std::string& error)
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IWICImagingFactory* factory = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;
        bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
            && SUCCEEDED(factory->CreateDecoderFromFilename(Wide(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))
            && SUCCEEDED(decoder->GetFrame(0, &frame))
            && SUCCEEDED(factory->CreateFormatConverter(&converter))
            && SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat16bppGray, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
        UINT w = 0, h = 0;
        std::vector<uint16_t> pixels;
        if (ok)
        {
            converter->GetSize(&w, &h);
            pixels.resize(size_t(w) * h);
            ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 2, UINT(pixels.size() * 2), reinterpret_cast<BYTE*>(pixels.data())));
        }
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        if (factory) factory->Release();
        if (!ok) { error = "cannot read " + path + " as a grayscale image"; return false; }
        width = int(w); height = int(h);
        values.assign(pixels.begin(), pixels.end());
        return true;
    }

    int Pack(const Args& args)
    {
        const std::string out = Text(args, "out");
        if (out.empty()) { fprintf(stderr, "pack needs --out DIR\n"); return 2; }
        std::vector<float> values;
        int width = 0, height = 0;
        std::string error;
        if (args.count("png"))
        {
            if (!ReadPng(Text(args, "png"), values, width, height, error)) { fprintf(stderr, "%s\n", error.c_str()); return 1; }
        }
        else if (args.count("raw"))
        {
            width = int(Number(args, "width", 0));
            height = int(Number(args, "height", 0));
            const std::string type = Text(args, "type", "f32");
            const size_t bytes = type == "f32" ? 4 : 2;
            if (width < 2 || height < 2 || (type != "f32" && type != "u16" && type != "i16"))
            { fprintf(stderr, "--raw needs --width, --height and --type f32|u16|i16\n"); return 2; }
            std::ifstream file(Text(args, "raw").c_str(), std::ios::binary);
            std::vector<uint8_t> data(size_t(width) * height * bytes);
            if (!file.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size())))
            { fprintf(stderr, "cannot read %zu bytes from %s\n", data.size(), Text(args, "raw").c_str()); return 1; }
            values.resize(size_t(width) * height);
            for (size_t i = 0; i < values.size(); ++i)
            {
                if (type == "f32") { float v; memcpy(&v, &data[i * 4], 4); values[i] = v; }
                else if (type == "u16") { uint16_t v; memcpy(&v, &data[i * 2], 2); values[i] = v; }
                else { int16_t v; memcpy(&v, &data[i * 2], 2); values[i] = v; }
            }
        }
        else { fprintf(stderr, "pack needs --png FILE or --raw FILE\n"); return 2; }

        // Height = offset + scale * value; cells equal to --nodata (or non-finite) take the lowest valid height.
        const double scale = Number(args, "height-scale", 1.0), offset = Number(args, "height-offset", 0.0);
        const bool hasNodata = args.count("nodata") > 0;
        const double nodata = Number(args, "nodata", 0.0);
        float lowest = FLT_MAX;
        std::vector<bool> missing(values.size(), false);
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (!std::isfinite(values[i]) || (hasNodata && values[i] == float(nodata))) { missing[i] = true; continue; }
            values[i] = float(offset + scale * values[i]);
            lowest = std::min(lowest, values[i]);
        }
        if (lowest == FLT_MAX) { fprintf(stderr, "no valid heights\n"); return 1; }
        size_t filled = 0;
        for (size_t i = 0; i < values.size(); ++i)
            if (missing[i]) { values[i] = lowest; ++filled; }

        // A seabed under water. Elevation surveys see a water surface, not the bottom (and hydro-flattened DTMs make it
        // one flat sheet), so water couldn't drop below it. Ground at or below --water-level is water: it sinks with
        // distance from the nearest dry ground, --water-slope meters per meter, down to --water-depth below the level.
        if (args.count("water-level"))
        {
            const float level = float(Number(args, "water-level", 0));
            const double slope = Number(args, "water-slope", 0.05), depth = Number(args, "water-depth", 8);
            const double spacingMeters = Number(args, "spacing", 1.0);
            // A bathymetry raster on the same grid (float32, same nodata), in meters times --seabed-scale: water samples
            // take its depth where it has one (never higher than the surface the survey saw).
            std::vector<bool> fromSeabed(values.size(), false);
            if (args.count("seabed-raw"))
            {
                std::ifstream file(Text(args, "seabed-raw").c_str(), std::ios::binary);
                std::vector<float> seabed(values.size());
                if (!file.read(reinterpret_cast<char*>(seabed.data()), std::streamsize(seabed.size() * sizeof(float))))
                { fprintf(stderr, "cannot read %zu heights from %s\n", seabed.size(), Text(args, "seabed-raw").c_str()); return 1; }
                const double seabedScale = Number(args, "seabed-scale", 1.0);
                size_t used = 0;
                for (size_t i = 0; i < values.size(); ++i)
                    if (values[i] <= level && std::isfinite(seabed[i]) && seabed[i] > -1e30f && !(hasNodata && seabed[i] == float(nodata)))
                    {
                        values[i] = std::min(values[i], float(seabedScale * seabed[i]));
                        fromSeabed[i] = true;
                        ++used;
                    }
                fprintf(stderr, "Seabed from bathymetry under %zu water samples\n", used);
            }
            // Two-pass chamfer distance transform (3-4 weights) to dry ground, in samples.
            const float unreached = 1e30f;
            std::vector<float> distance(values.size());
            for (size_t i = 0; i < values.size(); ++i)
                distance[i] = values[i] <= level ? unreached : 0;
            const auto relax = [&](int x, int y, int dx, int dy, float weight)
            {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) return;
                float& d = distance[size_t(y) * width + x];
                d = std::min(d, distance[size_t(ny) * width + nx] + weight);
            };
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    relax(x, y, -1, 0, 1); relax(x, y, 0, -1, 1); relax(x, y, -1, -1, 1.4142f); relax(x, y, 1, -1, 1.4142f);
                }
            for (int y = height - 1; y >= 0; --y)
                for (int x = width - 1; x >= 0; --x)
                {
                    relax(x, y, 1, 0, 1); relax(x, y, 0, 1, 1); relax(x, y, 1, 1, 1.4142f); relax(x, y, -1, 1, 1.4142f);
                }
            size_t carved = 0;
            for (size_t i = 0; i < values.size(); ++i)
                if (values[i] <= level && !fromSeabed[i] && distance[i] < unreached)
                {
                    values[i] = float(std::min<double>(values[i], level - std::min(depth, slope * distance[i] * spacingMeters)));
                    ++carved;
                }
            if (carved)
                fprintf(stderr, "Carved %zu water samples below %.2f m (slope %.3f, at most %.1f m deep)\n", carved, level, slope, depth);
        }

        // Pad to a whole quadtree (64 * 2^n + 1 samples per side), repeating the edge. The image's top row is north,
        // and the engine is left-handed with y up: north is +z (east +x), so rows go in from the bottom (-z) up.
        const int samples = TerrainFormat::NodeQuads * (1 << (TerrainPackage::LevelsFor(std::max(width, height)) - 1)) + 1;
        std::vector<float> heights(size_t(samples) * samples);
        for (int j = 0; j < samples; ++j)
            for (int i = 0; i < samples; ++i)
                heights[size_t(j) * samples + i] = values[size_t(std::max(height - 1 - j, 0)) * width + std::min(i, width - 1)];
        const double spacing = Number(args, "spacing", 1.0);
        std::string source = Text(args, "source-json", "{}");

        // Road beds, shoulders and ditches (Game/RoadGrading.h), from a roads file in the source's CRS.
        if (args.count("roads"))
        {
            std::ifstream file(Text(args, "roads").c_str(), std::ios::binary);
            if (!file) { fprintf(stderr, "cannot read %s\n", Text(args, "roads").c_str()); return 1; }
            std::stringstream text;
            text << file.rdbuf();
            std::string crs;
            double originX = 0, originY = 0;
            if (!Roads::PackageFrame(source, (samples - 1) * spacing, crs, originX, originY, error))
            { fprintf(stderr, "--roads: %s\n", error.c_str()); return 1; }
            Roads::Network network;
            try { network = Roads::Parse(text.str(), crs, originX, originY); }
            catch (const std::runtime_error& e) { fprintf(stderr, "%s\n", e.what()); return 1; }
            const Roads::GradeReport report = Roads::Grade(network, heights, samples, float(spacing));
            fprintf(stderr, "Graded %zu samples under %zu road and driveway lines (%zu in ditches); cut %.2f m, fill %.2f m at most\n",
                report.graded, network.lines.size(), report.ditched, report.deepestCut, report.highestFill);
        }
        if (!TerrainPackage::Write(Wide(out), heights, samples, spacing, source, error))
        {
            fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        fprintf(stderr, "Wrote %s: %d x %d input samples (%zu nodata filled), padded to %d, %.0f m\n", out.c_str(),
            width, height, filled, samples, spacing * (samples - 1));
        return 0;
    }

    // The height at (x, z) from the finest level, bilinear between samples.
    bool HeightAt(const TerrainPackage& package, double x, double z, std::map<uint64_t, std::vector<uint16_t>>& cache, float& height)
    {
        const TerrainManifest& m = package.Manifest();
        const int level = m.levels - 1;
        const double spacing = m.NodeSpacing(level), size = m.NodeSize(level);
        const double u = (x + m.worldSize / 2) / spacing, v = (z + m.worldSize / 2) / spacing;
        const int last = TerrainFormat::NodeQuads * (1 << level);
        const double cu = std::min(std::max(u, 0.0), double(last)), cv = std::min(std::max(v, 0.0), double(last));
        const int i0 = std::min(int(cu), last - 1), j0 = std::min(int(cv), last - 1);
        const int nx = i0 / TerrainFormat::NodeQuads, nz = j0 / TerrainFormat::NodeQuads;
        auto& block = cache[TerrainPackage::NodeIndex(level, nx, nz)];
        if (block.empty())
        {
            block.resize(size_t(TerrainFormat::BlockSamples) * TerrainFormat::BlockSamples);
            if (!package.ReadBlock(level, nx, nz, block.data())) return false;
        }
        const int a = i0 - nx * TerrainFormat::NodeQuads + 1, b = j0 - nz * TerrainFormat::NodeQuads + 1;
        const double fu = cu - i0, fv = cv - j0;
        const auto at = [&](int da, int db) { return m.Height(block[size_t(b + db) * TerrainFormat::BlockSamples + a + da]); };
        height = float((at(0, 0) * (1 - fu) + at(1, 0) * fu) * (1 - fv) + (at(0, 1) * (1 - fu) + at(1, 1) * fu) * fv);
        (void)size;
        return true;
    }

    // Tileable gradient noise: the lattice wraps every `period` cells.
    double PeriodicNoise(const Noise& noise, double x, double y, int period)
    {
        // Blend the four wrapped copies so the field repeats exactly at the period.
        const double fx = x / period - std::floor(x / period), fy = y / period - std::floor(y / period);
        const double px = fx * period, py = fy * period;
        const double a = noise(px, py), b = noise(px - period, py), c = noise(px, py - period), d = noise(px - period, py - period);
        return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
    }

    double TileFbm(const Noise& noise, double u, double v, int basePeriod, int octaves, double gain = 0.5)
    {
        double sum = 0, amplitude = 1, norm = 0;
        int period = basePeriod;
        for (int o = 0; o < octaves; ++o)
        {
            sum += amplitude * PeriodicNoise(noise, u * period, v * period, period);
            norm += amplitude;
            amplitude *= gain;
            period *= 2;
        }
        return sum / norm;
    }

    bool WritePng(const std::string& path, int size, const std::vector<uint8_t>& bgra)
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IWICImagingFactory* factory = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapEncoder* encoder = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
            && SUCCEEDED(factory->CreateStream(&stream))
            && SUCCEEDED(stream->InitializeFromFilename(Wide(path).c_str(), GENERIC_WRITE))
            && SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
            && SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache))
            && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr))
            && SUCCEEDED(frame->Initialize(nullptr))
            && SUCCEEDED(frame->SetSize(size, size))
            && SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA
            && SUCCEEDED(frame->WritePixels(size, size * 4, UINT(bgra.size()), const_cast<BYTE*>(bgra.data())))
            && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
        if (frame) frame->Release();
        if (encoder) encoder->Release();
        if (stream) stream->Release();
        if (factory) factory->Release();
        return ok;
    }

    // Tileable cells (Voronoi): `cells` jittered points per side. edge is the distance from the nearest cell's border
    // (half the gap between the two nearest points), in cells; shade a value in [-1, 1] fixed per cell.
    void TileCells(double u, double v, int cells, uint32_t seed, double& edge, double& shade)
    {
        const auto hash = [seed](int x, int y, uint32_t salt)
        {
            uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + seed * 2246822519u + salt * 3266489917u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return double((h ^ (h >> 16)) & 0xffffff) / double(0xffffff);
        };
        const double x = u * cells, y = v * cells;
        const int cx = int(std::floor(x)), cy = int(std::floor(y));
        double best = 1e9, second = 1e9;
        int bestX = 0, bestY = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int gx = cx + dx, gy = cy + dy;
                const int wx = ((gx % cells) + cells) % cells, wy = ((gy % cells) + cells) % cells;     // Tiles.
                const double px = gx + 0.15 + 0.7 * hash(wx, wy, 1), py = gy + 0.15 + 0.7 * hash(wx, wy, 2);
                const double d = std::sqrt((px - x) * (px - x) + (py - y) * (py - y));
                if (d < best) { second = best; best = d; bestX = wx; bestY = wy; }
                else if (d < second) second = d;
            }
        edge = (second - best) / 2;
        shade = hash(bestX, bestY, 3) * 2 - 1;
    }

    // Tileable surface maps for terrain layers: a height field drives the normal map, and color and roughness vary
    // with it and with broader noise. Base color is sRGB; the normal map is tangent space with +Y up the image (glTF);
    // the metallic-roughness map holds roughness in G and metallic (0) in B.
    int Texture(const Args& args)
    {
        const std::string out = Text(args, "out"), kind = Text(args, "kind", "grass");
        const int size = int(Number(args, "size", 512));
        static const std::set<std::string> kinds = { "grass", "rock", "snow", "dirt", "asphalt", "gravel", "chipseal",
            "patchygravel", "padgravel", "dirtweeds" };
        if (out.empty() || size < 16 || (size & (size - 1)) || !kinds.count(kind))
        {
            fprintf(stderr, "texture needs --out PREFIX, --kind grass|rock|snow|dirt|asphalt|gravel|chipseal|patchygravel|padgravel|dirtweeds and a power-of-two --size\n");
            return 2;
        }
        const uint32_t seed = uint32_t(Number(args, "seed", 1));
        const Noise noise(seed), detail(seed * 31u + 7u);
        // mix: where a two-material surface shows its second material (patchy gravel's dirt, dirt's weeds), 0 to 1.
        std::vector<double> height(size_t(size) * size), shade(size_t(size) * size), mix(size_t(size) * size);
        ParallelRows(size, [&](int row)
        {
            const double v = (row + 0.5) / size;
            for (int column = 0; column < size; ++column)
            {
                const double u = (column + 0.5) / size;
                double h = 0, s = 0;
                if (kind == "rock")
                {
                    // Strata (warped bands) and ridged cracks.
                    const double warp = TileFbm(noise, u, v, 2, 4);
                    const double bands = std::sin((v * 6 + warp * 1.2) * 6.2831853);
                    const double cracks = 1 - std::fabs(TileFbm(detail, u, v, 8, 4));
                    h = 0.45 * bands + 0.8 * std::pow(cracks, 6) + 0.35 * TileFbm(detail, u, v, 16, 4);
                    s = 0.5 * TileFbm(noise, u, v, 4, 3) + 0.3 * bands;
                }
                else if (kind == "grass")
                {
                    // Short tufts over soft clumps.
                    h = 0.6 * TileFbm(detail, u, v, 64, 3) + 0.4 * TileFbm(noise, u, v, 8, 3);
                    s = TileFbm(noise, u, v, 4, 4);
                }
                else if (kind == "snow")
                {
                    h = 0.7 * TileFbm(noise, u, v, 4, 5, 0.45) + 0.1 * TileFbm(detail, u, v, 64, 2);
                    s = TileFbm(noise, u, v, 2, 3);
                }
                else if (kind == "asphalt")
                {
                    // Fine aggregate in a dark binder, worn lighter in broad patches.
                    const double aggregate = std::pow(std::max(0.0, TileFbm(detail, u, v, 96, 2)), 3) * 4;
                    h = 0.3 * TileFbm(detail, u, v, 128, 2) + aggregate;
                    s = 0.6 * TileFbm(noise, u, v, 3, 3) + 0.8 * aggregate;
                }
                else if (kind == "gravel")
                {
                    // Packed stones of a few sizes over fines.
                    const double stones = std::pow(std::max(0.0, TileFbm(detail, u, v, 40, 2)), 1.5) * 2.5;
                    const double grit = std::pow(std::max(0.0, TileFbm(noise, u, v, 128, 2)), 2) * 1.5;
                    h = stones + grit + 0.3 * TileFbm(noise, u, v, 8, 3);
                    s = TileFbm(noise, u, v, 4, 3) + 0.6 * stones;
                }
                else if (kind == "chipseal")
                {
                    // Chip seal: a single layer of small, even stone chips pressed into dark binder, close packed,
                    // some chips lighter where traffic has worn the binder off them.
                    const double chips = std::pow(std::max(0.0, TileFbm(detail, u, v, 72, 2) + 0.15), 1.2) * 2.2;
                    h = chips + 0.15 * TileFbm(noise, u, v, 160, 2);
                    s = 0.5 * TileFbm(noise, u, v, 3, 3) + 0.9 * chips;
                }
                else if (kind == "patchygravel")
                {
                    // Worn gravel: stones thinned to bare packed dirt in broad patches and along wandering ruts.
                    const double stones = std::pow(std::max(0.0, TileFbm(detail, u, v, 40, 2)), 1.5) * 2.5;
                    const double patches = TileFbm(noise, u, v, 3, 4) + 0.35 * TileFbm(detail, u, v, 12, 2);
                    const double dirt = std::min(1.0, std::max(0.0, (patches + 0.05) * 3.5));
                    const double pebbles = std::pow(std::max(0.0, TileFbm(detail, u, v, 32, 2)), 2) * 1.5;
                    h = (1 - dirt) * (stones + 0.3 * TileFbm(noise, u, v, 8, 3)) + dirt * (0.4 * TileFbm(noise, u, v, 8, 4) + pebbles);
                    s = TileFbm(noise, u, v, 4, 3) + 0.6 * (1 - dirt) * stones;
                    mix[size_t(row) * size + column] = dirt;
                }
                else if (kind == "padgravel")
                {
                    // Trailer-pad gravel: large crushed stones (about four times gravel's), their tops rolled level,
                    // with dark fines in the gaps between them; each stone a slightly different grey.
                    // Stones are cells, 24 a side (about 17 cm at the 4 m tile), their borders wobbled so they're
                    // angular rather than round; packed tight, so the gaps are thin and not deep.
                    double edge = 0, cellShade = 0;
                    const double wobble = 0.015 * TileFbm(detail, u, v, 48, 2);
                    TileCells(u + wobble, v - wobble, 24, seed, edge, cellShade);
                    const double stone = std::min(1.0, std::max(0.0, (edge - 0.015) * 14));
                    h = 0.5 * std::sqrt(stone) + 0.15 * TileFbm(noise, u, v, 128, 2) * stone;
                    s = 0.35 * cellShade + 0.4 * TileFbm(noise, u, v, 3, 2) + 0.2 * TileFbm(detail, u, v, 96, 1);
                    mix[size_t(row) * size + column] = 1 - stone;
                }
                else if (kind == "dirtweeds")
                {
                    // Packed dirt, smoother than the forest dirt, with a few small clumps of weeds.
                    const double pebbles = std::pow(std::max(0.0, TileFbm(detail, u, v, 32, 2)), 2) * 1.5;
                    const double clumps = TileFbm(noise, u, v, 6, 3) + 0.5 * TileFbm(detail, u, v, 48, 2);
                    const double weeds = std::min(1.0, std::max(0.0, (clumps - 0.22) * 8));
                    h = 0.35 * TileFbm(noise, u, v, 8, 4) + pebbles + 0.8 * weeds * TileFbm(detail, u, v, 96, 2);
                    s = TileFbm(noise, u, v, 4, 3) + 0.4 * pebbles;
                    mix[size_t(row) * size + column] = weeds;
                }
                else
                {
                    // Dirt: pebbles over a lumpy base.
                    const double pebbles = std::pow(std::max(0.0, TileFbm(detail, u, v, 32, 2)), 2) * 3;
                    h = 0.5 * TileFbm(noise, u, v, 8, 4) + pebbles;
                    s = TileFbm(noise, u, v, 4, 3) + 0.5 * pebbles;
                }
                height[size_t(row) * size + column] = h;
                shade[size_t(row) * size + column] = s;
            }
        });

        const double bump = kind == "rock" ? 6.0 : kind == "dirt" || kind == "gravel" || kind == "patchygravel" ? 4.0
            : kind == "padgravel" ? 3.0 : kind == "grass" || kind == "dirtweeds" ? 3.0 : kind == "chipseal" ? 2.5
            : kind == "asphalt" ? 1.0 : 1.5;
        std::vector<uint8_t> color(size_t(size) * size * 4), normal(color.size()), mr(color.size());
        const auto at = [&](const std::vector<double>& map, int row, int column)
        {
            return map[size_t((row + size) % size) * size + (column + size) % size];
        };
        const auto byte = [](double value) { return uint8_t(std::min(255.0, std::max(0.0, std::round(value * 255)))); };
        const auto srgb = [&](double linear) { return byte(linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1 / 2.4) - 0.055); };
        for (int row = 0; row < size; ++row)
            for (int column = 0; column < size; ++column)
            {
                const size_t i = (size_t(row) * size + column) * 4;
                const double h = at(height, row, column), s = at(shade, row, column), m = at(mix, row, column);
                // Linear base color.
                double r, g, b, roughness;
                if (kind == "rock")      { const double t = 0.5 + 0.5 * s; r = 0.085 + 0.06 * t; g = 0.08 + 0.05 * t; b = 0.07 + 0.04 * t; roughness = 0.75 + 0.15 * h; }
                else if (kind == "grass") { const double t = 0.5 + 0.5 * s; r = 0.05 + 0.05 * t; g = 0.10 + 0.06 * t; b = 0.025 + 0.015 * t; roughness = 0.9; }
                else if (kind == "snow")  { const double t = 0.5 + 0.5 * s; r = 0.78 + 0.08 * t; g = 0.80 + 0.08 * t; b = 0.86 + 0.06 * t; roughness = 0.55 + 0.15 * t; }
                else if (kind == "asphalt") { const double t = std::min(1.0, std::max(0.0, 0.5 + 0.5 * s)); r = 0.045 + 0.05 * t; g = 0.045 + 0.05 * t; b = 0.048 + 0.05 * t; roughness = 0.85 + 0.1 * (1 - t); }
                else if (kind == "gravel")  { const double t = std::min(1.0, std::max(0.0, 0.5 + 0.5 * s)); r = 0.13 + 0.10 * t; g = 0.12 + 0.09 * t; b = 0.10 + 0.08 * t; roughness = 0.92; }
                else if (kind == "chipseal") { const double t = std::min(1.0, std::max(0.0, 0.5 + 0.5 * s)); r = 0.05 + 0.10 * t; g = 0.048 + 0.095 * t; b = 0.045 + 0.085 * t; roughness = 0.88 + 0.08 * (1 - t); }
                else if (kind == "padgravel")
                {
                    const double t = std::min(1.0, std::max(0.0, 0.5 + 0.5 * s)), gap = 1 - 0.3 * m;
                    r = (0.15 + 0.11 * t) * gap; g = (0.145 + 0.10 * t) * gap; b = (0.13 + 0.09 * t) * gap; roughness = 0.9;
                }
                else if (kind == "patchygravel")
                {
                    // Gravel's grey stones over the dirt's brown, by the patch mix.
                    const double t = std::min(1.0, std::max(0.0, 0.5 + 0.5 * s));
                    r = (1 - m) * (0.13 + 0.10 * t) + m * (0.12 + 0.07 * t);
                    g = (1 - m) * (0.12 + 0.09 * t) + m * (0.085 + 0.05 * t);
                    b = (1 - m) * (0.10 + 0.08 * t) + m * (0.06 + 0.035 * t);
                    roughness = 0.9;
                }
                else if (kind == "dirtweeds")
                {
                    // Packed dirt, a little lighter and drier than the forest's, and the weeds' muted green.
                    const double t = 0.5 + 0.5 * s;
                    r = (1 - m) * (0.14 + 0.07 * t) + m * (0.07 + 0.04 * t);
                    g = (1 - m) * (0.10 + 0.05 * t) + m * (0.10 + 0.06 * t);
                    b = (1 - m) * (0.07 + 0.035 * t) + m * (0.035 + 0.02 * t);
                    roughness = 0.85;
                }
                else                      { const double t = 0.5 + 0.5 * s; r = 0.12 + 0.07 * t; g = 0.085 + 0.05 * t; b = 0.06 + 0.035 * t; roughness = 0.85; }
                const double cavity = 0.75 + 0.25 * std::min(1.0, std::max(0.0, 0.5 + h));
                color[i + 0] = srgb(b * cavity);
                color[i + 1] = srgb(g * cavity);
                color[i + 2] = srgb(r * cavity);
                color[i + 3] = 255;
                // Tangent space, +X right and +Y up the image.
                const double dx = (at(height, row, column + 1) - at(height, row, column - 1)) * bump * size / 512.0;
                const double dy = (at(height, row - 1, column) - at(height, row + 1, column)) * bump * size / 512.0;
                const double length = std::sqrt(dx * dx + dy * dy + 1);
                normal[i + 0] = byte(0.5 + 0.5 * (1 / length));
                normal[i + 1] = byte(0.5 + 0.5 * (-dy / length));
                normal[i + 2] = byte(0.5 + 0.5 * (-dx / length));
                normal[i + 3] = 255;
                mr[i + 0] = 0;
                mr[i + 1] = byte(std::min(1.0, std::max(0.05, roughness)));
                mr[i + 2] = 255;
                mr[i + 3] = 255;
            }
        for (const auto& file : { std::make_pair(std::string("_color.png"), &color), std::make_pair(std::string("_normal.png"), &normal),
            std::make_pair(std::string("_mr.png"), &mr) })
            if (!WritePng(out + file.first, size, *file.second))
            {
                fprintf(stderr, "cannot write %s%s\n", out.c_str(), file.first.c_str());
                return 1;
            }
        fprintf(stderr, "Wrote %s_color.png, _normal.png and _mr.png (%s, %d px)\n", out.c_str(), kind.c_str(), size);
        return 0;
    }

    int Info(const std::string& directory)
    {
        TerrainPackage package;
        if (!package.Open(Wide(directory))) { fprintf(stderr, "%s\n", package.Error().c_str()); return 1; }
        const TerrainManifest& m = package.Manifest();
        printf("%s: %.0f m at %.3f m, %d levels (%llu nodes), heights %.2f to %.2f m (step %.4f m)\n", directory.c_str(),
            m.worldSize, m.sampleSpacing, m.levels, (unsigned long long)TerrainPackage::NodeCount(m.levels), m.minHeight,
            m.maxHeight, m.heightScale);
        printf("source: %s\n", m.sourceJson.c_str());
        std::vector<uint16_t> block(size_t(TerrainFormat::BlockSamples) * TerrainFormat::BlockSamples);
        uint64_t read = 0;
        for (int level = 0; level < m.levels; ++level)
            for (int z = 0; z < (1 << level); ++z)
                for (int x = 0; x < (1 << level); ++x)
                {
                    if (!package.ReadBlock(level, x, z, block.data())) { fprintf(stderr, "cannot read node %d/%d/%d\n", level, x, z); return 1; }
                    ++read;
                }
        printf("all %llu blocks read\n", (unsigned long long)read);
        return 0;
    }

    int Heights(const std::string& directory, const Args& args)
    {
        TerrainPackage package;
        if (!package.Open(Wide(directory))) { fprintf(stderr, "%s\n", package.Error().c_str()); return 1; }
        std::ifstream in(Text(args, "in").c_str());
        std::ofstream out(Text(args, "out").c_str());
        if (!in || !out) { fprintf(stderr, "heights needs readable --in and writable --out files\n"); return 2; }
        std::map<uint64_t, std::vector<uint16_t>> cache;
        double x, z;
        while (in >> x >> z)
        {
            float height;
            if (!HeightAt(package, x, z, cache, height)) { fprintf(stderr, "cannot read the block at %.2f %.2f\n", x, z); return 1; }
            out << height << "\n";
        }
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "TerrainTool generate|pack|info|heights|texture ... (see Tools/terrain/README.md)\n");
        return 2;
    }
    const std::string command = argv[1];
    std::vector<std::string> positional;
    const Args args = ParseArgs(argc, argv, 2, positional);
    try
    {
        if (command == "generate") return Generate(args);
        if (command == "pack") return Pack(args);
        if (command == "texture") return Texture(args);
        if (command == "info" && positional.size() == 1) return Info(positional[0]);
        if (command == "heights" && positional.size() == 1) return Heights(positional[0], args);
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    fprintf(stderr, "unknown command or arguments; see Tools/terrain/README.md\n");
    return 2;
}
