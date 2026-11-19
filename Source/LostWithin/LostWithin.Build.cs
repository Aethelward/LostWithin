// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LostWithin : ModuleRules
{
	public LostWithin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"AssetRegistry",
			"AnimGraphRuntime",
			"NavigationSystem",
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "PhysicsCore", "AudioWidgets" });

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "EngineSettings", "UnrealEd" });
		}

		PublicIncludePaths.AddRange(new string[] {
			"LostWithin",
			"LostWithin/Animation",
			"LostWithin/Components",
			"LostWithin/Interaction",
			"LostWithin/Inventory",
			"LostWithin/UI",
			"LostWithin/Variant_Horror",
			"LostWithin/Variant_Horror/UI",
			"LostWithin/Variant_Shooter",
			"LostWithin/Variant_Shooter/AI",
			"LostWithin/Variant_Shooter/UI",
			"LostWithin/Variant_Shooter/Weapons"
		});
	}
}
