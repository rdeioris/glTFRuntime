# glTFRuntime
Unreal Engine 4 Plugin for loading glTF assets at runtime 

Join the Discord Channel: https://discord.gg/DzS7MHy

Check the Features Showcase: https://www.youtube.com/watch?v=6058JA8wX8I

![glTFRuntime](Docs/Screenshots/glTFRuntime512.jpg?raw=true "glTFRuntime")

# Features

* Allows to load Static Meshes, Skeletal Meshes, Animations, Hierarchies, Materials and Textures from glTF 2.0 Embedded files.
* Assets can be loaded on the fly both in PIE and Packaged Games.
* Assets can be loaded from the filesystem, http servers or raw json strings.
* Supports generating ad-hoc Skeletons or reusing already existing ones (a gltf Exporter for Skeletons is included too)
* Non skeletons/skins-related animations are exposed as Curves.
* Full support for PBR Materials
* Support for glTF 2.0 Sparse Accessors
* Support for multiple texture coordinates/channels/uvs
* Allows to define Static Meshes collisions (Spheres, Boxes, Complex Meshes) at runtime.

# Quickstart

Consider buying the plugin from the Epic Marketplace, you will get automatic installation and you will support the project.

If you want to build from sources, just start with a C++ project, and clone the master branch into the Plugins/ directory of your project, regenerate the solution files and rebuild the project.

Once the plugin is enabled you will get 3 new main C++/Blueprint functions:

![MainFunctions](Docs/Screenshots/MainFunctions.PNG?raw=true "MainFunctions")

Let's start with remote asset loading (we will use the official glTF 2.0 samples), open your level blueprint and on the BeginPlay Event, trigger
the runtime asset loading:

![UrlDuck](Docs/Screenshots/UrlDuck.PNG?raw=true "UrlDuck")

A bunch of notes:

* We are loading the asset from the https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Models/master/2.0/Duck/glTF-Embedded/Duck.gltf url
* The glTFRuntimeAssetActor is a ready-to-use Actor class included in the plugin. It is perfect for testing, but you are encouraged to implement more advanced structures.
* The glTFLoadAssetFromUrl function is an asynchronous one, the related event will be triggered when the asset is loaded.
* The glTFRuntimeAssetActor requires a glTFRuntimeAsset to correctly spawn.

If all goes well you should see the Collada Duck:

![ColladaDuck](Docs/Screenshots/ColladaDuck.PNG?raw=true "ColladaDuck")

# Loading Scenes

Time to run your favourite DCC to create a glTF file.

Here i am using Blender 2.83, and i will create a simple scene with Suzanne and a Hat (well a cone) on the center:

![SuzanneWithHat](Docs/Screenshots/SuzanneWithHat.PNG?raw=true "SuzanneWithHat")

Now select both Suzanne and the Cone/Hat and select the File/Export/glTF2.0 menu option

In the export dialog ensure to select the gltf 2.0 Embedded format and to include the selected objects:

<img src="Docs/Screenshots/BlenderExport.PNG?raw=true" alt="BlenderExport" width="50%"/>

Now back to the Level Blueprint:

![LoadSuzanneWithHat](Docs/Screenshots/LoadSuzanneWithHat.PNG?raw=true "LoadSuzanneWithHat")

# Loading Static Meshes

# Loading Skeletal Meshes

# Skeletal Animations

# Curve Animations

# Errors Management

# Integration with LuaMachine

# TODO/WIP

* LODs
* Vertex Colors
* Async Loading
* Import Scenes as Sequencer Assets
* Generate Physics Assets at runtime
* Support for glTF binary files
* Instancing Extension (https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Vendor/EXT_mesh_gpu_instancing/README.md)
* MSFT_lod extension (https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Vendor/MSFT_lod/README.md)
* StaticMeshes/SkeletalMeshes merger (combine multiple meshes in a single one)

# Commercial Support

Commercial support is offered by Unbit (Rome, Italy).
Just enter the discord server and direct message the admin, or drop a mail to info at unbit dot it

# Thanks

Silvia Sicks for the glTFRuntime Logo.
