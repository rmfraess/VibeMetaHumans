// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UMetaHumanObjectService.generated.h"

/**
 * MetaHuman Object Service - generic reflection escape hatch.
 *
 * The MetaHuman plugins expose a lot through UPROPERTYs and Blueprint libraries, some of them in
 * private headers. These tools read/patch any object's properties by JSON and call any static
 * Blueprint function by name, so an agent is never blocked on a missing dedicated wrapper.
 *
 * Python Usage:
 *   svc = unreal.MetaHumanObjectService
 *   svc.get_properties("/Game/MHA/Perf_Take01.Perf_Take01", ["InputType", "HeadMovementMode"])
 *   svc.set_properties("/Game/MHA/Perf_Take01.Perf_Take01", '{"HeadMovementMode":"ControlRig","bSkipDiagnostics":true}')
 *   svc.call_function("CaptureManagerIngestBlueprintLibrary", "FindTakeDirectories", '{"SearchDirectory":"D:/Takes","bRecursive":true}')
 *   svc.list_functions("MetaHumanCharacterEditorSubsystem")
 */
UCLASS(BlueprintType)
class VIBEMETAHUMANS_API UMetaHumanObjectService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/** Read UPROPERTYs of a loaded asset/object as JSON. Empty PropertyNames returns every property. */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeMetaHumans|Object")
	static FString GetProperties(const FString& ObjectPath, const TArray<FString>& PropertyNames);

	/**
	 * Patch UPROPERTYs from a JSON object (enum values by name, objects by path string, structs as nested
	 * objects). Each property gets PostEditChangeProperty, which matters for MetaHuman Performance assets.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeMetaHumans|Object")
	static FString SetProperties(const FString& ObjectPath, const FString& JsonPatch);




	/** Describe a USTRUCT (field names, types, defaults) so an agent can build a JSON patch for it. */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeMetaHumans|Object")
	static FString DescribeStruct(const FString& StructPath);
};
