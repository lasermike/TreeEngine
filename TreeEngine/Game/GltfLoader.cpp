#include "pch.h"
#include "GltfLoader.h"

#pragma warning(push)
#pragma warning(disable: 4996)
#define CGLTF_IMPLEMENTATION
#include "../ThirdParty/cgltf/cgltf.h"
#pragma warning(pop)

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
    // Reads a whole file in one block. Returns false if it cannot be opened.
    bool ReadFileBytes(const std::wstring& filename, std::vector<uint8_t>& bytes)
    {
        std::ifstream input(filename.c_str(), std::ios::binary | std::ios::ate);
        if (!input) return false;
        const std::streamoff size = input.tellg();
        bytes.resize(size > 0 ? static_cast<size_t>(size) : 0);
        input.seekg(0);
        if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) bytes.clear();
        return true;
    }

    struct PrimitiveKey
    {
        const cgltf_primitive* primitive;
        bool mirrored;
        bool doubleSided;

        bool operator<(const PrimitiveKey& other) const
        {
            if (primitive != other.primitive) return primitive < other.primitive;
            if (mirrored != other.mirrored) return mirrored < other.mirrored;
            return doubleSided < other.doubleSided;
        }
    };

    std::string NameOrIndex(const char* name, const char* type, size_t index)
    {
        if (name && *name) return std::string(type) + " '" + name + "'";
        return std::string(type) + "[" + std::to_string(index) + "]";
    }

    std::string ResultName(cgltf_result result)
    {
        switch (result)
        {
        case cgltf_result_success: return "success";
        case cgltf_result_data_too_short: return "data too short";
        case cgltf_result_unknown_format: return "unknown format";
        case cgltf_result_invalid_json: return "invalid JSON";
        case cgltf_result_invalid_gltf: return "invalid glTF";
        case cgltf_result_invalid_options: return "invalid options";
        case cgltf_result_file_not_found: return "file not found";
        case cgltf_result_io_error: return "I/O error";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "legacy glTF";
        default: return "unknown error";
        }
    }

    [[noreturn]] void ImportError(const std::string& filename, const std::string& message)
    {
        throw std::runtime_error(filename + ": " + message);
    }

    std::string DirectoryName(const std::string& filename)
    {
        auto slash = filename.find_last_of("/\\");
        return slash == std::string::npos ? std::string() : filename.substr(0, slash + 1);
    }

    bool IsAbsolutePath(const std::string& filename)
    {
        return filename.size() > 2 && std::isalpha(static_cast<unsigned char>(filename[0])) && filename[1] == ':'
            || filename.size() > 1 && (filename[0] == '\\' || filename[0] == '/') && (filename[1] == '\\' || filename[1] == '/');
    }

    std::string CanonicalPath(const std::string& filename)
    {
        DWORD size = GetFullPathNameA(filename.c_str(), 0, nullptr, nullptr);
        if (!size) return filename;
        std::vector<char> fullPath(size);
        if (!GetFullPathNameA(filename.c_str(), size, fullPath.data(), nullptr)) return filename;
        return fullPath.data();
    }

    std::wstring Wide(const std::string& text)
    {
        if (text.empty()) return {};
        int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, nullptr, 0);
        if (!size) throw std::runtime_error("Invalid UTF-8 path in glTF asset");
        std::vector<wchar_t> result(size);
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, result.data(), size))
            throw std::runtime_error("Invalid UTF-8 path in glTF asset");
        return result.data();
    }

    std::string DecodeUri(const char* uri)
    {
        std::vector<char> decoded(uri, uri + strlen(uri) + 1);
        cgltf_decode_uri(decoded.data());
        return decoded.data();
    }

    std::string ResolveUri(const std::string& assetFilename, const char* uri)
    {
        auto decoded = DecodeUri(uri);
        return CanonicalPath(IsAbsolutePath(decoded) ? decoded : DirectoryName(assetFilename) + decoded);
    }

    int Base64Value(char c)
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    std::vector<uint8_t> DecodeBase64(const std::string& filename, const char* encoded)
    {
        std::vector<uint8_t> result;
        int value = 0;
        int bits = -8;
        for (const char* current = encoded; *current; ++current)
        {
            if (*current == '=') break;
            if (std::isspace(static_cast<unsigned char>(*current))) continue;
            int digit = Base64Value(*current);
            if (digit < 0) ImportError(filename, "embedded image has invalid base64 data");
            value = (value << 6) | digit;
            bits += 6;
            if (bits >= 0)
            {
                result.push_back(static_cast<uint8_t>((value >> bits) & 0xff));
                bits -= 8;
            }
        }
        return result;
    }

    bool StartsWith(const char* value, const char* prefix)
    {
        return strncmp(value, prefix, strlen(prefix)) == 0;
    }

    std::string LowerExtension(const std::string& filename)
    {
        auto dot = filename.find_last_of('.');
        std::string extension = dot == std::string::npos ? std::string() : filename.substr(dot);
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension;
    }

    size_t PointerIndex(const void* pointer, const void* first, size_t stride)
    {
        return static_cast<size_t>((static_cast<const uint8_t*>(pointer) - static_cast<const uint8_t*>(first)) / stride);
    }

    XMFLOAT3 ReadVector3(const cgltf_accessor* accessor, size_t index, const std::string& filename, const std::string& semantic)
    {
        cgltf_float value[4] = {};
        if (!cgltf_accessor_read_float(accessor, index, value, 3))
            ImportError(filename, "cannot read " + semantic + " accessor element " + std::to_string(index));
        return XMFLOAT3(value[0], value[1], value[2]);
    }

    XMFLOAT2 ReadVector2(const cgltf_accessor* accessor, size_t index, const std::string& filename)
    {
        cgltf_float value[2] = {};
        if (!cgltf_accessor_read_float(accessor, index, value, 2))
            ImportError(filename, "cannot read TEXCOORD_0 accessor element " + std::to_string(index));
        return XMFLOAT2(value[0], value[1]);
    }

    const cgltf_accessor* FindAttribute(const cgltf_primitive& primitive, cgltf_attribute_type type, int index)
    {
        for (size_t i = 0; i < primitive.attributes_count; ++i)
        {
            const auto& attribute = primitive.attributes[i];
            if (attribute.type == type && attribute.index == index) return attribute.data;
        }
        return nullptr;
    }

    void ValidateAccessor(const std::string& filename, const cgltf_accessor* accessor, const std::string& semantic)
    {
        if (!accessor) return;
        if (accessor->is_sparse) ImportError(filename, semantic + " sparse accessors are not supported");
        if (!accessor->buffer_view || !accessor->buffer_view->buffer || !accessor->buffer_view->buffer->data)
            ImportError(filename, semantic + " accessor has no loaded buffer data");
    }

    XMFLOAT3 NormalizeOr(const XMFLOAT3& value, const XMFLOAT3& fallback)
    {
        XMVECTOR vector = XMLoadFloat3(&value);
        if (XMVectorGetX(XMVector3LengthSq(vector)) < 1e-12f) return fallback;
        XMFLOAT3 normalized;
        XMStoreFloat3(&normalized, XMVector3Normalize(vector));
        return normalized;
    }

    void GenerateNormals(GltfPrimitiveData& primitive)
    {
        std::vector<XMVECTOR> sums(primitive.vertices.size(), XMVectorZero());
        for (size_t i = 0; i + 2 < primitive.indices.size(); i += 3)
        {
            UINT i0 = primitive.indices[i];
            UINT i1 = primitive.indices[i + 1];
            UINT i2 = primitive.indices[i + 2];
            XMVECTOR p0 = XMLoadFloat3(&primitive.vertices[i0].Pos);
            XMVECTOR p1 = XMLoadFloat3(&primitive.vertices[i1].Pos);
            XMVECTOR p2 = XMLoadFloat3(&primitive.vertices[i2].Pos);
            XMVECTOR normal = XMVector3Cross(p1 - p0, p2 - p0);
            sums[i0] += normal;
            sums[i1] += normal;
            sums[i2] += normal;
        }
        for (size_t i = 0; i < primitive.vertices.size(); ++i)
        {
            XMFLOAT3 normal;
            XMStoreFloat3(&normal, sums[i]);
            primitive.vertices[i].Normal = NormalizeOr(normal, XMFLOAT3(0, 1, 0));
        }
    }

    void GenerateTangents(GltfPrimitiveData& primitive)
    {
        std::vector<XMVECTOR> sums(primitive.vertices.size(), XMVectorZero());
        std::vector<XMVECTOR> bitangentSums(primitive.vertices.size(), XMVectorZero());
        for (size_t i = 0; i + 2 < primitive.indices.size(); i += 3)
        {
            UINT i0 = primitive.indices[i];
            UINT i1 = primitive.indices[i + 1];
            UINT i2 = primitive.indices[i + 2];
            const auto& v0 = primitive.vertices[i0];
            const auto& v1 = primitive.vertices[i1];
            const auto& v2 = primitive.vertices[i2];
            float x1 = v1.Pos.x - v0.Pos.x;
            float y1 = v1.Pos.y - v0.Pos.y;
            float z1 = v1.Pos.z - v0.Pos.z;
            float x2 = v2.Pos.x - v0.Pos.x;
            float y2 = v2.Pos.y - v0.Pos.y;
            float z2 = v2.Pos.z - v0.Pos.z;
            float s1 = v1.Tex.x - v0.Tex.x;
            float t1 = v1.Tex.y - v0.Tex.y;
            float s2 = v2.Tex.x - v0.Tex.x;
            float t2 = v2.Tex.y - v0.Tex.y;
            float determinant = s1 * t2 - s2 * t1;
            if (fabsf(determinant) < 1e-8f) continue;
            float inverse = 1.0f / determinant;
            XMVECTOR tangent = XMVectorSet(
                (t2 * x1 - t1 * x2) * inverse,
                (t2 * y1 - t1 * y2) * inverse,
                (t2 * z1 - t1 * z2) * inverse,
                0);
            sums[i0] += tangent;
            sums[i1] += tangent;
            sums[i2] += tangent;
            XMVECTOR bitangent = XMVectorSet(
                (s1 * x2 - s2 * x1) * inverse,
                (s1 * y2 - s2 * y1) * inverse,
                (s1 * z2 - s2 * z1) * inverse,
                0);
            bitangentSums[i0] += bitangent;
            bitangentSums[i1] += bitangent;
            bitangentSums[i2] += bitangent;
        }
        for (size_t i = 0; i < primitive.vertices.size(); ++i)
        {
            XMVECTOR normal = XMLoadFloat3(&primitive.vertices[i].Normal);
            XMVECTOR tangent = sums[i] - normal * XMVector3Dot(normal, sums[i]);
            XMFLOAT3 tangentValue;
            XMStoreFloat3(&tangentValue, tangent);
            if (XMVectorGetX(XMVector3LengthSq(tangent)) < 1e-12f)
            {
                XMVECTOR axis = fabsf(primitive.vertices[i].Normal.y) < 0.9f
                    ? XMVectorSet(0, 1, 0, 0)
                    : XMVectorSet(1, 0, 0, 0);
                XMStoreFloat3(&tangentValue, XMVector3Normalize(XMVector3Cross(axis, normal)));
            }
            else
            {
                tangentValue = NormalizeOr(tangentValue, XMFLOAT3(1, 0, 0));
            }
            float tangentSign = 1.0f;
            XMVECTOR bitangent = bitangentSums[i];
            if (XMVectorGetX(XMVector3LengthSq(bitangent)) >= 1e-12f)
                tangentSign = XMVectorGetX(XMVector3Dot(XMVector3Cross(normal, XMLoadFloat3(&tangentValue)), bitangent)) < 0 ? -1.0f : 1.0f;
            primitive.vertices[i].TangentU = XMFLOAT4(tangentValue.x, tangentValue.y, tangentValue.z, tangentSign);
        }
    }

    XMMATRIX ConvertedNodeTransform(const cgltf_node& node)
    {
        cgltf_float source[16];
        cgltf_node_transform_world(&node, source);
        XMMATRIX transform(
            source[0], source[1], source[2], source[3],
            source[4], source[5], source[6], source[7],
            source[8], source[9], source[10], source[11],
            source[12], source[13], source[14], source[15]);
        XMMATRIX handedness = XMMatrixScaling(1, 1, -1);
        return handedness * transform * handedness;
    }

    void ValidateRequiredExtensions(const std::string& filename, const cgltf_data& data)
    {
        for (size_t i = 0; i < data.extensions_required_count; ++i)
            if (strcmp(data.extensions_required[i], "KHR_materials_specular") != 0
                && strcmp(data.extensions_required[i], "KHR_materials_ior") != 0
                && strcmp(data.extensions_required[i], "KHR_materials_transmission") != 0
                && strcmp(data.extensions_required[i], "KHR_texture_transform") != 0)
                ImportError(filename, std::string("required glTF extension not supported: ") + data.extensions_required[i]);
    }

    GltfTextureSource ImportTexture(
        const std::string& filename,
        const cgltf_texture& texture,
        size_t textureIndex)
    {
        if (texture.has_basisu || texture.has_webp)
            ImportError(filename, "texture[" + std::to_string(textureIndex) + "] uses an unsupported compressed image extension");
        if (!texture.image)
            ImportError(filename, "texture[" + std::to_string(textureIndex) + "] has no image");

        const auto& image = *texture.image;
        GltfTextureSource result;
        result.name = image.name && *image.name ? image.name : NameOrIndex(nullptr, "image", textureIndex);
        if (image.uri)
        {
            if (StartsWith(image.uri, "data:"))
            {
                const char* comma = strchr(image.uri, ',');
                if (!comma || !StartsWith(image.uri, "data:image/") || !strstr(image.uri, ";base64,"))
                    ImportError(filename, "texture[" + std::to_string(textureIndex) + "] uses an unsupported image data URI");
                result.mimeType.assign(image.uri + 5, strstr(image.uri, ";base64,") - (image.uri + 5));
                if (result.mimeType != "image/png" && result.mimeType != "image/jpeg")
                    ImportError(filename, "texture[" + std::to_string(textureIndex) + "] uses unsupported MIME type '" + result.mimeType + "'");
                result.encodedData = DecodeBase64(filename, comma + 1);
                result.origin = GltfTextureSource::Origin::DataUri;
                result.key = Wide(filename + "#texture" + std::to_string(textureIndex));
            }
            else
            {
                auto resolved = ResolveUri(filename, image.uri);
                auto extension = LowerExtension(resolved);
                if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" && extension != ".dds")
                    ImportError(filename, "texture[" + std::to_string(textureIndex) + "] references unsupported image '" + resolved + "'");
                std::ifstream exists(resolved, std::ios::binary);
                if (!exists) ImportError(filename, "texture[" + std::to_string(textureIndex) + "] image '" + resolved + "' was not found");
                result.filename = Wide(resolved);
                result.mimeType = extension == ".png" ? "image/png" : extension == ".dds" ? "image/vnd-ms.dds" : "image/jpeg";
                result.key = result.filename;
            }
        }
        else if (image.buffer_view)
        {
            if (!image.buffer_view->buffer || !image.buffer_view->buffer->data)
                ImportError(filename, "texture[" + std::to_string(textureIndex) + "] embedded image buffer was not loaded");
            if (!image.mime_type || (strcmp(image.mime_type, "image/png") != 0 && strcmp(image.mime_type, "image/jpeg") != 0))
                ImportError(filename, "texture[" + std::to_string(textureIndex) + "] embedded image has an unsupported MIME type");
            if (image.buffer_view->offset + image.buffer_view->size > image.buffer_view->buffer->size)
                ImportError(filename, "texture[" + std::to_string(textureIndex) + "] embedded image exceeds its buffer");
            const auto* bytes = static_cast<const uint8_t*>(image.buffer_view->buffer->data) + image.buffer_view->offset;
            result.encodedData.assign(bytes, bytes + image.buffer_view->size);
            result.origin = GltfTextureSource::Origin::BufferView;
            result.mimeType = image.mime_type;
            result.key = Wide(filename + "#texture" + std::to_string(textureIndex));
        }
        else
        {
            ImportError(filename, "texture[" + std::to_string(textureIndex) + "] image has no URI or buffer view");
        }
        return result;
    }

    GltfMaterialData ImportMaterial(
        const std::string& filename,
        const cgltf_data& data,
        const cgltf_material* material,
        std::vector<GltfTextureSource>& textures,
        std::map<std::pair<const cgltf_texture*, int>, size_t>& textureMap)
    {
        GltfMaterialData result;
        result.name = material
            ? (material->name && *material->name ? material->name : NameOrIndex(nullptr, "material", PointerIndex(material, data.materials, sizeof(cgltf_material))))
            : "Default material";
        result.shaderMaterial.Ambient = XMFLOAT4(.15f, .15f, .15f, 1);
        result.shaderMaterial.Diffuse = XMFLOAT4(1, 1, 1, 1);
        result.shaderMaterial.specularParams = XMFLOAT4(1, 1, 1, 1);
        if (!material) return result;

        size_t materialIndex = PointerIndex(material, data.materials, sizeof(cgltf_material));
        std::string materialName = NameOrIndex(material->name, "material", materialIndex);
        if (material->has_pbr_specular_glossiness || material->has_clearcoat
            || material->has_volume || material->has_sheen
            || material->has_emissive_strength || material->has_iridescence || material->has_diffuse_transmission
            || material->has_anisotropy || material->has_dispersion)
            ImportError(filename, materialName + " uses an unsupported material extension");
        if (material->occlusion_texture.texture || material->emissive_texture.texture)
            ImportError(filename, materialName + " uses unsupported occlusion or emissive textures");
        if (material->emissive_factor[0] != 0 || material->emissive_factor[1] != 0 || material->emissive_factor[2] != 0)
            ImportError(filename, materialName + " uses unsupported emissive output");

        const auto& pbr = material->pbr_metallic_roughness;
        PbrMaterialFactors factors;
        factors.metallic = pbr.metallic_factor;
        factors.roughness = pbr.roughness_factor;
        if (!std::isfinite(factors.metallic) || factors.metallic < 0 || factors.metallic > 1)
            ImportError(filename, materialName + " metallicFactor must be in [0, 1]");
        if (!std::isfinite(factors.roughness) || factors.roughness < 0 || factors.roughness > 1)
            ImportError(filename, materialName + " roughnessFactor must be in [0, 1]");
        const float* color = pbr.base_color_factor;
        factors.baseColor = XMFLOAT4(color[0], color[1], color[2], color[3]);
        factors.alphaMode = material->alpha_mode == cgltf_alpha_mode_blend ? PbrAlphaMode::Blend
            : material->alpha_mode == cgltf_alpha_mode_mask ? PbrAlphaMode::Mask
            : PbrAlphaMode::Opaque;
        factors.alphaCutoff = material->alpha_cutoff;
        factors.unlit = material->unlit != 0;
        result.alphaMask = factors.alphaMode == PbrAlphaMode::Mask;
        result.alphaCutoff = material->alpha_cutoff;
        result.unlit = factors.unlit;
        result.doubleSided = material->double_sided != 0;

        if (material->has_ior)
        {
            const float ior = material->ior.ior;
            if (material->unlit)
                ImportError(filename, materialName + " combines KHR_materials_ior with KHR_materials_unlit");
            if (!std::isfinite(ior) || (ior != 0.0f && ior < 1.0f))
                ImportError(filename, materialName + " KHR_materials_ior.ior must be zero or at least one");
            factors.ior = ior;
        }

        if (material->has_transmission)
        {
            const auto& transmission = material->transmission;
            if (material->unlit)
                ImportError(filename, materialName + " combines KHR_materials_transmission with KHR_materials_unlit");
            if (!std::isfinite(transmission.transmission_factor) || transmission.transmission_factor < 0.0f
                || transmission.transmission_factor > 1.0f)
                ImportError(filename, materialName + " transmissionFactor must be in [0, 1]");
            if (transmission.transmission_texture.texture)
                ImportError(filename, materialName + " uses unsupported transmissionTexture");
            factors.transmission = transmission.transmission_factor;
        }

        if (material->has_specular)
        {
            const auto& spec = material->specular;
            if (material->unlit)
                ImportError(filename, materialName + " combines KHR_materials_specular with KHR_materials_unlit");
            if (!std::isfinite(spec.specular_factor) || spec.specular_factor < 0 || spec.specular_factor > 1)
                ImportError(filename, materialName + " specularFactor must be in [0, 1]");
            for (float component : spec.specular_color_factor)
                if (!std::isfinite(component) || component < 0)
                    ImportError(filename, materialName + " specularColorFactor must be finite and nonnegative");
            factors.specular = spec.specular_factor;
            factors.specularColor = XMFLOAT3(spec.specular_color_factor[0],
                spec.specular_color_factor[1], spec.specular_color_factor[2]);
        }
        result.shaderMaterial = BuildPbrShaderMaterial(factors, result.alphaBlend);

        auto importView = [&](const cgltf_texture_view& textureView, int colorSpace, bool& wrap,
            MaterialTextureRole role) -> int
        {
            if (textureView.texture)
            {
                const int texcoord = textureView.has_transform && textureView.transform.has_texcoord
                    ? textureView.transform.texcoord : textureView.texcoord;
                if (texcoord != 0)
                    ImportError(filename, materialName + " uses unsupported texture coordinate set TEXCOORD_" + std::to_string(texcoord));
                if (textureView.has_transform)
                {
                    const auto& transform = textureView.transform;
                    if (!std::isfinite(transform.offset[0]) || !std::isfinite(transform.offset[1])
                        || !std::isfinite(transform.rotation)
                        || !std::isfinite(transform.scale[0]) || !std::isfinite(transform.scale[1]))
                        ImportError(filename, materialName + " KHR_texture_transform values must be finite");
                    SetMaterialUvTransform(result.shaderMaterial, role,
                        XMFLOAT2(transform.offset[0], transform.offset[1]), transform.rotation,
                        XMFLOAT2(transform.scale[0], transform.scale[1]));
                }
                const auto* texture = textureView.texture;
                if (texture->sampler)
                {
                    const auto& sampler = *texture->sampler;
                    if (sampler.wrap_s != sampler.wrap_t || sampler.wrap_s == cgltf_wrap_mode_mirrored_repeat)
                        ImportError(filename, materialName + " uses unsupported texture wrapping");
                    if (sampler.mag_filter == cgltf_filter_type_nearest
                        || sampler.min_filter == cgltf_filter_type_nearest
                        || sampler.min_filter == cgltf_filter_type_nearest_mipmap_nearest
                        || sampler.min_filter == cgltf_filter_type_nearest_mipmap_linear)
                        ImportError(filename, materialName + " uses unsupported nearest-neighbor texture filtering");
                    wrap = sampler.wrap_s != cgltf_wrap_mode_clamp_to_edge;
                }
    
                auto key = std::make_pair(texture, colorSpace);
                auto existing = textureMap.find(key);
                if (existing == textureMap.end())
                {
                    size_t index = textures.size();
                    textures.push_back(ImportTexture(filename, *texture,
                        PointerIndex(texture, data.textures, sizeof(cgltf_texture))));
                    auto& source = textures.back();
                    source.colorSpace = colorSpace;
                    if (colorSpace)
                    {
                        // Core glTF images are PNG/JPEG. DDS remains supported on the legacy path.
                        if (!source.IsMemoryBacked())
                        {
                            auto dot = source.filename.find_last_of(L'.');
                            if (dot != std::wstring::npos && _wcsicmp(source.filename.c_str() + dot, L".dds") == 0)
                                ImportError(filename, materialName + " material maps require PNG/JPEG images");
                            ReadFileBytes(source.filename, source.encodedData);
                            if (source.encodedData.empty()) ImportError(filename, materialName + " has an empty texture image");
                        }
                        source.key += colorSpace == 2 ? L"#srgb" : L"#linear";
                    }
                    textureMap.emplace(key, index);
                    return static_cast<int>(index);
                }
                else
                {
                    return static_cast<int>(existing->second);
                }
            }
            return -1;
        };
        result.textureIndex = importView(pbr.base_color_texture, 2, result.wrapTexture, BaseColorTextureRole);
        result.shaderMaterial.specularColor.w = result.wrapTexture ? 0.0f : 1.0f;
        bool metallicRoughnessWrap = true;
        result.metallicRoughnessTextureIndex = importView(pbr.metallic_roughness_texture, 1, metallicRoughnessWrap, MetallicRoughnessTextureRole);
        result.shaderMaterial.metallicRoughnessTextures.y = metallicRoughnessWrap ? 0.0f : 1.0f;
        if (material->normal_texture.texture)
        {
            if (!std::isfinite(material->normal_texture.scale) || material->normal_texture.scale < 0)
                ImportError(filename, materialName + " normalTexture.scale must be finite and nonnegative");
            bool normalWrap = true;
            result.normalTextureIndex = importView(material->normal_texture, 1, normalWrap, NormalTextureRole);
            result.shaderMaterial.normalTextures.y = material->normal_texture.scale;
            result.shaderMaterial.normalTextures.z = normalWrap ? 0.0f : 1.0f;
        }
        result.shaderMaterial.flags.y = result.textureIndex >= 0 ? 1.0f : 0.0f;
        if (material->has_specular)
        {
            bool strengthWrap = true, colorWrap = true;
            result.specularTextureIndex = importView(material->specular.specular_texture, 1, strengthWrap, SpecularStrengthTextureRole);
            result.specularColorTextureIndex = importView(material->specular.specular_color_texture, 2, colorWrap, SpecularColorTextureRole);
            result.shaderMaterial.specularTextures.z = strengthWrap ? 0.0f : 1.0f;
            result.shaderMaterial.specularTextures.w = colorWrap ? 0.0f : 1.0f;
            result.shaderMaterial.specularColor.w = result.wrapTexture ? 0.0f : 1.0f;
        }
        return result;
    }

    GltfPrimitiveData ImportPrimitive(
        const std::string& filename,
        const cgltf_primitive& source,
        bool mirrored,
        bool doubleSided,
        bool requiresTexcoords)
    {
        const auto* positions = FindAttribute(source, cgltf_attribute_type_position, 0);
        const auto* normals = FindAttribute(source, cgltf_attribute_type_normal, 0);
        const auto* texcoords = FindAttribute(source, cgltf_attribute_type_texcoord, 0);
        const auto* tangents = FindAttribute(source, cgltf_attribute_type_tangent, 0);
        if (!positions) ImportError(filename, "mesh primitive is missing POSITION");
        if (requiresTexcoords && !texcoords) ImportError(filename, "normal texture requires TEXCOORD_0");
        ValidateAccessor(filename, positions, "POSITION");
        ValidateAccessor(filename, normals, "NORMAL");
        ValidateAccessor(filename, texcoords, "TEXCOORD_0");
        ValidateAccessor(filename, tangents, "TANGENT");
        ValidateAccessor(filename, source.indices, "indices");
        if (positions->type != cgltf_type_vec3)
            ImportError(filename, "POSITION accessor must contain VEC3 values");
        if (normals && (normals->type != cgltf_type_vec3 || normals->count != positions->count))
            ImportError(filename, "NORMAL accessor must contain one VEC3 per position");
        if (texcoords && (texcoords->type != cgltf_type_vec2 || texcoords->count != positions->count))
            ImportError(filename, "TEXCOORD_0 accessor must contain one VEC2 per position");
        if (tangents && (tangents->type != cgltf_type_vec4 || tangents->count != positions->count))
            ImportError(filename, "TANGENT accessor must contain one VEC4 per position");
        if (positions->count > std::numeric_limits<UINT>::max())
            ImportError(filename, "mesh primitive has too many vertices");

        GltfPrimitiveData result;
        result.sourceVertexCount = positions->count;
        result.sourceIndexCount = source.indices ? source.indices->count : positions->count;
        result.hasNormals = normals != nullptr;
        result.hasTexcoords = texcoords != nullptr;
        result.hasTangents = tangents != nullptr;
        result.vertices.resize(positions->count);
        for (size_t i = 0; i < positions->count; ++i)
        {
            auto position = ReadVector3(positions, i, filename, "POSITION");
            position.z = -position.z;
            result.vertices[i].Pos = position;
            result.vertices[i].Tex = texcoords ? ReadVector2(texcoords, i, filename) : XMFLOAT2(0, 0);
            if (normals)
            {
                auto normal = ReadVector3(normals, i, filename, "NORMAL");
                normal.z = -normal.z;
                result.vertices[i].Normal = NormalizeOr(normal, XMFLOAT3(0, 1, 0));
            }
            if (tangents)
            {
                cgltf_float tangentComponents[4] = {};
                if (!cgltf_accessor_read_float(tangents, i, tangentComponents, 4))
                    ImportError(filename, "cannot read TANGENT accessor element " + std::to_string(i));
                if (tangentComponents[3] != -1.0f && tangentComponents[3] != 1.0f)
                    ImportError(filename, "TANGENT.w must be +1 or -1");
                // Flipping glTF Z reverses the tangent frame handedness.
                XMFLOAT3 tangent(tangentComponents[0], tangentComponents[1], tangentComponents[2]);
                tangent.z = -tangent.z;
                tangent = NormalizeOr(tangent, XMFLOAT3(1, 0, 0));
                result.vertices[i].TangentU = XMFLOAT4(tangent.x, tangent.y, tangent.z, -tangentComponents[3]);
            }
        }

        size_t indexCount = source.indices ? source.indices->count : positions->count;
        if (indexCount % 3 != 0) ImportError(filename, "triangle primitive index count is not divisible by three");
        result.indices.resize(indexCount);
        for (size_t i = 0; i < indexCount; ++i)
        {
            size_t index = source.indices ? cgltf_accessor_read_index(source.indices, i) : i;
            if (index >= positions->count || index > std::numeric_limits<UINT>::max())
                ImportError(filename, "mesh primitive contains an out-of-range index");
            result.indices[i] = static_cast<UINT>(index);
        }
        if (!mirrored)
        {
            for (size_t i = 0; i < result.indices.size(); i += 3)
                std::swap(result.indices[i + 1], result.indices[i + 2]);
        }
        if (!normals) GenerateNormals(result);
        if (!tangents) GenerateTangents(result);
        if (doubleSided)
        {
            if (result.vertices.size() > std::numeric_limits<UINT>::max() / 2)
                ImportError(filename, "double-sided mesh primitive has too many vertices");
            const UINT backVertexOffset = static_cast<UINT>(result.vertices.size());
            size_t frontVertexCount = result.vertices.size();
            size_t frontIndexCount = result.indices.size();
            result.vertices.reserve(frontVertexCount * 2);
            for (size_t i = 0; i < frontVertexCount; ++i)
            {
                auto vertex = result.vertices[i];
                vertex.Normal.x = -vertex.Normal.x;
                vertex.Normal.y = -vertex.Normal.y;
                vertex.Normal.z = -vertex.Normal.z;
                vertex.TangentU.x = -vertex.TangentU.x;
                vertex.TangentU.y = -vertex.TangentU.y;
                vertex.TangentU.z = -vertex.TangentU.z;
                result.vertices.push_back(vertex);
            }
            result.indices.reserve(frontIndexCount * 2);
            for (size_t i = 0; i < frontIndexCount; i += 3)
            {
                result.indices.push_back(result.indices[i] + backVertexOffset);
                result.indices.push_back(result.indices[i + 2] + backVertexOffset);
                result.indices.push_back(result.indices[i + 1] + backVertexOffset);
            }
        }
        return result;
    }

    void VisitNode(
        const std::string& filename,
        const cgltf_data& data,
        const cgltf_node& node,
        std::shared_ptr<GltfAsset>& asset,
        std::map<PrimitiveKey, size_t>& primitiveMap,
        const std::vector<size_t>& materialIndices,
        size_t defaultMaterialIndex,
        std::set<const cgltf_node*>& active)
    {
        if (!active.insert(&node).second)
            ImportError(filename, "node hierarchy contains a cycle");
        size_t nodeIndex = PointerIndex(&node, data.nodes, sizeof(cgltf_node));
        std::string nodeName = NameOrIndex(node.name, "node", nodeIndex);
        if (node.skin) ImportError(filename, nodeName + " uses unsupported skinning");
        if (node.weights_count) ImportError(filename, nodeName + " uses unsupported morph weights");

        XMMATRIX transform = ConvertedNodeTransform(node);
        bool mirrored = XMVectorGetX(XMMatrixDeterminant(transform)) < 0;
        if (node.mesh)
        {
            size_t meshIndex = PointerIndex(node.mesh, data.meshes, sizeof(cgltf_mesh));
            if (asset->meshes.size() < data.meshes_count)
                asset->meshes.resize(data.meshes_count);
            asset->meshes[meshIndex] = { node.mesh->name && *node.mesh->name ? node.mesh->name : NameOrIndex(nullptr, "mesh", meshIndex), meshIndex };
            for (size_t primitiveIndex = 0; primitiveIndex < node.mesh->primitives_count; ++primitiveIndex)
            {
                const auto& primitive = node.mesh->primitives[primitiveIndex];
                if (primitive.type != cgltf_primitive_type_triangles)
                    ImportError(filename, NameOrIndex(node.mesh->name, "mesh", meshIndex)
                        + " primitive[" + std::to_string(primitiveIndex) + "] is not TRIANGLES");
                if (primitive.targets_count)
                    ImportError(filename, NameOrIndex(node.mesh->name, "mesh", meshIndex)
                        + " primitive[" + std::to_string(primitiveIndex) + "] uses unsupported morph targets");
                if (primitive.has_draco_mesh_compression)
                    ImportError(filename, NameOrIndex(node.mesh->name, "mesh", meshIndex)
                        + " primitive[" + std::to_string(primitiveIndex) + "] uses unsupported Draco compression");

                size_t materialIndex = primitive.material
                    ? materialIndices[PointerIndex(primitive.material, data.materials, sizeof(cgltf_material))]
                    : defaultMaterialIndex;
                bool doubleSided = asset->materials[materialIndex].doubleSided;
                PrimitiveKey key = { &primitive, mirrored, doubleSided };
                auto existing = primitiveMap.find(key);
                size_t importedPrimitiveIndex;
                if (existing == primitiveMap.end())
                {
                    importedPrimitiveIndex = asset->primitives.size();
                    asset->primitives.push_back(ImportPrimitive(filename, primitive, mirrored, doubleSided, asset->materials[materialIndex].normalTextureIndex >= 0));
                    primitiveMap.emplace(key, importedPrimitiveIndex);
                }
                else
                {
                    importedPrimitiveIndex = existing->second;
                }

                GltfRenderPart part;
                part.meshIndex = meshIndex;
                part.nodeIndex = nodeIndex;
                part.sourcePrimitiveIndex = primitiveIndex;
                part.nodeName = node.name && *node.name ? node.name : nodeName;
                part.primitiveIndex = importedPrimitiveIndex;
                part.materialIndex = materialIndex;
                XMStoreFloat4x4(&part.transform, transform);
                asset->renderParts.push_back(part);
            }
        }

        for (size_t i = 0; i < node.children_count; ++i)
            VisitNode(filename, data, *node.children[i], asset, primitiveMap, materialIndices, defaultMaterialIndex, active);
        active.erase(&node);
    }
}

ShaderMaterial BuildPbrShaderMaterial(const PbrMaterialFactors& factors, bool& alphaBlend)
{
    ShaderMaterial result;
    const XMFLOAT4& color = factors.baseColor;
    const float metallic = factors.metallic;
    result.specularParams = XMFLOAT4(1, metallic, factors.roughness, factors.specular);
    result.specularColor = XMFLOAT4(factors.specularColor.x, factors.specularColor.y, factors.specularColor.z, 0);
    result.opticalParams.x = factors.ior;
    result.opticalParams.y = factors.transmission;
    // Ambient is the scene ambient's artistic 0.15 x base-color approximation.
    result.Ambient = XMFLOAT4(color.x * .15f, color.y * .15f, color.z * .15f, color.w);
    result.Diffuse = color;
    alphaBlend = factors.alphaMode == PbrAlphaMode::Blend;
    result.flags.z = factors.alphaMode == PbrAlphaMode::Mask
        ? factors.alphaCutoff
        : (alphaBlend ? 1.0f / 255.0f : 0.0f);
    result.flags.w = factors.unlit ? 1.0f : 0.0f;
    if (factors.transmission > 0.0f)
        alphaBlend = true; // Raster fallback: display opaque geometry through the surface.
    result.flags.x = alphaBlend ? 1.0f : 0.0f;
    return result;
}

void SetMaterialUvTransform(ShaderMaterial& material, MaterialTextureRole role,
    XMFLOAT2 offset, float rotation, XMFLOAT2 scale)
{
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    material.uvTransformRow0[role] = XMFLOAT4(cosine * scale.x, -sine * scale.y, offset.x, 0);
    material.uvTransformRow1[role] = XMFLOAT4(sine * scale.x, cosine * scale.y, offset.y, 0);
}

GltfTextureSource LoadMaterialTextureFile(const std::wstring& filename, int colorSpace)
{
    std::string narrow;
    if (int size = WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0, nullptr, nullptr))
    {
        std::vector<char> buffer(size);
        WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, buffer.data(), size, nullptr, nullptr);
        narrow = buffer.data();
    }
    auto extension = LowerExtension(narrow);
    if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" && extension != ".dds")
        throw std::runtime_error("material map '" + narrow + "' must be a PNG, JPEG, or DDS image");
    GltfTextureSource result;
    result.filename = filename;
    result.name = narrow;
    result.mimeType = extension == ".png" ? "image/png" : extension == ".dds" ? "image/vnd-ms.dds" : "image/jpeg";
    result.colorSpace = colorSpace;
    if (!ReadFileBytes(filename, result.encodedData)) throw std::runtime_error("material map '" + narrow + "' was not found");
    if (result.encodedData.empty()) throw std::runtime_error("material map '" + narrow + "' is empty");
    result.key = filename + (colorSpace == 2 ? L"#srgb" : L"#linear");
    return result;
}

std::shared_ptr<const GltfTextureSource> LoadSharedMaterialTextureFile(const std::wstring& filename, int colorSpace)
{
    // Objects sharing a map share its bytes, so a texture used by many objects is read
    // once. Entries live while any object holds them and are reread if the file changes.
    struct CacheEntry
    {
        std::weak_ptr<const GltfTextureSource> source;
        FILETIME lastWrite;
        ULARGE_INTEGER size;
    };
    static std::mutex cacheMutex;
    static std::map<std::wstring, CacheEntry> cache;

    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    const bool exists = GetFileAttributesExW(filename.c_str(), GetFileExInfoStandard, &attributes) != 0;
    ULARGE_INTEGER size;
    size.LowPart = attributes.nFileSizeLow;
    size.HighPart = attributes.nFileSizeHigh;
    const std::wstring key = filename + (colorSpace == 2 ? L"#srgb" : L"#linear");

    std::lock_guard<std::mutex> lock(cacheMutex);
    if (exists)
    {
        auto found = cache.find(key);
        if (found != cache.end() && CompareFileTime(&found->second.lastWrite, &attributes.ftLastWriteTime) == 0 &&
            found->second.size.QuadPart == size.QuadPart)
        {
            if (auto shared = found->second.source.lock()) return shared;
        }
    }
    auto loaded = std::make_shared<const GltfTextureSource>(LoadMaterialTextureFile(filename, colorSpace));
    for (auto it = cache.begin(); it != cache.end();)
        it = it->second.source.expired() ? cache.erase(it) : std::next(it);
    cache[key] = { loaded, attributes.ftLastWriteTime, size };
    return loaded;
}

void GltfAsset::AppendGeometry(GeometryBufferData& geometryData)
{
    if (!geometryData.appendedAssets.insert(this).second) return;
    for (auto& primitive : primitives)
    {
        primitive.bufferOffsets.VertexOffset = static_cast<UINT>(geometryData.vertices.size());
        primitive.bufferOffsets.VertexCount = static_cast<UINT>(primitive.vertices.size());
        primitive.bufferOffsets.IndexOffset = static_cast<UINT>(geometryData.indices.size());
        primitive.bufferOffsets.IndexCount = static_cast<UINT>(primitive.indices.size());
        geometryData.vertices.insert(geometryData.vertices.end(), primitive.vertices.begin(), primitive.vertices.end());
        geometryData.indices.insert(geometryData.indices.end(), primitive.indices.begin(), primitive.indices.end());
    }
}

std::shared_ptr<GltfAsset> LoadGltfAsset(const std::string& inputFilename)
{
    const std::string filename = CanonicalPath(inputFilename);
    cgltf_options options = {};
    cgltf_data* rawData = nullptr;
    cgltf_result result = cgltf_parse_file(&options, filename.c_str(), &rawData);
    if (result != cgltf_result_success)
        ImportError(filename, "cannot parse glTF: " + ResultName(result));
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(rawData, &cgltf_free);

    result = cgltf_load_buffers(&options, data.get(), filename.c_str());
    if (result != cgltf_result_success)
        ImportError(filename, "cannot load glTF buffers: " + ResultName(result));
    result = cgltf_validate(data.get());
    if (result != cgltf_result_success)
        ImportError(filename, "glTF validation failed: " + ResultName(result));

    ValidateRequiredExtensions(filename, *data);
    if (data->animations_count) ImportError(filename, "animations are not supported");
    if (data->skins_count) ImportError(filename, "skins are not supported");
    for (size_t i = 0; i < data->buffer_views_count; ++i)
        if (data->buffer_views[i].has_meshopt_compression)
            ImportError(filename, "EXT_meshopt_compression is not supported");

    const cgltf_scene* scene = data->scene;
    if (!scene && data->scenes_count) scene = &data->scenes[0];
    if (!scene) ImportError(filename, "asset has no scene");

    auto asset = std::make_shared<GltfAsset>();
    asset->sourceFilename = filename;
    std::map<std::pair<const cgltf_texture*, int>, size_t> textureMap;
    std::vector<size_t> materialIndices(data->materials_count);
    for (size_t i = 0; i < data->materials_count; ++i)
    {
        materialIndices[i] = asset->materials.size();
        asset->materials.push_back(ImportMaterial(filename, *data, &data->materials[i], asset->textures, textureMap));
    }
    size_t defaultMaterialIndex = asset->materials.size();
    asset->materials.push_back(ImportMaterial(filename, *data, nullptr, asset->textures, textureMap));

    std::map<PrimitiveKey, size_t> primitiveMap;
    std::set<const cgltf_node*> active;
    for (size_t i = 0; i < scene->nodes_count; ++i)
        VisitNode(filename, *data, *scene->nodes[i], asset, primitiveMap, materialIndices, defaultMaterialIndex, active);
    if (asset->renderParts.empty()) ImportError(filename, "selected scene contains no triangle meshes");

    XMVECTOR boundsMin = XMVectorReplicate(std::numeric_limits<float>::max());
    XMVECTOR boundsMax = XMVectorReplicate(std::numeric_limits<float>::lowest());
    for (const auto& part : asset->renderParts)
    {
        XMMATRIX transform = XMLoadFloat4x4(&part.transform);
        for (const auto& vertex : asset->primitives[part.primitiveIndex].vertices)
        {
            XMVECTOR position = XMVector3TransformCoord(XMLoadFloat3(&vertex.Pos), transform);
            boundsMin = XMVectorMin(boundsMin, position);
            boundsMax = XMVectorMax(boundsMax, position);
        }
    }
    XMStoreFloat3(&asset->boundsMin, boundsMin);
    XMStoreFloat3(&asset->boundsMax, boundsMax);
    return asset;
}
