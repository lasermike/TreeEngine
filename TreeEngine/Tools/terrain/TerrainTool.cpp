// TerrainTool: builds terrain packages (Game/TerrainPackage.h) from procedural maps or height rasters, and inspects
// them. See README.md.
//
//   TerrainTool generate --out DIR [--preset valley|hills|flat] [--size 8192] [--spacing 1] [--seed 7] [--relief 600]
//   TerrainTool pack --out DIR (--raw FILE --width W --height H [--type f32|u16|i16] | --png FILE)
//                    [--spacing 1] [--height-scale 1] [--height-offset 0] [--nodata V] [--source-json JSON]
//   TerrainTool info DIR
//   TerrainTool heights DIR --in POINTS --out HEIGHTS     (lines of "x z" in meters -> one height per line)
#include "pch.h"
#include "TerrainPackage.h"

#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
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

        // Pad to a whole quadtree (64 * 2^n + 1 samples per side), repeating the edge.
        const int samples = TerrainFormat::NodeQuads * (1 << (TerrainPackage::LevelsFor(std::max(width, height)) - 1)) + 1;
        std::vector<float> heights(size_t(samples) * samples);
        for (int j = 0; j < samples; ++j)
            for (int i = 0; i < samples; ++i)
                heights[size_t(j) * samples + i] = values[size_t(std::min(j, height - 1)) * width + std::min(i, width - 1)];
        const double spacing = Number(args, "spacing", 1.0);
        std::string source = Text(args, "source-json", "{}");
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
        fprintf(stderr, "TerrainTool generate|pack|info|heights ... (see Tools/terrain/README.md)\n");
        return 2;
    }
    const std::string command = argv[1];
    std::vector<std::string> positional;
    const Args args = ParseArgs(argc, argv, 2, positional);
    try
    {
        if (command == "generate") return Generate(args);
        if (command == "pack") return Pack(args);
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
