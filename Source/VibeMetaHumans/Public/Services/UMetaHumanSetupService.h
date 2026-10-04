// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UMetaHumanSetupService.generated.h"

/**
 * MetaHuman Setup Service - environment checks and asset discovery for MetaHuman work.
 *
 * Call get_status() first in any MetaHuman session: it reports which MetaHuman plugins are enabled,
 * whether the "MetaHuman Creator Core Data" optional content is installed (presets, texture
 * synthesis and the wardrobe library all depend on it), and what MetaHuman assets the project
 * already has.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.MetaHumanSetupService
 *   print(json.loads(svc.get_status()))
 *   print(svc.list_assets("character", "/Game"))
 *   print(svc.list_assets("preset", ""))
 *   svc.open_in_editor("/Game/MetaHumans/MH_Hero")
 */
UCLASS(BlueprintType)
class VIBEMETAHUMANS_API UMetaHumanSetupService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Environment summary as JSON: plugins (name -> enabled), optional_content_installed, editor subsystem
	 * availability, preset/wardrobe folders, counts of MetaHuman assets in the project, Live Link source count.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Setup")
	static FString GetStatus();

	/**
	 * List MetaHuman-related assets as JSON {"kind","count","assets":[object paths]}.
	 * Kind: character | identity | performance | capture_data | wardrobe | preset | blueprint | collection | soundwave.
	 * SearchRoot defaults per kind ("/Game" for project assets, the plugin Optional folder for presets/wardrobe).
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Setup")
	static FString ListAssets(const FString& Kind, const FString& SearchRoot);

	/** Open an asset in its editor (MetaHuman Character editor, Performance editor, ...). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Setup")
	static FString OpenInEditor(const FString& AssetPath);

	/** Names of the enum values an agent can pass to the other services (body types, rig types, pipeline types, quality, input types, moods...). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Setup")
	static FString GetEnumOptions();
};
