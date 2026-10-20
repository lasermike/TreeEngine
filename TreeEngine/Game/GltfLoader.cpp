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
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
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
            primitive.vertices[i].TangentU = tangentValue;
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
            if (strcmp(data.extensions_required[i], "KHR_materials_specular") != 0)
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
        result.shaderMaterial.Ambient = XMFLOAT4(.15f, .15f, .15f, 1);
        result.shaderMaterial.Diffuse = XMFLOAT4(1, 1, 1, 1);
        result.shaderMaterial.Specular = XMFLOAT4(.04f, .04f, .04f, 16);
        result.shaderMaterial.Reflect = XMFLOAT4(0, 0, 0, 1);
        result.shaderMaterial.specularParams = XMFLOAT4(1, 1, 1, 1);
        if (!material) return result;

        size_t materialIndex = PointerIndex(material, data.materials, sizeof(cgltf_material));
        std::string materialName = NameOrIndex(material->name, "material", materialIndex);
        if (material->has_pbr_specular_glossiness || material->has_clearcoat || material->has_transmission
            || material->has_volume || material->has_ior || material->has_sheen
            || material->has_emissive_strength || material->has_iridescence || material->has_diffuse_transmission
            || material->has_anisotropy || material->has_dispersion)
            ImportError(filename, materialName + " uses an unsupported material extension");
        if (material->occlusion_texture.texture || material->emissive_texture.texture)
            ImportError(filename, materialName + " uses unsupported occlusion or emissive textures");
        if (material->emissive_factor[0] != 0 || material->emissive_factor[1] != 0 || material->emissive_factor[2] != 0)
            ImportError(filename, materialName + " uses unsupported emissive output");

        const auto& pbr = material->pbr_metallic_roughness;
        float metallic = pbr.metallic_factor;
        float roughness = pbr.roughness_factor;
        if (!std::isfinite(metallic) || metallic < 0 || metallic > 1)
            ImportError(filename, materialName + " metallicFactor must be in [0, 1]");
        if (!std::isfinite(roughness) || roughness < 0 || roughness > 1)
            ImportError(filename, materialName + " roughnessFactor must be in [0, 1]");
        result.shaderMaterial.specularParams = XMFLOAT4(1, metallic, roughness, 1);
        const float* color = pbr.base_color_factor;
        result.shaderMaterial.Ambient = XMFLOAT4(color[0] * .15f, color[1] * .15f, color[2] * .15f, color[3]);
        result.shaderMaterial.Diffuse = XMFLOAT4(color[0], color[1], color[2], color[3]);
        result.shaderMaterial.Specular = XMFLOAT4(
            .04f + (color[0] - .04f) * metallic,
            .04f + (color[1] - .04f) * metallic,
            .04f + (color[2] - .04f) * metallic,
            std::max(1.0f, (1.0f - roughness) * 128.0f));
        result.shaderMaterial.Reflect = XMFLOAT4(color[0] * metallic, color[1] * metallic, color[2] * metallic, 1);
        result.alphaBlend = material->alpha_mode == cgltf_alpha_mode_blend;
        result.shaderMaterial.flags.x = result.alphaBlend ? 1.0f : 0.0f;
        result.shaderMaterial.flags.z = material->alpha_mode == cgltf_alpha_mode_mask
            ? material->alpha_cutoff
            : (result.alphaBlend ? 1.0f / 255.0f : 0.0f);
        result.shaderMaterial.flags.w = material->unlit ? 1.0f : 0.0f;
        result.doubleSided = material->double_sided != 0;

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
            result.shaderMaterial.specularParams = XMFLOAT4(1, metallic, roughness, spec.specular_factor);
            result.shaderMaterial.specularColor = XMFLOAT4(spec.specular_color_factor[0],
                spec.specular_color_factor[1], spec.specular_color_factor[2], 0);
        }

        auto importView = [&](const cgltf_texture_view& textureView, int colorSpace, bool& wrap) -> int
        {
            if (textureView.texture)
            {
                if (textureView.texcoord != 0)
                    ImportError(filename, materialName + " uses unsupported texture coordinate set TEXCOORD_" + std::to_string(textureView.texcoord));
                if (textureView.has_transform)
                    ImportError(filename, materialName + " uses unsupported KHR_texture_transform");
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
                            std::ifstream input(source.filename.c_str(), std::ios::binary);
                            source.encodedData.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
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
        result.textureIndex = importView(pbr.base_color_texture, 2, result.wrapTexture);
        result.shaderMaterial.specularColor.w = result.wrapTexture ? 0.0f : 1.0f;
        bool metallicRoughnessWrap = true;
        result.metallicRoughnessTextureIndex = importView(pbr.metallic_roughness_texture, 1, metallicRoughnessWrap);
        result.shaderMaterial.metallicRoughnessTextures.y = metallicRoughnessWrap ? 0.0f : 1.0f;
        if (material->normal_texture.texture)
        {
            if (!std::isfinite(material->normal_texture.scale) || material->normal_texture.scale < 0)
                ImportError(filename, materialName + " normalTexture.scale must be finite and nonnegative");
            bool normalWrap = true;
            result.normalTextureIndex = importView(material->normal_texture, 1, normalWrap);
            result.shaderMaterial.normalTextures.y = material->normal_texture.scale;
            result.shaderMaterial.normalTextures.z = normalWrap ? 0.0f : 1.0f;
        }
        result.shaderMaterial.flags.y = result.textureIndex >= 0 ? 1.0f : 0.0f;
        if (material->has_specular)
        {
            bool strengthWrap = true, colorWrap = true;
            result.specularTextureIndex = importView(material->specular.specular_texture, 1, strengthWrap);
            result.specularColorTextureIndex = importView(material->specular.specular_color_texture, 2, colorWrap);
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
                // SimpleVertex currently has no place to retain tangentComponents[3] (handedness).
                XMFLOAT3 tangent(tangentComponents[0], tangentComponents[1], tangentComponents[2]);
                tangent.z = -tangent.z;
                result.vertices[i].TangentU = NormalizeOr(tangent, XMFLOAT3(1, 0, 0));
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
