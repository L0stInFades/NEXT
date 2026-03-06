# Asset Pipeline Quickstart (Models)

This repo uses an offline compile step: source assets (DCC exports) are compiled into `NEXT` binary assets and then bundled into `.npkg` packages for runtime loading.

## Current Supported Import Formats

- Mesh: `.obj` (triangulated on import)
- Texture: `.tga` (uncompressed true-color, 24/32-bit)

FBX/PNG/JPG are not supported yet in the compiler (export OBJ/TGA as a workaround).

## 1) Build The Tools

From the repo root:

```powershell
cmake --build build --config Debug --target next_assetc
```

The executable will be at `build/bin/Debug/next_assetc.exe` (or `Release`).

## 2) Import A Model Into A Package

Put your exported OBJ somewhere (example: `SourceAssets/house.obj`), then:

```powershell
build/bin/Debug/next_assetc.exe import SourceAssets/house.obj assets/house.npkg
```

This will:
- compile a mesh into `assets/house_compiled/house.mesh`
- create `assets/house.npkg`

## 3) Load The Mesh At Runtime

Runtime loads by **asset name**, which defaults to the output filename stem.

```cpp
auto& am = Next::AssetManager::Instance();
am.LoadPackage("assets/house.npkg");
auto mesh = am.LoadAssetSync("house");
```

Note: currently the demo loads assets but does not render imported meshes yet. It is safe for gameplay teams to start integrating content and building higher-level systems while rendering integration proceeds.

