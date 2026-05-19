#include "material.h"
#include "delegate.h"
#include "renderer.h"
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <pxr/base/tf/token.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/imaging/hio/types.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/usd/sdf/assetPath.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace pxr {

static const TfToken _diffuseColorToken("diffuseColor");
static const TfToken _emissiveColorToken("emissiveColor");
static const TfToken _fileToken("file");
static const TfToken _metallicToken("metallic");
static const TfToken _normalToken("normal");
static const TfToken _occlusionToken("occlusion");
static const TfToken _opacityToken("opacity");
static const TfToken _roughnessToken("roughness");

static bool _TokenNamesInput(const TfToken &token, const TfToken &inputName) {
    if (token == inputName) {
        return true;
    }
    const std::string tokenText = token.GetString();
    const std::string inputText = inputName.GetString();
    const size_t colon = tokenText.rfind(':');
    return colon != std::string::npos && tokenText.substr(colon + 1) == inputText;
}

static const HdMaterialNode *_FindNode(const HdMaterialNetwork &network,
                                       const SdfPath &path) {
    for (const HdMaterialNode &node : network.nodes) {
        if (node.path == path) {
            return &node;
        }
    }
    return nullptr;
}

static const HdMaterialNode *_FindTextureNodeForInput(
    const HdMaterialNetwork &network,
    const SdfPath &nodePath,
    const TfToken &inputName,
    std::vector<SdfPath> *visited) {
    if (std::find(visited->begin(), visited->end(), nodePath) != visited->end()) {
        return nullptr;
    }
    visited->push_back(nodePath);

    for (const HdMaterialRelationship &rel : network.relationships) {
        SdfPath upstreamPath;
        TfToken upstreamOutput;

        if (rel.outputId == nodePath && _TokenNamesInput(rel.outputName, inputName)) {
            upstreamPath = rel.inputId;
            upstreamOutput = rel.inputName;
        } else if (rel.inputId == nodePath &&
                   _TokenNamesInput(rel.inputName, inputName)) {
            upstreamPath = rel.outputId;
            upstreamOutput = rel.outputName;
        } else {
            continue;
        }

        const HdMaterialNode *upstream = _FindNode(network, upstreamPath);
        if (!upstream) {
            continue;
        }
        if (upstream->identifier == TfToken("UsdUVTexture")) {
            return upstream;
        }

        if (const HdMaterialNode *texture = _FindTextureNodeForInput(
                network, upstream->path, upstreamOutput, visited)) {
            return texture;
        }
    }

    return nullptr;
}

static const HdMaterialNode *_FindTextureNodeForInput(
    const HdMaterialNetwork &network,
    const SdfPath &previewSurfacePath,
    const TfToken &inputName) {
    std::vector<SdfPath> visited;
    return _FindTextureNodeForInput(network, previewSurfacePath, inputName,
                                    &visited);
}

static Eigen::Vector3f _ClampedColor(const Eigen::Vector3f &c) {
    return c.cwiseMax(0.0f).cwiseMin(1.0f);
}

static bool _ReadColor(const VtValue &value, Eigen::Vector3f *out) {
    if (value.IsHolding<GfVec3f>()) {
        const GfVec3f &c = value.UncheckedGet<GfVec3f>();
        *out = _ClampedColor(Eigen::Vector3f(c[0], c[1], c[2]));
        return true;
    }
    if (value.IsHolding<GfVec4f>()) {
        const GfVec4f &c = value.UncheckedGet<GfVec4f>();
        *out = _ClampedColor(Eigen::Vector3f(c[0], c[1], c[2]));
        return true;
    }
    if (value.IsHolding<float>()) {
        float c = std::clamp(value.UncheckedGet<float>(), 0.0f, 1.0f);
        *out = Eigen::Vector3f(c, c, c);
        return true;
    }
    return false;
}

static bool _ReadFloat(const VtValue &value, float *out) {
    if (value.IsHolding<float>()) {
        *out = value.UncheckedGet<float>();
        return true;
    }
    if (value.IsHolding<double>()) {
        *out = static_cast<float>(value.UncheckedGet<double>());
        return true;
    }
    return false;
}

static bool _ReadImageAsRgb(const HioImageSharedPtr &image, ImageTexture *texture) {
    HioFormat format = image->GetFormat();
    if (format == HioFormatInvalid || HioIsCompressed(format)) {
        return false;
    }

    const int width = image->GetWidth();
    const int height = image->GetHeight();
    const int components = HioGetComponentCount(format);
    const size_t pixelSize = HioGetDataSizeOfFormat(format);
    if (width <= 0 || height <= 0 || components <= 0 || pixelSize == 0) {
        return false;
    }

    std::vector<uint8_t> source(width * height * pixelSize);
    HioImage::StorageSpec storage;
    storage.width = width;
    storage.height = height;
    storage.format = format;
    storage.flipped = true;
    storage.data = source.data();
    if (!image->Read(storage)) {
        return false;
    }

    std::vector<float> pixels(width * height * 3, 1.0f);
    const HioType type = HioGetHioType(format);
    for (int i = 0; i < width * height; ++i) {
        const uint8_t *src = source.data() + i * pixelSize;
        float c[3] = {1.0f, 1.0f, 1.0f};
        if (type == HioTypeUnsignedByte || type == HioTypeUnsignedByteSRGB) {
            for (int j = 0; j < std::min(3, components); ++j) {
                c[j] = src[j] / 255.0f;
            }
        } else if (type == HioTypeFloat) {
            const float *f = reinterpret_cast<const float *>(src);
            for (int j = 0; j < std::min(3, components); ++j) {
                c[j] = std::clamp(f[j], 0.0f, 1.0f);
            }
        } else {
            return false;
        }
        if (components == 1) {
            c[1] = c[0];
            c[2] = c[0];
        }
        pixels[i * 3 + 0] = c[0];
        pixels[i * 3 + 1] = c[1];
        pixels[i * 3 + 2] = c[2];
    }
    texture->width = width;
    texture->height = height;
    texture->pixels = std::move(pixels);
    texture->valid = true;
    return true;
}

static bool _LoadTextureFromNode(const HdMaterialNode *node,
                                 ImageTexture *texture) {
    texture->valid = false;
    texture->pixels.clear();
    texture->width = 0;
    texture->height = 0;

    if (!node) {
        return false;
    }
    auto fileIt = node->parameters.find(_fileToken);
    if (fileIt == node->parameters.end() ||
        !fileIt->second.IsHolding<SdfAssetPath>()) {
        return false;
    }

    const SdfAssetPath &asset = fileIt->second.UncheckedGet<SdfAssetPath>();
    std::string texturePath = asset.GetResolvedPath();
    if (texturePath.empty()) {
        texturePath = asset.GetAssetPath();
    }
    if (texturePath.empty()) {
        return false;
    }

    static std::mutex textureCacheMutex;
    static std::unordered_map<std::string, ImageTexture> textureCache;
    {
        std::lock_guard<std::mutex> lock(textureCacheMutex);
        auto it = textureCache.find(texturePath);
        if (it != textureCache.end()) {
            *texture = it->second;
            return texture->valid;
        }
    }

    HioImageSharedPtr image = HioImage::OpenForReading(texturePath);
    bool loaded = image && _ReadImageAsRgb(image, texture);
    if (loaded) {
        std::lock_guard<std::mutex> lock(textureCacheMutex);
        textureCache.emplace(texturePath, *texture);
    }
    return loaded;
}

HdTerminalMaterial::HdTerminalMaterial(SdfPath const &id) : HdMaterial(id) {}

void HdTerminalMaterial::Sync(HdSceneDelegate *sceneDelegate,
                              HdRenderParam * /*renderParam*/,
                              HdDirtyBits *dirtyBits) {
    
    if (*dirtyBits & HdMaterial::DirtyResource) {
        auto id = GetId();
        
        // 1. Get the Material Network
        // This contains all nodes (Surface, Textures, PrimvarReaders)
        VtValue val = sceneDelegate->GetMaterialResource(id);
        
        if (val.IsHolding<HdMaterialNetworkMap>()) {
            auto &networkMap = val.UncheckedGet<HdMaterialNetworkMap>();
            
            // 2. Find the "Surface" network
            auto it = networkMap.map.find(HdMaterialTerminalTokens->surface);
            if (it != networkMap.map.end()) {
                const HdMaterialNetwork &network = it->second;
                
                MaterialData material;
                const HdMaterialNode *previewSurfaceNode = nullptr;
                
                for (const auto &node : network.nodes) {
                    if (node.identifier == TfToken("UsdPreviewSurface")) {
                        auto colorIt = node.parameters.find(_diffuseColorToken);
                        if (colorIt != node.parameters.end()) {
                            _ReadColor(colorIt->second, &material.baseColor);
                        }
                        auto emissiveIt = node.parameters.find(_emissiveColorToken);
                        if (emissiveIt != node.parameters.end()) {
                            _ReadColor(emissiveIt->second, &material.emissiveColor);
                        }
                        auto metallicIt = node.parameters.find(_metallicToken);
                        if (metallicIt != node.parameters.end()) {
                            _ReadFloat(metallicIt->second, &material.metallic);
                        }
                        auto roughnessIt = node.parameters.find(_roughnessToken);
                        if (roughnessIt != node.parameters.end()) {
                            _ReadFloat(roughnessIt->second, &material.roughness);
                        }
                        auto occlusionIt = node.parameters.find(_occlusionToken);
                        if (occlusionIt != node.parameters.end()) {
                            _ReadFloat(occlusionIt->second, &material.occlusion);
                        }
                        auto opacityIt = node.parameters.find(_opacityToken);
                        if (opacityIt != node.parameters.end()) {
                            _ReadFloat(opacityIt->second, &material.opacity);
                        }
                        previewSurfaceNode = &node;
                        break;
                    }
                }

                if (previewSurfaceNode) {
                    _LoadTextureFromNode(
                        _FindTextureNodeForInput(network, previewSurfaceNode->path,
                                                 _diffuseColorToken),
                        &material.baseColorTexture);
                    _LoadTextureFromNode(
                        _FindTextureNodeForInput(network, previewSurfaceNode->path,
                                                 _normalToken),
                        &material.normalTexture);
                    _LoadTextureFromNode(
                        _FindTextureNodeForInput(network, previewSurfaceNode->path,
                                                 _occlusionToken),
                        &material.occlusionTexture);
                }

                material.metallic = std::clamp(material.metallic, 0.0f, 1.0f);
                material.roughness = std::clamp(material.roughness, 0.04f, 1.0f);
                material.occlusion = std::clamp(material.occlusion, 0.0f, 1.0f);
                material.opacity = std::clamp(material.opacity, 0.0f, 1.0f);

                HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();
                HdTerminalDelegate *delegate =
                    static_cast<HdTerminalDelegate *>(renderIndex.GetRenderDelegate());
                Renderer *renderer = delegate->GetRenderer();

                std::lock_guard<std::mutex> lock(renderer->get_scene_mutex());
                renderer->get_materials()[id] = std::move(material);
            }
        }
    }
    *dirtyBits = HdChangeTracker::Clean;
}

} // namespace pxr
