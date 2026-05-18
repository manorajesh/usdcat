#include "material.h"
#include "delegate.h"
#include "renderer.h"
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/token.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Define tokens we need to look for in the material graph
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (diffuseColor)
    (file)
    (st)
);

namespace pxr {

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
                
                // 3. Find the UsdPreviewSurface node (usually the last one connected to terminal)
                // In a robust implementation, you follow the relationships. 
                // For simplicity, we search for a node with "file" input or specific ID.
                
                std::string foundTexturePath;
                
                // Iterate over all nodes in the network to find a Texture node
                for (const auto &node : network.nodes) {
                    if (node.identifier == TfToken("UsdUVTexture")) {
                        // Check if this texture has a 'file' parameter
                        auto fileIt = node.parameters.find(_tokens->file);
                        if (fileIt != node.parameters.end()) {
                             if (fileIt->second.IsHolding<SdfAssetPath>()) {
                                foundTexturePath = fileIt->second.UncheckedGet<SdfAssetPath>().GetResolvedPath();
                                if (foundTexturePath.empty()) {
                                    foundTexturePath = fileIt->second.UncheckedGet<SdfAssetPath>().GetAssetPath();
                                }
                                break; // Found a texture!
                             }
                        }
                    }
                }

                // 4. Load the Image using Hio
                if (!foundTexturePath.empty()) {
                    // Use HioImage to read the file
                    HioImageSharedPtr image = HioImage::OpenForReading(foundTexturePath);
                    if (image) {
                        HioImage::StorageSpec storage;
                        storage.width = image->GetWidth();
                        storage.height = image->GetHeight();
                        storage.format = HioFormatFloat32Vec3; // Force convert to float RGB
                        storage.flipped = true; // OpenGL style flip often needed
                        
                        // Allocate memory
                        std::vector<float> textureData(storage.width * storage.height * 3);
                        storage.data = textureData.data();
                        
                        if (image->Read(storage)) {
                            // 5. Send to Renderer
                            HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();
                            HdTerminalDelegate *delegate = 
                                static_cast<HdTerminalDelegate *>(renderIndex.GetRenderDelegate());
                            Renderer *renderer = delegate->GetRenderer();
                            
                            Texture &tex = renderer->get_textures()[id]; // Map SdfPath -> Texture
                            tex.width = storage.width;
                            tex.height = storage.height;
                            tex.pixels = std::move(textureData);
                            tex.valid = true;
                        }
                    }
                }
            }
        }
    }
    *dirtyBits = HdChangeTracker::Clean;
}

} // namespace pxr
