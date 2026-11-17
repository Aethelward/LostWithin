// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LostWithinEditor : ModuleRules
{
	public LostWithinEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"UnrealEd",
			"LostWithin"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate",
			"SlateCore",
			"AIModule",
			"NavigationSystem",
			"AssetRegistry",
			"BlueprintGraph",
			"KismetCompiler",
			"PhysicsCore"
		});
	}
}
