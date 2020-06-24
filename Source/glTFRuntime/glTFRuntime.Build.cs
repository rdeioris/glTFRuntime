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
                "Core"
				// ... add other public dependencies that you statically link with here ...
			}
            );


        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "SlateCore",
                "JSON",
                "MeshDescription",
                "StaticMeshDescription",
                "RenderCore",
                "RHI",
                "ApplicationCore",
                "DesktopPlatform",
                "Http"
				// ... add private dependencies that you statically link with here ...	
			}
            );

        if (Target.Type == TargetType.Editor)
        {
            PrivateDependencyModuleNames.Add("SkeletalMeshUtilitiesCommon");
        }


        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
				// ... add any modules that your module loads dynamically here ...
			}
            );
    }
}
