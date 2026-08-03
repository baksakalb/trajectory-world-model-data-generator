// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class he_grenade_game : ModuleRules
{
	public he_grenade_game(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"ImageWrapper",
			"Json",
			"RenderCore",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibWebPRoot = Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "libwebp");
			PublicSystemIncludePaths.Add(Path.Combine(LibWebPRoot, "include"));
			PublicAdditionalLibraries.Add(Path.Combine(LibWebPRoot, "lib", "Win64", "libwebp.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibWebPRoot, "lib", "Win64", "libsharpyuv.lib"));
			PublicDefinitions.Add("CURRICULUM_WITH_LIBWEBP=1");
		}
		else
		{
			PublicDefinitions.Add("CURRICULUM_WITH_LIBWEBP=0");
		}

		PublicIncludePaths.AddRange(new string[] {
			"he_grenade_game",
			"he_grenade_game/Grenade",
			"he_grenade_game/Grenade/Breakables",
			"he_grenade_game/Variant_Shooter",
			"he_grenade_game/Variant_Shooter/AI",
			"he_grenade_game/Variant_Shooter/UI",
			"he_grenade_game/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
