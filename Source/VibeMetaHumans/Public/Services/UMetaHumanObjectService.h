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
 *   # Read with EditorToolset.ObjectToolset.GetProperties; inspect its schema before calling.
 *   # Write with EditorToolset.ObjectToolset.SetProperties; it does not run PostEditChangeProperty.
 *   # Scan takes with CaptureManagerToolset.CaptureManagerIngestTools.ScanForTakes.
 *   svc.list_functions("MetaHumanCharacterEditorSubsystem")
 */
UCLASS(BlueprintType)
class VIBEMETAHUMANS_API UMetaHumanObjectService : public UToolsetDefinition
{
	GENERATED_BODY()

public:

	/**
	 * Call a static UFUNCTION on a class (e.g. a Blueprint function library) with JSON arguments keyed by
	 * parameter name. Returns out-parameters and "ReturnValue" as JSON. ClassPath accepts
	 * "/Script/Module.Class", "Module.Class" or a bare class name.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Object")
	static FString CallFunction(const FString& ClassPath, const FString& FunctionName, const FString& ArgsJson);

	/**
	 * Call an instance UFUNCTION on a loaded object (asset, subsystem settings object, actor...) with JSON
	 * arguments keyed by parameter name or by position ("0", "1", ...). Returns out-parameters and "ReturnValue".
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Object")
	static FString CallObjectFunction(const FString& ObjectPath, const FString& FunctionName, const FString& ArgsJson);

	/** List the callable UFUNCTIONs of a class with parameter names/types (static flag, out params, return type). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Object")
	static FString ListFunctions(const FString& ClassPath, const FString& NameFilter);

	/** Describe a USTRUCT (field names, types, defaults) so an agent can build a JSON patch for it. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Object")
	static FString DescribeStruct(const FString& StructPath);

};
