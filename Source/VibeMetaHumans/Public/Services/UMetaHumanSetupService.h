// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UMetaHumanSetupService.generated.h"

/**
 * MetaHuman Setup Service - environment checks and asset discovery for MetaHuman work.
 *
 * Check the environment first: PluginToolset.PluginToolset.ListEnabledPlugins for plugins, and
 * unreal.MetaHumanGeneratorSubsystemWrapper.is_optional_content_installed() for the "MetaHuman
 * Creator Core Data" optional content (presets, texture synthesis and the wardrobe library depend on it).
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.MetaHumanSetupService
 *   print(json.loads(svc.get_enum_options()))
 *   # Discover assets with EditorToolset.AssetToolset.FindAssets; inspect the tool schema.
 *   # Find preset assets using EditorToolset.AssetToolset.FindAssets.
 *   # Open loaded assets with unreal.AssetEditorSubsystem.open_editor_for_assets.
 */
UCLASS(BlueprintType)
class VIBEMETAHUMANS_API UMetaHumanSetupService : public UToolsetDefinition
{
	GENERATED_BODY()

public:

	/** Names of the enum values an agent can pass to the other services (body types, rig types, pipeline types, quality, input types, moods...). */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeMetaHumans|Setup")
	static FString GetEnumOptions();
};
