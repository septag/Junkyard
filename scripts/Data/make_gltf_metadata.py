import argparse
import json
import sys
import os
from enum import Enum

ArgsParser = argparse.ArgumentParser(description='')
ArgsParser.add_argument('--recurse-dir', help='Recursively search a directory for GLTF files and process them', action='store_true', default=False)
ArgsParser.add_argument('--force-bc1-for-albedo', help='Instead of BC7 as a default TC format for Albedo, use BC1', action='store_true', default=False)
ArgsParser.add_argument('gltf_path', help='Path to the gltf file', default=None, nargs="?")
Args = ArgsParser.parse_args(sys.argv[1:])
ProcessedTextures = set()

class TextureType(Enum):
    BASE_COLOR = 1
    NORMAL = 2
    OCCLUSION = 3
    ROUGHNESS = 4

def CreateMetadata(gltfFilepath, srcFilepath, type:TextureType):
    global ProcessedTextures
    if srcFilepath in ProcessedTextures:
        return
    ProcessedTextures.add(srcFilepath)
    
    filepath = os.path.abspath(os.path.join(os.path.dirname(gltfFilepath), srcFilepath)) + ".asset"
    jsonData = {}
    jsonData['generateMips'] = True
    jsonData['pc'] = {}

    if type == TextureType.BASE_COLOR:
        jsonData['pc']['format'] = "bc7" if not Args.force_bc1_for_albedo else "bc1"
        jsonData['sRGB'] = True
    elif type == TextureType.NORMAL:
        jsonData['pc']['format'] = "bc5"
    elif type == TextureType.ROUGHNESS or type == TextureType.OCCLUSION:
        jsonData['pc']['format'] = "bc1"

    with open(filepath, 'w') as file:
        file.write(json.dumps(jsonData, indent=2))

def ProcessGLTF(gltfFilepath):
    materials = []
    with open(gltfFilepath, 'r') as file:
        data = json.load(file)

    if 'materials' in data:
        materials = data['materials']
        textures = data['textures']
        images = data['images']

    for material in materials:
        if 'normalTexture' in material:
            normalTexture = material['normalTexture']['index']
        if 'occlusionTexture' in material:
            occlusionTexture = material['occlusionTexture']['index']
        if 'pbrMetallicRoughness' in material:
            metallicRoughness = material['pbrMetallicRoughness']
            if 'baseColorTexture' in metallicRoughness:
                baseColorTexture = metallicRoughness['baseColorTexture']['index']
            if 'metallicRoughnessTexture' in metallicRoughness:
                metallicRoughnessTexture = metallicRoughness['metallicRoughnessTexture']['index']

        print("Material:", material['name'])
        if 'baseColorTexture' in locals():
            baseColorTexturePath = images[textures[baseColorTexture]['source']]['uri']
            print('\tBaseColor:', baseColorTexturePath)
            CreateMetadata(gltfFilepath, baseColorTexturePath, TextureType.BASE_COLOR)

        if 'normalTexture' in locals():
            normalTexturePath = images[textures[normalTexture]['source']]['uri']
            print('\tNormal:', normalTexturePath)
            CreateMetadata(gltfFilepath, normalTexturePath, TextureType.NORMAL)

        if 'occlusionTexture' in locals():
            occlusionTexturePath = images[textures[occlusionTexture]['source']]['uri']
            print('\tOcclusion:', occlusionTexturePath)
            CreateMetadata(gltfFilepath, occlusionTexturePath, TextureType.OCCLUSION)

        if 'metallicRoughnessTexture' in locals():
            metallicRoughnessTexturePath = images[textures[metallicRoughnessTexture]['source']]['uri']
            print('\tRoughness:', metallicRoughnessTexturePath)
            CreateMetadata(gltfFilepath, metallicRoughnessTexturePath, TextureType.ROUGHNESS)

if not Args.gltf_path and not Args.recurse_dir:
    print('Error: gltf path or --recurse_dir is not provided in arguments')
    sys.exit(-1)

if Args.recurse_dir:
    print('Recurse directory:', Args.recurse_dir)
    gltfFiles = []
    for root, _, files in os.walk(Args.recurse_dir):
        for file in files:
            if file.endswith('.gltf'):
                gltfFiles.append(os.path.join(root, file))

    for gltfFile in gltfFiles:
        print(gltfFile)
        ProcessGLTF(gltfFile)
else:
    ProcessGLTF(Args.gltf_path)



    


