// Copyright 2020, Roberto De Ioris.

using UnrealBuildTool;

public class glTFRuntime : ModuleRules
{
    public glTFRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;

        PublicIncludePaths.AddRange(
            new string[] {
				// ... add public include paths required here ...
			}
            );


        PrivateIncludePaths.AddRange(
            new string[] {
				// ... add other private include paths required here ...
			}
            );


        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
				// ... add other public dependencies that you statically link with here ...
                "ProceduralMeshComponent"
            }
            );


        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "JSON",
                "MeshDescription",
                "StaticMeshDescription",
                "RenderCore",
                "RHI",
                "ApplicationCore",
                "Http",
                "PhysicsCore",
				// ... add private dependencies that you statically link with here ...	
			}
            );

        if (Target.Type == TargetType.Editor)
        {
            PrivateDependencyModuleNames.Add("SkeletalMeshUtilitiesCommon");
            PrivateDependencyModuleNames.Add("UnrealEd");
        }


        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
				// ... add any modules that your module loads dynamically here ...
			}
            );
    }
}
