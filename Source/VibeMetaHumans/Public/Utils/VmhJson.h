// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * JSON and reflection helpers shared by every VibeMetaHumans service.
 *
 * Every tool returns a JSON string shaped as {"success": true, ...} or
 * {"success": false, "error_code": "...", "error": "..."} so agents can branch on one field.
 */
namespace VmhJson
{
	/** {"success":true, ...Data} */
	VIBEMETAHUMANS_API FString Ok(const TSharedPtr<FJsonObject>& Data = nullptr);

	/** {"success":false,"error_code":Code,"error":Message, ...Extra} */
	VIBEMETAHUMANS_API FString Error(const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Extra = nullptr);

	/** Parse a JSON object string. Returns nullptr and fills OutError on failure. An empty string parses as {}. */
	VIBEMETAHUMANS_API TSharedPtr<FJsonObject> Parse(const FString& Json, FString& OutError);

	/** Serialize a JSON object (condensed by default). */
	VIBEMETAHUMANS_API FString Stringify(const TSharedPtr<FJsonObject>& Object, bool bPretty = false);

	/** Reflect a UStruct value to JSON (UPROPERTYs only). */
	VIBEMETAHUMANS_API TSharedPtr<FJsonObject> StructToJson(const UStruct* Type, const void* Value);

	/** Overwrite the fields present in Json on an existing UStruct value; untouched fields keep their values. */
	VIBEMETAHUMANS_API bool JsonToStruct(const TSharedPtr<FJsonObject>& Json, const UStruct* Type, void* Value, FString& OutError);

	/** Reflect an object's UPROPERTYs to JSON. An empty filter means every property. */
	VIBEMETAHUMANS_API TSharedPtr<FJsonObject> ObjectToJson(const UObject* Object, const TArray<FString>& PropertyFilter);

	/**
	 * Patch an object's UPROPERTYs from JSON (property names are matched case-insensitively, with or
	 * without the "b" bool prefix). Each changed property gets a PostEditChangeProperty so assets that
	 * react to edits (e.g. UMetaHumanPerformance) stay consistent. Returns false on the first failure.
	 */
	VIBEMETAHUMANS_API bool PatchObject(UObject* Object, const TSharedPtr<FJsonObject>& Patch, TArray<FString>& OutChanged, FString& OutError);

	/** Find a property on a class/struct by name, tolerant of case and the "b" prefix on bools. */
	VIBEMETAHUMANS_API FProperty* FindPropertyLoose(const UStruct* Type, const FString& Name);

	/**
	 * Call a static UFUNCTION by reflection. Input parameters are filled from Args (by name); out
	 * parameters and the return value are returned in OutResult. This reaches Blueprint libraries whose
	 * headers are private (e.g. UCaptureManagerIngestBlueprintLibrary).
	 */
	VIBEMETAHUMANS_API bool CallStaticFunction(UClass* Class, FName FunctionName, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	/** Same as CallStaticFunction but for an instance method on Target. Args keyed by parameter name, or by "0","1",... for positional. */
	VIBEMETAHUMANS_API bool CallObjectFunction(UObject* Target, FName FunctionName, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	/** Resolve a class from "/Script/Module.Class", "Module.Class" or a bare class name. */
	VIBEMETAHUMANS_API UClass* FindClass(const FString& PathOrName);

	/** Resolve a UScriptStruct from "/Script/Module.Struct" or a bare name (with or without the F prefix). */
	VIBEMETAHUMANS_API UScriptStruct* FindStruct(const FString& PathOrName);

	/** Parse an enum value by (short or qualified) name. Returns INDEX_NONE when unknown. */
	VIBEMETAHUMANS_API int64 ParseEnum(const UEnum* Enum, const FString& Name);

	/** Comma-joined list of an enum's value names (for error messages). */
	VIBEMETAHUMANS_API FString EnumNames(const UEnum* Enum);

	/** Build a JSON object from key/value pairs quickly. */
	VIBEMETAHUMANS_API TSharedPtr<FJsonObject> Obj();
	VIBEMETAHUMANS_API TArray<TSharedPtr<FJsonValue>> StringArray(const TArray<FString>& Values);
	VIBEMETAHUMANS_API TArray<TSharedPtr<FJsonValue>> NameArray(const TArray<FName>& Values);
}
