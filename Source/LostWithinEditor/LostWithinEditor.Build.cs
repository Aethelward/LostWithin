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
			"NavigationSystem",
			"InputCore",
			"UnrealEd",
			"LostWithin"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate",
			"SlateCore",
			"AIModule",
			"AssetRegistry",
			"BlueprintGraph",
			"KismetCompiler",
			"PhysicsCore"
		});
	}
}
