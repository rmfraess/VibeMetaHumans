// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UFactory;
class UWorld;

/** Asset and editor helpers shared by the VibeMetaHumans services. */
namespace VmhAssets
{
	/** Load an asset by package path ("/Game/X/Y") or object path ("/Game/X/Y.Y"). nullptr when missing. */
	VIBEMETAHUMANS_API UObject* Load(const FString& Path);

	template <typename T>
	T* Load(const FString& Path)
	{
		return Cast<T>(Load(Path));
	}

	/** Create an asset through AssetTools with the given factory class (by reflected class path or name). */
	VIBEMETAHUMANS_API UObject* Create(const FString& PackagePath, const FString& AssetName, UClass* AssetClass, const FString& FactoryClassPathOrName, FString& OutError);

	/** Mark dirty and save the package that owns the object. */
	VIBEMETAHUMANS_API bool Save(UObject* Asset, FString& OutError);

	/** Object path ("/Game/X/Y.Y") of an asset, or "" for null. */
	VIBEMETAHUMANS_API FString PathOf(const UObject* Object);

	/** The editor world (not PIE). */
	VIBEMETAHUMANS_API UWorld* EditorWorld();

	/** Find a level actor by label, then by object name. */
	VIBEMETAHUMANS_API AActor* FindActor(const FString& LabelOrName);

	/** Split "/Game/Folder/Name" into package path and asset name. Accepts object paths too. */
	VIBEMETAHUMANS_API void SplitAssetPath(const FString& Path, FString& OutPackagePath, FString& OutAssetName);

	/** Asset object paths under a content folder whose class is (or derives from) ClassName. */
	VIBEMETAHUMANS_API TArray<FString> FindAssetsOfClass(const FString& ClassPath, const FString& SearchRoot, bool bRecursiveClasses = true);

	/** Open the asset in its editor. */
	VIBEMETAHUMANS_API bool OpenEditor(UObject* Asset);
}
