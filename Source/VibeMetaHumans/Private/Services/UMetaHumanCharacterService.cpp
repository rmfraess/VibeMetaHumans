// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Services/UMetaHumanCharacterService.h"

#include "Animation/Skeleton.h"
#include "AssetExportTask.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DNA.h"
#include "DNAAssetUserData.h"
#include "DNACommon.h"
#include "DNAUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Exporters/Exporter.h"
#include "Exporters/FbxExportOption.h"
#include "Exporters/SkeletalMeshExporterFBX.h"
#include "Exporters/StaticMeshExporterFBX.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "TextureCompiler.h"
#include "InterchangeDnaModule.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MaterialDomain.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MetaHumanCharacterExportBlueprintLibrary.h"
#include "MetaHumanCharacterTargetKeyPoints.h"
#include "MetaHumanConformTargetParams.h"
#include "PreviewScene.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "StaticMeshResources.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "MetaHumanBodyType.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterAssemblySettings.h"
#include "MetaHumanCharacterBodyIdentity.h"
#include "MetaHumanCharacterEditorModule.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterIdentity.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionBlueprintLibrary.h"
#include "MetaHumanIdentity.h"
#include "MetaHumanInstance.h"
#include "MetaHumanPaletteItemKey.h"
#include "MetaHumanPipelineSlotSelection.h"
#include "MetaHumanPipelineSlotSelectionData.h"
#include "MetaHumanTypes.h"
#include "MetaHumanWardrobeItem.h"
#include "Misc/Paths.h"
#include "Subsystem/MetaHumanCharacterBuild.h"
#include "UObject/UnrealType.h"
#include "Utils/VmhAssets.h"
#include "Utils/VmhJson.h"
#include "VibeMetaHumansModule.h"

namespace
{
	UMetaHumanCharacterEditorSubsystem* GetSubsystem(FString& OutError)
	{
		UMetaHumanCharacterEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>() : nullptr;
		if (!Subsystem)
		{
			OutError = TEXT("MetaHumanCharacterEditorSubsystem is not available (is the MetaHuman Creator plugin enabled?)");
		}
		return Subsystem;
	}

	UMetaHumanCharacter* LoadCharacter(const FString& Path, FString& OutError)
	{
		UObject* Object = VmhAssets::Load(Path);
		UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(Object);
		if (!Character)
		{
			OutError = Object
				? FString::Printf(TEXT("%s is a %s, not a MetaHumanCharacter"), *Path, *Object->GetClass()->GetName())
				: FString::Printf(TEXT("No MetaHuman Character asset at %s"), *Path);
		}
		return Character;
	}

	/** Resolve subsystem + character and make sure the character is open for editing. */
	struct FEditContext
	{
		UMetaHumanCharacterEditorSubsystem* Subsystem = nullptr;
		UMetaHumanCharacter* Character = nullptr;
		FString Error;
		FString ErrorCode;

		bool IsValid() const { return Subsystem && Character && Error.IsEmpty(); }
		FString ToError() const { return VmhJson::Error(ErrorCode.IsEmpty() ? TEXT("EDIT_FAILED") : ErrorCode, Error); }
	};

	FEditContext OpenForEdit(const FString& CharacterPath, bool bRequireEdit = true)
	{
		FEditContext Ctx;
		Ctx.Subsystem = GetSubsystem(Ctx.Error);
		if (!Ctx.Subsystem)
		{
			Ctx.ErrorCode = TEXT("NO_SUBSYSTEM");
			return Ctx;
		}
		Ctx.Character = LoadCharacter(CharacterPath, Ctx.Error);
		if (!Ctx.Character)
		{
			Ctx.ErrorCode = TEXT("CHARACTER_NOT_FOUND");
			return Ctx;
		}
		if (bRequireEdit && !Ctx.Subsystem->IsObjectAddedForEditing(Ctx.Character))
		{
			if (!Ctx.Subsystem->TryAddObjectToEdit(Ctx.Character))
			{
				Ctx.Error = FString::Printf(TEXT("TryAddObjectToEdit failed for %s (see Output Log; a missing optional-content model or an invalid character state is the usual cause)"), *Ctx.Character->GetPathName());
				Ctx.ErrorCode = TEXT("BEGIN_EDIT_FAILED");
			}
		}
		return Ctx;
	}

	/** The subsystem declares IsFixedBodyType but never defines it (unresolved at link time), so read the asset flag. */
	bool IsFixedBodyType(const UMetaHumanCharacter* Character)
	{
		if (!Character)
		{
			return false;
		}
		if (const FBoolProperty* Prop = CastField<FBoolProperty>(Character->GetClass()->FindPropertyByName(TEXT("bFixedBodyType"))))
		{
			return Prop->GetPropertyValue_InContainer(Character);
		}
		return false;
	}

	const TCHAR* RigStateName(EMetaHumanCharacterRigState State)
	{
		switch (State)
		{
		case EMetaHumanCharacterRigState::Unrigged: return TEXT("Unrigged");
		case EMetaHumanCharacterRigState::RigPending: return TEXT("RigPending");
		case EMetaHumanCharacterRigState::Rigged: return TEXT("Rigged");
		default: return TEXT("Unknown");
		}
	}

	FString ImportErrorName(EImportErrorCode Code)
	{
		if (const UEnum* Enum = StaticEnum<EImportErrorCode>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Code));
		}
		return FString::FromInt(static_cast<int32>(Code));
	}

	/** Copy of a struct UPROPERTY on an object, by name (so we never depend on member access). */
	template <typename TStruct>
	bool ReadStruct(UObject* Object, const TCHAR* PropertyName, TStruct& Out, FString& OutError)
	{
		FStructProperty* Prop = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!Prop || Prop->Struct != TStruct::StaticStruct())
		{
			OutError = FString::Printf(TEXT("%s has no %s struct property"), *Object->GetClass()->GetName(), PropertyName);
			return false;
		}
		Out = *Prop->ContainerPtrToValuePtr<TStruct>(Object);
		return true;
	}

	bool WriteStruct(UObject* Object, const TCHAR* PropertyName, const UScriptStruct* Type, const void* Value, FString& OutError)
	{
		FStructProperty* Prop = CastField<FStructProperty>(Object->GetClass()->FindPropertyByName(PropertyName));
		if (!Prop || Prop->Struct != Type)
		{
			OutError = FString::Printf(TEXT("%s has no %s struct property"), *Object->GetClass()->GetName(), PropertyName);
			return false;
		}
		Object->Modify();
		Prop->CopyCompleteValue(Prop->ContainerPtrToValuePtr<void>(Object), Value);
		return true;
	}

	TSharedPtr<FJsonObject> ConstraintsToJson(const TArray<FMetaHumanCharacterBodyConstraint>& Constraints)
	{
		TSharedPtr<FJsonObject> Out = VmhJson::Obj();
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FMetaHumanCharacterBodyConstraint& C : Constraints)
		{
			TSharedPtr<FJsonObject> Item = VmhJson::Obj();
			Item->SetStringField(TEXT("name"), C.Name.ToString());
			Item->SetBoolField(TEXT("active"), C.bIsActive);
			Item->SetNumberField(TEXT("target"), C.TargetMeasurement);
			Item->SetNumberField(TEXT("min"), C.MinMeasurement);
			Item->SetNumberField(TEXT("max"), C.MaxMeasurement);
			const float Range = C.MaxMeasurement - C.MinMeasurement;
			Item->SetNumberField(TEXT("normalized"), Range > KINDA_SMALL_NUMBER ? (C.TargetMeasurement - C.MinMeasurement) / Range : 0.f);
			Items.Add(MakeShared<FJsonValueObject>(Item));
		}
		Out->SetArrayField(TEXT("constraints"), Items);
		return Out;
	}

	/** Re-align the neck after a body change (what Epic's MetaHumanGenerator does via ResetNeckToBody). */
	void ResetNeckToBody(UMetaHumanCharacterEditorSubsystem* Subsystem, UMetaHumanCharacter* Character)
	{
		TSharedRef<FMetaHumanCharacterIdentity::FState> NewState = Subsystem->CopyFaceState(Character);
		NewState->ResetNeckRegion();
		Subsystem->ApplyFaceState(Character, NewState);
	}

	TSharedPtr<FJsonObject> ItemKeyToJson(UMetaHumanCharacterPalette* Palette, const FMetaHumanPaletteItemKey& Key)
	{
		TSharedPtr<FJsonObject> Out = VmhJson::StructToJson(FMetaHumanPaletteItemKey::StaticStruct(), &Key);
		Out->SetStringField(TEXT("asset_name"), Key.ToAssetNameString());
		Out->SetStringField(TEXT("slot"), UMetaHumanCollectionBlueprintLibrary::GetItemSlotName(Palette, Key).ToString());
		Out->SetStringField(TEXT("display_name"), UMetaHumanCollectionBlueprintLibrary::GetItemDisplayName(Palette, Key).ToString());
		return Out;
	}

	FString CharacterResult(const FEditContext& Ctx, TSharedPtr<FJsonObject> Extra = nullptr)
	{
		TSharedPtr<FJsonObject> Out = Extra.IsValid() ? Extra : VmhJson::Obj();
		Out->SetStringField(TEXT("character"), Ctx.Character->GetPathName());
		Out->SetStringField(TEXT("rig_state"), RigStateName(Ctx.Subsystem->GetRiggingState(Ctx.Character)));
		return VmhJson::Ok(Out);
	}
}

// =====================================================================
// Lifecycle
// =====================================================================

FString UMetaHumanCharacterService::CreateCharacter(const FString& PackagePath, const FString& AssetName, const FString& PresetCharacterPath)
{
	FString Error;
	UMetaHumanCharacterEditorSubsystem* Subsystem = GetSubsystem(Error);
	if (!Subsystem)
	{
		return VmhJson::Error(TEXT("NO_SUBSYSTEM"), Error);
	}

	UMetaHumanCharacter* Preset = nullptr;
	if (!PresetCharacterPath.IsEmpty())
	{
		Preset = LoadCharacter(PresetCharacterPath, Error);
		if (!Preset)
		{
			return VmhJson::Error(TEXT("PRESET_NOT_FOUND"), Error);
		}
	}

	UObject* Created = VmhAssets::Create(PackagePath, AssetName, UMetaHumanCharacter::StaticClass(),
		TEXT("/Script/MetaHumanCharacterEditor.MetaHumanCharacterFactoryNew"), Error);
	UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(Created);
	if (!Character)
	{
		return VmhJson::Error(TEXT("CREATE_FAILED"), Error.IsEmpty() ? TEXT("Factory returned no MetaHumanCharacter") : Error);
	}

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	if (Preset)
	{
		if (!Subsystem->IsObjectAddedForEditing(Character) && !Subsystem->TryAddObjectToEdit(Character))
		{
			return VmhJson::Error(TEXT("BEGIN_EDIT_FAILED"), TEXT("Created the asset but could not open it for editing to apply the preset"));
		}
		Subsystem->InitializeFromPreset(Character, Preset);
		Out->SetStringField(TEXT("preset"), Preset->GetPathName());
	}

	if (!VmhAssets::Save(Character, Error))
	{
		UE_LOG(LogVibeMetaHumans, Warning, TEXT("%s"), *Error);
	}

	Out->SetStringField(TEXT("character"), Character->GetPathName());
	Out->SetBoolField(TEXT("optional_content_installed"), FMetaHumanCharacterEditorModule::IsOptionalMetaHumanContentInstalled());
	Out->SetStringField(TEXT("next"), TEXT("set_body_shape / set_skin_tone / set_settings, then request_auto_rig and build"));
	return VmhJson::Ok(Out);
}

FString UMetaHumanCharacterService::BeginEdit(const FString& CharacterPath)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetBoolField(TEXT("editing"), true);
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::EndEdit(const FString& CharacterPath)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, false);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	const bool bWasEditing = Ctx.Subsystem->IsObjectAddedForEditing(Ctx.Character);
	if (bWasEditing)
	{
		Ctx.Subsystem->RemoveObjectToEdit(Ctx.Character);
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetBoolField(TEXT("was_editing"), bWasEditing);
	Out->SetStringField(TEXT("character"), Ctx.Character->GetPathName());
	return VmhJson::Ok(Out);
}

FString UMetaHumanCharacterService::GetSummary(const FString& CharacterPath)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, false);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	UMetaHumanCharacter* Character = Ctx.Character;
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("character"), Character->GetPathName());
	Out->SetStringField(TEXT("name"), Character->GetName());
	Out->SetBoolField(TEXT("is_editing"), Ctx.Subsystem->IsObjectAddedForEditing(Character));
	Out->SetStringField(TEXT("rig_state"), RigStateName(Ctx.Subsystem->GetRiggingState(Character)));
	Out->SetBoolField(TEXT("has_face_dna"), Character->HasFaceDNA());
	Out->SetBoolField(TEXT("has_body_dna"), Character->HasBodyDNA());
	Out->SetBoolField(TEXT("has_synthesized_textures"), Character->HasSynthesizedTextures());
	Out->SetBoolField(TEXT("has_high_resolution_textures"), Character->HasHighResolutionTextures());
	Out->SetBoolField(TEXT("fixed_body_type"), IsFixedBodyType(Character));
	Out->SetBoolField(TEXT("auto_rigging_in_progress"), Ctx.Subsystem->IsAutoRiggingFace(Character));
	Out->SetBoolField(TEXT("requesting_high_res_textures"), Ctx.Subsystem->IsRequestingHighResolutionTextures(Character));

	FString Err;
	FMetaHumanCharacterSkinSettings Skin;
	if (ReadStruct(Character, TEXT("SkinSettings"), Skin, Err))
	{
		TSharedPtr<FJsonObject> SkinJson = VmhJson::Obj();
		SkinJson->SetNumberField(TEXT("u"), Skin.Skin.U);
		SkinJson->SetNumberField(TEXT("v"), Skin.Skin.V);
		SkinJson->SetNumberField(TEXT("roughness"), Skin.Skin.Roughness);
		SkinJson->SetNumberField(TEXT("face_texture_index"), Skin.Skin.FaceTextureIndex);
		SkinJson->SetNumberField(TEXT("body_texture_index"), Skin.Skin.BodyTextureIndex);
		Out->SetObjectField(TEXT("skin"), SkinJson);
	}
	FMetaHumanCharacterEyesSettings Eyes;
	if (ReadStruct(Character, TEXT("EyesSettings"), Eyes, Err))
	{
		TSharedPtr<FJsonObject> EyesJson = VmhJson::Obj();
		EyesJson->SetNumberField(TEXT("left_u"), Eyes.EyeLeft.Iris.PrimaryColorU);
		EyesJson->SetNumberField(TEXT("left_v"), Eyes.EyeLeft.Iris.PrimaryColorV);
		EyesJson->SetNumberField(TEXT("right_u"), Eyes.EyeRight.Iris.PrimaryColorU);
		EyesJson->SetNumberField(TEXT("right_v"), Eyes.EyeRight.Iris.PrimaryColorV);
		Out->SetObjectField(TEXT("eyes"), EyesJson);
	}
	FMetaHumanCharacterFaceEvaluationSettings FaceEval;
	if (ReadStruct(Character, TEXT("FaceEvaluationSettings"), FaceEval, Err))
	{
		Out->SetObjectField(TEXT("face_evaluation"), VmhJson::StructToJson(FMetaHumanCharacterFaceEvaluationSettings::StaticStruct(), &FaceEval));
	}

	if (Ctx.Subsystem->IsObjectAddedForEditing(Character))
	{
		TArray<TSharedPtr<FJsonValue>> Active;
		for (const FMetaHumanCharacterBodyConstraint& C : Ctx.Subsystem->GetBodyConstraints(Character))
		{
			if (!C.bIsActive)
			{
				continue;
			}
			TSharedPtr<FJsonObject> Item = VmhJson::Obj();
			Item->SetStringField(TEXT("name"), C.Name.ToString());
			Item->SetNumberField(TEXT("target"), C.TargetMeasurement);
			Active.Add(MakeShared<FJsonValueObject>(Item));
		}
		Out->SetArrayField(TEXT("active_body_constraints"), Active);
	}

	if (UMetaHumanCollection* Collection = Character->GetMutableInternalCollection())
	{
		TArray<TSharedPtr<FJsonValue>> Selections;
		for (const FMetaHumanPipelineSlotSelectionData& Data : Collection->GetMutableDefaultInstance()->GetSlotSelectionData())
		{
			TSharedPtr<FJsonObject> Item = VmhJson::Obj();
			Item->SetStringField(TEXT("slot"), Data.Selection.SlotName.ToString());
			Item->SetStringField(TEXT("item"), Data.Selection.SelectedItem.ToAssetNameString());
			Selections.Add(MakeShared<FJsonValueObject>(Item));
		}
		Out->SetArrayField(TEXT("wardrobe_selections"), Selections);
	}
	return VmhJson::Ok(Out);
}

FString UMetaHumanCharacterService::Save(const FString& CharacterPath)
{
	FString Error;
	UMetaHumanCharacter* Character = LoadCharacter(CharacterPath, Error);
	if (!Character)
	{
		return VmhJson::Error(TEXT("CHARACTER_NOT_FOUND"), Error);
	}
	if (!VmhAssets::Save(Character, Error))
	{
		return VmhJson::Error(TEXT("SAVE_FAILED"), Error);
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("character"), Character->GetPathName());
	return VmhJson::Ok(Out);
}

// =====================================================================
// Look
// =====================================================================

namespace
{
	enum class ESection { Skin, Eyes, Makeup, Head, FaceEvaluation, Viewport, Unknown };

	ESection ParseSection(const FString& Section)
	{
		const FString S = Section.TrimStartAndEnd().ToLower();
		if (S == TEXT("skin")) return ESection::Skin;
		if (S == TEXT("eyes")) return ESection::Eyes;
		if (S == TEXT("makeup")) return ESection::Makeup;
		if (S == TEXT("head") || S == TEXT("head_model") || S == TEXT("teeth") || S == TEXT("eyelashes")) return ESection::Head;
		if (S == TEXT("face_evaluation") || S == TEXT("face")) return ESection::FaceEvaluation;
		if (S == TEXT("viewport")) return ESection::Viewport;
		return ESection::Unknown;
	}

	const TCHAR* SectionProperty(ESection Section)
	{
		switch (Section)
		{
		case ESection::Skin: return TEXT("SkinSettings");
		case ESection::Eyes: return TEXT("EyesSettings");
		case ESection::Makeup: return TEXT("MakeupSettings");
		case ESection::Head: return TEXT("HeadModelSettings");
		case ESection::FaceEvaluation: return TEXT("FaceEvaluationSettings");
		case ESection::Viewport: return TEXT("ViewportSettings");
		default: return TEXT("");
		}
	}

	const FString SectionError = TEXT("Section must be one of: skin, eyes, makeup, head, face_evaluation, viewport");
}

FString UMetaHumanCharacterService::GetSettings(const FString& CharacterPath, const FString& Section)
{
	const ESection S = ParseSection(Section);
	if (S == ESection::Unknown)
	{
		return VmhJson::Error(TEXT("UNKNOWN_SECTION"), SectionError);
	}
	FEditContext Ctx = OpenForEdit(CharacterPath, false);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	FStructProperty* Prop = CastField<FStructProperty>(Ctx.Character->GetClass()->FindPropertyByName(SectionProperty(S)));
	if (!Prop)
	{
		return VmhJson::Error(TEXT("NO_PROPERTY"), FString::Printf(TEXT("Character has no %s"), SectionProperty(S)));
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("character"), Ctx.Character->GetPathName());
	Out->SetStringField(TEXT("section"), Section);
	Out->SetStringField(TEXT("struct"), Prop->Struct->GetName());
	Out->SetObjectField(TEXT("settings"), VmhJson::StructToJson(Prop->Struct, Prop->ContainerPtrToValuePtr<void>(Ctx.Character)));
	return VmhJson::Ok(Out);
}

FString UMetaHumanCharacterService::SetSettings(const FString& CharacterPath, const FString& Section, const FString& JsonPatch)
{
	const ESection S = ParseSection(Section);
	if (S == ESection::Unknown)
	{
		return VmhJson::Error(TEXT("UNKNOWN_SECTION"), SectionError);
	}
	FString ParseError;
	TSharedPtr<FJsonObject> Patch = VmhJson::Parse(JsonPatch, ParseError);
	if (!Patch.IsValid())
	{
		return VmhJson::Error(TEXT("INVALID_JSON"), ParseError);
	}
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	UMetaHumanCharacter* Character = Ctx.Character;
	FString Err;

	switch (S)
	{
	case ESection::Skin:
	{
		FMetaHumanCharacterSkinSettings Value;
		if (!ReadStruct(Character, TEXT("SkinSettings"), Value, Err) || !VmhJson::JsonToStruct(Patch, FMetaHumanCharacterSkinSettings::StaticStruct(), &Value, Err))
		{
			return VmhJson::Error(TEXT("PATCH_FAILED"), Err);
		}
		Ctx.Subsystem->CommitSkinSettings(Character, Value);
		break;
	}
	case ESection::Eyes:
	{
		FMetaHumanCharacterEyesSettings Value;
		if (!ReadStruct(Character, TEXT("EyesSettings"), Value, Err) || !VmhJson::JsonToStruct(Patch, FMetaHumanCharacterEyesSettings::StaticStruct(), &Value, Err))
		{
			return VmhJson::Error(TEXT("PATCH_FAILED"), Err);
		}
		Ctx.Subsystem->CommitEyesSettings(Character, Value);
		break;
	}
	case ESection::Makeup:
	{
		FMetaHumanCharacterMakeupSettings Value;
		if (!ReadStruct(Character, TEXT("MakeupSettings"), Value, Err) || !VmhJson::JsonToStruct(Patch, FMetaHumanCharacterMakeupSettings::StaticStruct(), &Value, Err))
		{
			return VmhJson::Error(TEXT("PATCH_FAILED"), Err);
		}
		Ctx.Subsystem->CommitMakeupSettings(Character, Value);
		break;
	}
	case ESection::Head:
	{
		FMetaHumanCharacterHeadModelSettings Value;
		if (!ReadStruct(Character, TEXT("HeadModelSettings"), Value, Err) || !VmhJson::JsonToStruct(Patch, FMetaHumanCharacterHeadModelSettings::StaticStruct(), &Value, Err))
		{
			return VmhJson::Error(TEXT("PATCH_FAILED"), Err);
		}
		Ctx.Subsystem->CommitHeadModelSettings(Character, Value);
		break;
	}
	case ESection::FaceEvaluation:
	{
		FMetaHumanCharacterFaceEvaluationSettings Value;
		if (!ReadStruct(Character, TEXT("FaceEvaluationSettings"), Value, Err) || !VmhJson::JsonToStruct(Patch, FMetaHumanCharacterFaceEvaluationSettings::StaticStruct(), &Value, Err))
		{
			return VmhJson::Error(TEXT("PATCH_FAILED"), Err);
		}
		Ctx.Subsystem->CommitFaceEvaluationSettings(Character, Value);
		break;
	}
	case ESection::Viewport:
	{
		FStructProperty* Prop = CastField<FStructProperty>(Character->GetClass()->FindPropertyByName(TEXT("ViewportSettings")));
		if (!Prop)
		{
			return VmhJson::Error(TEXT("NO_PROPERTY"), TEXT("Character has no ViewportSettings"));
		}
		Character->Modify();
		if (!VmhJson::JsonToStruct(Patch, Prop->Struct, Prop->ContainerPtrToValuePtr<void>(Character), Err))
		{
			return VmhJson::Error(TEXT("PATCH_FAILED"), Err);
		}
		FPropertyChangedEvent Event(Prop, EPropertyChangeType::ValueSet);
		Character->PostEditChangeProperty(Event);
		break;
	}
	default:
		break;
	}

	return GetSettings(CharacterPath, Section);
}

FString UMetaHumanCharacterService::SetSkinTone(const FString& CharacterPath, float U, float V)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	FString Err;
	FMetaHumanCharacterSkinSettings Skin;
	if (!ReadStruct(Ctx.Character, TEXT("SkinSettings"), Skin, Err))
	{
		return VmhJson::Error(TEXT("NO_PROPERTY"), Err);
	}
	Skin.Skin.U = FMath::Clamp(U, 0.f, 1.f);
	Skin.Skin.V = FMath::Clamp(V, 0.f, 1.f);
	Ctx.Subsystem->CommitSkinSettings(Ctx.Character, Skin);

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetNumberField(TEXT("u"), Skin.Skin.U);
	Out->SetNumberField(TEXT("v"), Skin.Skin.V);
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::SetEyeColor(const FString& CharacterPath, float U, float V, const FString& Eye)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	FString Err;
	FMetaHumanCharacterEyesSettings Eyes;
	if (!ReadStruct(Ctx.Character, TEXT("EyesSettings"), Eyes, Err))
	{
		return VmhJson::Error(TEXT("NO_PROPERTY"), Err);
	}
	const FString Which = Eye.TrimStartAndEnd().ToLower();
	const bool bLeft = Which.IsEmpty() || Which == TEXT("both") || Which == TEXT("left");
	const bool bRight = Which.IsEmpty() || Which == TEXT("both") || Which == TEXT("right");
	if (!bLeft && !bRight)
	{
		return VmhJson::Error(TEXT("BAD_EYE"), TEXT("Eye must be both, left or right"));
	}
	const float CU = FMath::Clamp(U, 0.f, 1.f);
	const float CV = FMath::Clamp(V, 0.f, 1.f);
	auto Apply = [CU, CV](FMetaHumanCharacterEyeProperties& P)
	{
		P.Iris.PrimaryColorU = CU;
		P.Iris.PrimaryColorV = CV;
		P.Iris.SecondaryColorU = CU;
		P.Iris.SecondaryColorV = CV;
	};
	if (bLeft) Apply(Eyes.EyeLeft);
	if (bRight) Apply(Eyes.EyeRight);
	Ctx.Subsystem->CommitEyesSettings(Ctx.Character, Eyes);

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetNumberField(TEXT("u"), CU);
	Out->SetNumberField(TEXT("v"), CV);
	Out->SetStringField(TEXT("eye"), Which.IsEmpty() ? TEXT("both") : Which);
	return CharacterResult(Ctx, Out);
}

// =====================================================================
// Body
// =====================================================================

FString UMetaHumanCharacterService::GetBodyConstraints(const FString& CharacterPath)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	TSharedPtr<FJsonObject> Out = ConstraintsToJson(Ctx.Subsystem->GetBodyConstraints(Ctx.Character, false));
	Out->SetBoolField(TEXT("fixed_body_type"), IsFixedBodyType(Ctx.Character));
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::SetBodyConstraints(const FString& CharacterPath, const FString& ValuesJson, bool bNormalized)
{
	FString ParseError;
	TSharedPtr<FJsonObject> Values = VmhJson::Parse(ValuesJson, ParseError);
	if (!Values.IsValid())
	{
		return VmhJson::Error(TEXT("INVALID_JSON"), ParseError);
	}
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	if (IsFixedBodyType(Ctx.Character))
	{
		return VmhJson::Error(TEXT("FIXED_BODY"), TEXT("This character uses a fixed body type; call set_body_type(..., \"BlendableBody\") first to use parametric constraints"));
	}

	TArray<FMetaHumanCharacterBodyConstraint> Constraints = Ctx.Subsystem->GetBodyConstraints(Ctx.Character, false);
	TArray<FString> Applied;
	TArray<FString> Unknown;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Values->Values)
	{
		FMetaHumanCharacterBodyConstraint* Found = Constraints.FindByPredicate([&Pair](const FMetaHumanCharacterBodyConstraint& C)
		{
			return C.Name.ToString().Equals(Pair.Key, ESearchCase::IgnoreCase);
		});
		if (!Found)
		{
			Unknown.Add(Pair.Key);
			continue;
		}
		double Value = 0.0;
		if (!Pair.Value->TryGetNumber(Value))
		{
			Unknown.Add(Pair.Key + TEXT(" (not a number)"));
			continue;
		}
		Found->bIsActive = true;
		const bool bIsHeight = Found->Name.ToString().Equals(TEXT("Height"), ESearchCase::IgnoreCase);
		if (bIsHeight || !bNormalized)
		{
			Found->TargetMeasurement = FMath::Clamp(static_cast<float>(Value), Found->MinMeasurement, Found->MaxMeasurement);
		}
		else
		{
			Found->TargetMeasurement = FMath::Lerp(Found->MinMeasurement, Found->MaxMeasurement, FMath::Clamp(static_cast<float>(Value), 0.f, 1.f));
		}
		Applied.Add(Found->Name.ToString());
	}
	if (Applied.Num() == 0)
	{
		TArray<FString> Names;
		for (const FMetaHumanCharacterBodyConstraint& C : Constraints) Names.Add(C.Name.ToString());
		return VmhJson::Error(TEXT("NO_MATCHING_CONSTRAINT"), FString::Printf(TEXT("None of the keys matched a body constraint. Available: %s"), *FString::Join(Names, TEXT(", "))));
	}

	Ctx.Subsystem->SetBodyConstraints(Ctx.Character, Constraints);
	Ctx.Subsystem->CommitBodyState(Ctx.Character);
	ResetNeckToBody(Ctx.Subsystem, Ctx.Character);

	TSharedPtr<FJsonObject> Out = ConstraintsToJson(Ctx.Subsystem->GetBodyConstraints(Ctx.Character, false));
	Out->SetArrayField(TEXT("applied"), VmhJson::StringArray(Applied));
	if (Unknown.Num() > 0)
	{
		Out->SetArrayField(TEXT("ignored"), VmhJson::StringArray(Unknown));
	}
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::SetBodyShape(const FString& CharacterPath, float MasculineFeminine, float Fat, float Muscularity, float HeightCm)
{
	TSharedPtr<FJsonObject> Values = VmhJson::Obj();
	if (MasculineFeminine >= 0.f) Values->SetNumberField(TEXT("Masculine/Feminine"), MasculineFeminine);
	if (Fat >= 0.f) Values->SetNumberField(TEXT("Fat"), Fat);
	if (Muscularity >= 0.f) Values->SetNumberField(TEXT("Muscularity"), Muscularity);
	if (HeightCm >= 0.f) Values->SetNumberField(TEXT("Height"), HeightCm);
	if (Values->Values.Num() == 0)
	{
		return VmhJson::Error(TEXT("NO_VALUES"), TEXT("Pass at least one non-negative value"));
	}
	return SetBodyConstraints(CharacterPath, VmhJson::Stringify(Values), true);
}

FString UMetaHumanCharacterService::SetBodyType(const FString& CharacterPath, const FString& BodyType)
{
	const UEnum* Enum = StaticEnum<EMetaHumanBodyType>();
	const int64 Value = VmhJson::ParseEnum(Enum, BodyType);
	if (Value == INDEX_NONE || Value >= static_cast<int64>(EMetaHumanBodyType::Count))
	{
		return VmhJson::Error(TEXT("BAD_BODY_TYPE"), FString::Printf(TEXT("Unknown body type '%s'. Options: %s"), *BodyType, *VmhJson::EnumNames(Enum)));
	}
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	Ctx.Subsystem->SetMetaHumanBodyType(Ctx.Character, static_cast<EMetaHumanBodyType>(Value), UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Full);
	Ctx.Subsystem->CommitBodyState(Ctx.Character);

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("body_type"), Enum->GetNameStringByValue(Value));
	Out->SetBoolField(TEXT("fixed_body_type"), IsFixedBodyType(Ctx.Character));
	return CharacterResult(Ctx, Out);
}

// =====================================================================
// Face
// =====================================================================

FString UMetaHumanCharacterService::GetFaceCoefficients(const FString& CharacterPath)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	TArray<float> Coefficients;
	Ctx.Subsystem->GetFaceModelCoefficients(Ctx.Character, Coefficients);
	TArray<TSharedPtr<FJsonValue>> Values;
	for (float C : Coefficients)
	{
		Values.Add(MakeShared<FJsonValueNumber>(C));
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetNumberField(TEXT("count"), Coefficients.Num());
	Out->SetArrayField(TEXT("coefficients"), Values);
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::SetFaceCoefficients(const FString& CharacterPath, const TArray<float>& Coefficients)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	TArray<float> Current;
	Ctx.Subsystem->GetFaceModelCoefficients(Ctx.Character, Current);
	if (Current.Num() != Coefficients.Num())
	{
		return VmhJson::Error(TEXT("BAD_LENGTH"), FString::Printf(TEXT("Expected %d coefficients, got %d"), Current.Num(), Coefficients.Num()));
	}
	Ctx.Subsystem->SetFaceModelCoefficients(Ctx.Character, Coefficients);
	Ctx.Subsystem->CommitFaceState(Ctx.Character);
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetNumberField(TEXT("count"), Coefficients.Num());
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::ResetFace(const FString& CharacterPath)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	Ctx.Subsystem->ResetCharacterFace(Ctx.Character);
	Ctx.Subsystem->CommitFaceState(Ctx.Character);
	return CharacterResult(Ctx);
}

// =====================================================================
// Import
// =====================================================================

FString UMetaHumanCharacterService::ImportFromIdentity(const FString& CharacterPath, const FString& IdentityPath, bool bUseEyeMeshes, bool bUseTeethMesh, bool bUseMetricScale)
{
	UMetaHumanIdentity* Identity = VmhAssets::Load<UMetaHumanIdentity>(IdentityPath);
	if (!Identity)
	{
		return VmhJson::Error(TEXT("IDENTITY_NOT_FOUND"), FString::Printf(TEXT("No MetaHuman Identity at %s"), *IdentityPath));
	}
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	FImportFromIdentityParams Params;
	Params.bUseEyeMeshes = bUseEyeMeshes;
	Params.bUseTeethMesh = bUseTeethMesh;
	Params.bUseMetricScale = bUseMetricScale;
	const EImportErrorCode Code = Ctx.Subsystem->ImportFromIdentity(Ctx.Character, Identity, Params);
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("identity"), Identity->GetPathName());
	Out->SetStringField(TEXT("import_result"), ImportErrorName(Code));
	if (Code != EImportErrorCode::Success)
	{
		Out->SetStringField(TEXT("character"), Ctx.Character->GetPathName());
		return VmhJson::Error(TEXT("IMPORT_FAILED"), FString::Printf(TEXT("ImportFromIdentity returned %s (IdentityNotConformed means run the Identity's conform/solve first)"), *ImportErrorName(Code)), Out);
	}
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::ImportFromFaceDna(const FString& CharacterPath, const FString& DnaFilePath, bool bImportWholeRig, bool bIsolateHeadFromBody)
{
	if (!FPaths::FileExists(DnaFilePath))
	{
		return VmhJson::Error(TEXT("FILE_NOT_FOUND"), FString::Printf(TEXT("No file at %s"), *DnaFilePath));
	}
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	FImportFromDNAParams Params;
	Params.bImportWholeRig = bImportWholeRig;
	Params.bIsolateHeadFromBody = bIsolateHeadFromBody;
	const EImportErrorCode Code = Ctx.Subsystem->ImportFromFaceDna(Ctx.Character, DnaFilePath, Params);
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("dna_file"), DnaFilePath);
	Out->SetStringField(TEXT("import_result"), ImportErrorName(Code));
	if (Code != EImportErrorCode::Success)
	{
		Out->SetStringField(TEXT("character"), Ctx.Character->GetPathName());
		return VmhJson::Error(TEXT("IMPORT_FAILED"), FString::Printf(TEXT("ImportFromFaceDna returned %s"), *ImportErrorName(Code)), Out);
	}
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::ImportFromTemplate(const FString& CharacterPath, const FString& HeadMeshPath, const FString& LeftEyeMeshPath, const FString& RightEyeMeshPath, const FString& TeethMeshPath)
{
	UObject* Head = VmhAssets::Load(HeadMeshPath);
	if (!Head)
	{
		return VmhJson::Error(TEXT("MESH_NOT_FOUND"), FString::Printf(TEXT("No head mesh at %s"), *HeadMeshPath));
	}
	UObject* LeftEye = LeftEyeMeshPath.IsEmpty() ? nullptr : VmhAssets::Load(LeftEyeMeshPath);
	UObject* RightEye = RightEyeMeshPath.IsEmpty() ? nullptr : VmhAssets::Load(RightEyeMeshPath);
	UObject* Teeth = TeethMeshPath.IsEmpty() ? nullptr : VmhAssets::Load(TeethMeshPath);

	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	FImportFromTemplateParams Params;
	Params.bUseEyeMeshes = LeftEye && RightEye;
	Params.bUseTeethMesh = Teeth != nullptr;
	const EImportErrorCode Code = Ctx.Subsystem->ImportFromTemplate(Ctx.Character, Head, LeftEye, RightEye, Teeth, Params);
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("import_result"), ImportErrorName(Code));
	if (Code != EImportErrorCode::Success)
	{
		Out->SetStringField(TEXT("character"), Ctx.Character->GetPathName());
		return VmhJson::Error(TEXT("IMPORT_FAILED"), FString::Printf(TEXT("ImportFromTemplate returned %s"), *ImportErrorName(Code)), Out);
	}
	return CharacterResult(Ctx, Out);
}

// =====================================================================
// Cloud
// =====================================================================

FString UMetaHumanCharacterService::RequestAutoRig(const FString& CharacterPath, const FString& RigType, bool bBlocking)
{
	const UEnum* Enum = StaticEnum<EMetaHumanRigType>();
	int64 Value = RigType.IsEmpty() ? static_cast<int64>(EMetaHumanRigType::JointsOnly) : VmhJson::ParseEnum(Enum, RigType);
	if (Value == INDEX_NONE)
	{
		return VmhJson::Error(TEXT("BAD_RIG_TYPE"), FString::Printf(TEXT("RigType must be one of: %s"), *VmhJson::EnumNames(Enum)));
	}
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	FMetaHumanCharacterAutoRiggingRequestParams Params;
	Params.RigType = static_cast<EMetaHumanRigType>(Value);
	Params.bBlocking = bBlocking;
	Params.bReportProgress = !bBlocking;
	Ctx.Subsystem->RequestAutoRigging(Ctx.Character, Params);

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("rig_type"), Enum->GetNameStringByValue(Value));
	Out->SetBoolField(TEXT("blocking"), bBlocking);
	Out->SetBoolField(TEXT("auto_rigging_in_progress"), Ctx.Subsystem->IsAutoRiggingFace(Ctx.Character));
	Out->SetBoolField(TEXT("has_face_dna"), Ctx.Character->HasFaceDNA());
	if (Ctx.Subsystem->GetRiggingState(Ctx.Character) != EMetaHumanCharacterRigState::Rigged)
	{
		Out->SetStringField(TEXT("hint"), TEXT("Rig not applied yet: a non-blocking request is still running, or the editor is not signed in to the MetaHuman service (sign in via the MetaHuman Character editor toolbar / Epic account) - check the Output Log."));
	}
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::RemoveFaceRig(const FString& CharacterPath)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	Ctx.Subsystem->RemoveFaceRig(Ctx.Character);
	return CharacterResult(Ctx);
}

FString UMetaHumanCharacterService::RequestTextureSources(const FString& CharacterPath, bool bBlocking)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	FMetaHumanCharacterTextureRequestParams Params;
	Params.bBlocking = bBlocking;
	Params.bReportProgress = !bBlocking;
	Ctx.Subsystem->RequestTextureSources(Ctx.Character, Params);

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetBoolField(TEXT("blocking"), bBlocking);
	Out->SetBoolField(TEXT("requesting_high_res_textures"), Ctx.Subsystem->IsRequestingHighResolutionTextures(Ctx.Character));
	Out->SetBoolField(TEXT("has_high_resolution_textures"), Ctx.Character->HasHighResolutionTextures());
	return CharacterResult(Ctx, Out);
}

// =====================================================================
// Wardrobe
// =====================================================================

FString UMetaHumanCharacterService::GetWardrobe(const FString& CharacterPath)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, false);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	UMetaHumanCollection* Collection = Ctx.Character->GetMutableInternalCollection();
	if (!Collection)
	{
		return VmhJson::Error(TEXT("NO_COLLECTION"), TEXT("Character has no internal collection"));
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetArrayField(TEXT("slots"), VmhJson::NameArray(UMetaHumanCollectionBlueprintLibrary::GetSlotNames(Collection)));

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FMetaHumanPaletteItemKey& Key : UMetaHumanCollectionBlueprintLibrary::GetAllItemKeys(Collection))
	{
		Items.Add(MakeShared<FJsonValueObject>(ItemKeyToJson(Collection, Key)));
	}
	Out->SetArrayField(TEXT("items"), Items);

	TArray<TSharedPtr<FJsonValue>> Selections;
	for (const FMetaHumanPipelineSlotSelectionData& Data : Collection->GetMutableDefaultInstance()->GetSlotSelectionData())
	{
		TSharedPtr<FJsonObject> Item = VmhJson::Obj();
		Item->SetStringField(TEXT("slot"), Data.Selection.SlotName.ToString());
		Item->SetStringField(TEXT("item"), Data.Selection.SelectedItem.ToAssetNameString());
		Selections.Add(MakeShared<FJsonValueObject>(Item));
	}
	Out->SetArrayField(TEXT("selections"), Selections);
	Out->SetStringField(TEXT("character"), Ctx.Character->GetPathName());
	return VmhJson::Ok(Out);
}

FString UMetaHumanCharacterService::AddWardrobeItem(const FString& CharacterPath, const FString& SlotName, const FString& WardrobeItemPath, bool bSelect)
{
	UMetaHumanWardrobeItem* Item = VmhAssets::Load<UMetaHumanWardrobeItem>(WardrobeItemPath);
	if (!Item)
	{
		return VmhJson::Error(TEXT("WARDROBE_ITEM_NOT_FOUND"), FString::Printf(TEXT("No MetaHuman Wardrobe Item at %s (use MetaHumanSetupService.list_assets('wardrobe'))"), *WardrobeItemPath));
	}
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	UMetaHumanCollection* Collection = Ctx.Character->GetMutableInternalCollection();
	if (!Collection)
	{
		return VmhJson::Error(TEXT("NO_COLLECTION"), TEXT("Character has no internal collection"));
	}
	const FName Slot(*SlotName);
	FMetaHumanPaletteItemKey Key;
	TArray<FMetaHumanPaletteItemKey> Existing = UMetaHumanCollectionBlueprintLibrary::GetItemKeysForWardrobeItem(Collection, Item);
	if (Existing.Num() > 0)
	{
		Key = Existing[0];
	}
	else if (!Collection->TryAddItemFromWardrobeItem(Slot, Item, Key))
	{
		TArray<FName> Slots = UMetaHumanCollectionBlueprintLibrary::GetSlotNames(Collection);
		TArray<FString> SlotStrings;
		for (const FName& S : Slots) SlotStrings.Add(S.ToString());
		return VmhJson::Error(TEXT("ADD_ITEM_FAILED"), FString::Printf(TEXT("Could not add %s to slot '%s' (incompatible item or unknown slot). Slots: %s"), *Item->GetName(), *SlotName, *FString::Join(SlotStrings, TEXT(", "))));
	}

	if (bSelect)
	{
		Collection->GetMutableDefaultInstance()->SetSingleSlotSelection(Slot, Key);
		Ctx.Subsystem->RunCharacterEditorPipelineForPreview(Ctx.Character);
	}

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("slot"), SlotName);
	Out->SetStringField(TEXT("wardrobe_item"), Item->GetPathName());
	Out->SetObjectField(TEXT("item_key"), ItemKeyToJson(Collection, Key));
	Out->SetBoolField(TEXT("selected"), bSelect);
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::SelectWardrobeItem(const FString& CharacterPath, const FString& SlotName, const FString& WardrobeItemPath)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	UMetaHumanCollection* Collection = Ctx.Character->GetMutableInternalCollection();
	if (!Collection)
	{
		return VmhJson::Error(TEXT("NO_COLLECTION"), TEXT("Character has no internal collection"));
	}
	const FName Slot(*SlotName);
	FMetaHumanPaletteItemKey Key; // null key clears the slot
	if (!WardrobeItemPath.IsEmpty())
	{
		UMetaHumanWardrobeItem* Item = VmhAssets::Load<UMetaHumanWardrobeItem>(WardrobeItemPath);
		if (!Item)
		{
			return VmhJson::Error(TEXT("WARDROBE_ITEM_NOT_FOUND"), FString::Printf(TEXT("No MetaHuman Wardrobe Item at %s"), *WardrobeItemPath));
		}
		TArray<FMetaHumanPaletteItemKey> Keys = UMetaHumanCollectionBlueprintLibrary::GetItemKeysForWardrobeItem(Collection, Item);
		if (Keys.Num() == 0)
		{
			return VmhJson::Error(TEXT("ITEM_NOT_IN_COLLECTION"), TEXT("That wardrobe item is not in the character's collection yet; call add_wardrobe_item first"));
		}
		Key = Keys[0];
	}
	Collection->GetMutableDefaultInstance()->SetSingleSlotSelection(Slot, Key);
	Ctx.Subsystem->RunCharacterEditorPipelineForPreview(Ctx.Character);

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("slot"), SlotName);
	Out->SetStringField(TEXT("item"), Key.IsNull() ? TEXT("") : Key.ToAssetNameString());
	return CharacterResult(Ctx, Out);
}

// =====================================================================
// Assembly and placement
// =====================================================================

FString UMetaHumanCharacterService::AssembleForPreview(const FString& CharacterPath)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	Ctx.Subsystem->RunCharacterEditorPipelineForPreview(Ctx.Character);
	return CharacterResult(Ctx);
}

FString UMetaHumanCharacterService::CanBuild(const FString& CharacterPath)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	FText Reason;
	const bool bCan = Ctx.Subsystem->CanBuildMetaHuman(Ctx.Character, Reason);
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetBoolField(TEXT("can_build"), bCan);
	if (!bCan)
	{
		Out->SetStringField(TEXT("reason"), Reason.ToString());
	}
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::Build(const FString& CharacterPath, const FString& PipelineType, const FString& Quality, const FString& BuildPath, const FString& NameOverride)
{
	const UEnum* PipelineEnum = StaticEnum<EMetaHumanDefaultPipelineType>();
	const UEnum* QualityEnum = StaticEnum<EMetaHumanQualityLevel>();
	const int64 PipelineValue = PipelineType.IsEmpty() ? static_cast<int64>(EMetaHumanDefaultPipelineType::Cinematic) : VmhJson::ParseEnum(PipelineEnum, PipelineType);
	const int64 QualityValue = Quality.IsEmpty() ? static_cast<int64>(EMetaHumanQualityLevel::Cinematic) : VmhJson::ParseEnum(QualityEnum, Quality);
	if (PipelineValue == INDEX_NONE)
	{
		return VmhJson::Error(TEXT("BAD_PIPELINE"), FString::Printf(TEXT("PipelineType must be one of: %s"), *VmhJson::EnumNames(PipelineEnum)));
	}
	if (QualityValue == INDEX_NONE)
	{
		return VmhJson::Error(TEXT("BAD_QUALITY"), FString::Printf(TEXT("Quality must be one of: %s"), *VmhJson::EnumNames(QualityEnum)));
	}

	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	FText Reason;
	if (!Ctx.Subsystem->CanBuildMetaHuman(Ctx.Character, Reason))
	{
		return VmhJson::Error(TEXT("CANNOT_BUILD"), Reason.ToString());
	}

	FMetaHumanCharacterEditorBuildParameters Params;
	Params.PipelineType = static_cast<EMetaHumanDefaultPipelineType>(PipelineValue);
	Params.PipelineQuality = static_cast<EMetaHumanQualityLevel>(QualityValue);
	Params.AbsoluteBuildPath = BuildPath;
	Params.NameOverride = NameOverride;

	const double Start = FPlatformTime::Seconds();
	Ctx.Subsystem->BuildMetaHuman(Ctx.Character, Params);
	const double Seconds = FPlatformTime::Seconds() - Start;

	const FString Root = BuildPath.IsEmpty() ? TEXT("/Game/MetaHumans") : BuildPath;
	const FString Leaf = NameOverride.IsEmpty() ? Ctx.Character->GetName() : NameOverride;
	TArray<FString> Blueprints = VmhAssets::FindAssetsOfClass(TEXT("/Script/Engine.Blueprint"), Root / Leaf);
	if (Blueprints.Num() == 0)
	{
		Blueprints = VmhAssets::FindAssetsOfClass(TEXT("/Script/Engine.Blueprint"), Root);
	}

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("pipeline_type"), PipelineEnum->GetNameStringByValue(PipelineValue));
	Out->SetStringField(TEXT("quality"), QualityEnum->GetNameStringByValue(QualityValue));
	Out->SetStringField(TEXT("output_folder"), Root / Leaf);
	Out->SetNumberField(TEXT("seconds"), Seconds);
	Out->SetArrayField(TEXT("blueprints"), VmhJson::StringArray(Blueprints));
	Out->SetStringField(TEXT("hint"), TEXT("Place the Blueprint in a level; its Live Link / animation inputs are on the actor. Save all packages afterwards."));
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::SpawnInLevel(const FString& CharacterPath, FVector Location, FRotator Rotation, bool bKeepTransient)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	AActor* Actor = Ctx.Subsystem->SpawnMetaHumanActor(Ctx.Character, bKeepTransient);
	if (!Actor)
	{
		return VmhJson::Error(TEXT("SPAWN_FAILED"), TEXT("SpawnMetaHumanActor returned null (no editor world?)"));
	}
	Actor->SetActorLocationAndRotation(Location, Rotation);
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("actor"), Actor->GetPathName());
#if WITH_EDITOR
	Out->SetStringField(TEXT("label"), Actor->GetActorLabel());
#endif
	Out->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	Out->SetBoolField(TEXT("transient"), bKeepTransient);
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::ExportDna(const FString& CharacterPath, const FString& ProjectPath, const FString& ExternalPath, bool bHead, bool bBody)
{
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	UClass* Library = VmhJson::FindClass(TEXT("/Script/MetaHumanCharacterEditor.MetaHumanCharacterExportBlueprintLibrary"));
	if (!Library)
	{
		return VmhJson::Error(TEXT("NO_EXPORT_LIBRARY"), TEXT("MetaHumanCharacterExportBlueprintLibrary not found"));
	}
	TSharedPtr<FJsonObject> Params = VmhJson::Obj();
	Params->SetStringField(TEXT("ProjectPath"), ProjectPath);
	Params->SetStringField(TEXT("ExternalPath"), ExternalPath);
	Params->SetBoolField(TEXT("bDNAHead"), bHead);
	Params->SetBoolField(TEXT("bDNABody"), bBody);
	Params->SetBoolField(TEXT("bOverwriteExistingAssets"), true);
	TSharedPtr<FJsonObject> Args = VmhJson::Obj();
	Args->SetStringField(TEXT("InCharacter"), Ctx.Character->GetPathName());
	Args->SetObjectField(TEXT("InParams"), Params);
	TSharedPtr<FJsonObject> Result;
	FString Error;
	if (!VmhJson::CallStaticFunction(Library, TEXT("ExportDNA"), Args, Result, Error))
	{
		return VmhJson::Error(TEXT("EXPORT_FAILED"), Error);
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("project_path"), ProjectPath);
	Out->SetStringField(TEXT("external_path"), ExternalPath);
	return CharacterResult(Ctx, Out);
}

// =====================================================================
// Custom mesh to MetaHuman (2026-10-04): what the Mesh Import tool does behind "Import > from custom mesh",
// without its viewport. The recipe is the tool's own (MetaHumanCharacterEditorMeshImportTool.cpp,
// MetaHumanCharacterEditorMeshTargetContourMechanic.cpp): the target's vertices and triangles through
// GetMeshDataForConforming, the head framed from the mesh's top 15%, a front-on render whose face landmarks are
// tracked and stored on the character, then ConformToTargetMeshes with the "combined" pipeline.
// =====================================================================

namespace
{
	/** Load a static or skeletal mesh asset (anything else is an error). */
	UObject* LoadMeshAsset(const FString& Path, FString& OutError)
	{
		UObject* Object = VmhAssets::Load(Path);
		if (!Object)
		{
			OutError = FString::Printf(TEXT("No mesh asset at %s"), *Path);
			return nullptr;
		}
		if (!Object->IsA<UStaticMesh>() && !Object->IsA<USkeletalMesh>())
		{
			OutError = FString::Printf(TEXT("%s is a %s, not a static or skeletal mesh"), *Path, *Object->GetClass()->GetName());
			return nullptr;
		}
		return Object;
	}

	/** The key the character stores the conform's state under: the combined mesh, or body + head. */
	bool BuildTargetMeshKey(const FString& MeshPath, const FString& HeadMeshPath, FMetaHumanCharacterTargetMeshKey& OutKey, UObject*& OutMesh, UObject*& OutHead, FString& OutError)
	{
		OutMesh = LoadMeshAsset(MeshPath, OutError);
		if (!OutMesh)
		{
			return false;
		}
		OutHead = nullptr;
		if (!HeadMeshPath.IsEmpty())
		{
			OutHead = LoadMeshAsset(HeadMeshPath, OutError);
			if (!OutHead)
			{
				return false;
			}
			OutKey.BodyMesh = OutMesh;
			OutKey.HeadMesh = OutHead;
		}
		else
		{
			OutKey.CombinedMesh = OutMesh;
		}
		return true;
	}

	FBox BoundsOf(const TArray<FVector3f>& Vertices)
	{
		FBox Box(ForceInit);
		for (const FVector3f& V : Vertices)
		{
			Box += FVector(V);
		}
		return Box;
	}

	/** Which way the mesh faces, read off the feet: the toes stand ahead of the ankles (Master Smith's mh_conform heuristic). */
	FVector FacingOf(const TArray<FVector3f>& Vertices, const FBox& Bounds)
	{
		const double H = Bounds.Max.Z - Bounds.Min.Z;
		FVector Toes(0), Ankles(0);
		int32 NToes = 0, NAnkles = 0;
		for (const FVector3f& V : Vertices)
		{
			if (V.Z < Bounds.Min.Z + 0.04 * H) { Toes += FVector(V); ++NToes; }
			else if (V.Z > Bounds.Min.Z + 0.08 * H && V.Z < Bounds.Min.Z + 0.12 * H) { Ankles += FVector(V); ++NAnkles; }
		}
		if (NToes < 10 || NAnkles < 10)
		{
			return FVector(0, 1, 0);
		}
		FVector F = Toes / NToes - Ankles / NAnkles;
		F.Z = 0;
		return F.SizeSquared() > KINDA_SMALL_NUMBER ? F.GetSafeNormal() : FVector(0, 1, 0);
	}

	/** The tool's head box on a combined mesh: the top 15% of the height, tightened to 60% sideways. */
	FBox HeadBoundsOf(const FBox& MeshBounds)
	{
		const double H = MeshBounds.Max.Z - MeshBounds.Min.Z;
		FBox Head(FVector(MeshBounds.Min.X, MeshBounds.Min.Y, MeshBounds.Max.Z - H * 0.15), MeshBounds.Max);
		const FVector C = Head.GetCenter();
		FVector E = Head.GetExtent();
		E.X *= 0.6; E.Y *= 0.6;
		return FBox(C - E, C + E);
	}

	/**
	 * Render the target mesh's head front-on in a private preview scene and track the face landmarks, as the tool's
	 * contour mechanic does with auto framing. Fills the tracking results (camera, image size, curves).
	 */
	bool TrackFaceOnMesh(UMetaHumanCharacterEditorSubsystem* Subsystem, UObject* Mesh, const TArray<FVector3f>& Vertices, const TArray<int32>& Triangles,
	                     const FBox& HeadBounds, const FVector& Facing, int32 ImageSize,
	                     FMetaHumanCharacterTargetTrackingResults& OutResults, TMap<FString, FTrackingPoints>& OutCurves, TSharedPtr<FJsonObject> Report, FString& OutError)
	{
		const FVector HeadCenter = HeadBounds.GetCenter();
		const float HeadSize = HeadBounds.GetExtent().Size();
		if (HeadSize <= KINDA_SMALL_NUMBER)
		{
			OutError = TEXT("degenerate head bounds");
			return false;
		}
		// the tool's framing: 1.8 head sizes away along the facing, raised a fifth of the head, the head half the frame
		const float CameraDistance = HeadSize * 1.8f;
		const FVector CameraPosition = HeadCenter + Facing * CameraDistance + FVector(0, 0, HeadSize * 0.2f);
		const FRotator CameraRotation = (HeadCenter - CameraPosition).Rotation();
		const float FOVDegrees = FMath::Clamp(FMath::RadiansToDegrees(2.0f * FMath::Atan(HeadSize / (CameraDistance * 2.f))), 35.0f, 110.0f);

		// what the tracker is given, success or not
		TSharedPtr<FJsonObject> Track = VmhJson::Obj();
		Track->SetStringField(TEXT("head_center_cm"), HeadCenter.ToString());
		Track->SetNumberField(TEXT("head_size_cm"), HeadSize);
		Track->SetStringField(TEXT("camera_cm"), CameraPosition.ToString());
		Track->SetStringField(TEXT("camera_rotation"), CameraRotation.ToString());
		Track->SetNumberField(TEXT("fov_deg"), FOVDegrees);
		Track->SetNumberField(TEXT("image_size"), ImageSize);
		if (Report.IsValid())
		{
			Report->SetObjectField(TEXT("face_tracking"), Track);
		}

		// lit from the camera, softly: the default key from above left the face in hard shadow (2026-10-04)
		// lit from the camera, softly (2.5 / 1.5 blew a white default material out to a flat mask, 2026-10-05)
		FPreviewScene Scene{ FPreviewScene::ConstructionValues().SetSkyBrightness(0.6f).SetLightBrightness(1.2f) };
		Scene.SetLightDirection(CameraRotation);
		// The mesh with its own textures when it can be rendered as it is: a face painted on the seed gives the tracker
		// eyes and brows. A Nanite static mesh cannot (the capture draws its 40-face fallback), so it goes in as a
		// dynamic mesh of the conform's own vertices, as the Mesh Import tool renders, in the tool's skin material.
		UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh);
		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh);
		UMeshComponent* MeshComponent = nullptr;
		FString RenderedAs;
		if (StaticMesh && !StaticMesh->IsNaniteEnabled())
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
			C->SetStaticMesh(StaticMesh);
			C->SetForcedLodModel(1);
			MeshComponent = C;
			RenderedAs = TEXT("static mesh, its own materials, LOD0");
		}
		else if (SkeletalMesh)
		{
			USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
			C->SetSkeletalMeshAsset(SkeletalMesh);
			C->SetForcedLOD(1);
			MeshComponent = C;
			RenderedAs = TEXT("skeletal mesh, its own materials, LOD0");
		}
		else
		{
			UE::Geometry::FDynamicMesh3 DynamicMesh;
			DynamicMesh.EnableVertexNormals(FVector3f::UnitZ());
			for (const FVector3f& V : Vertices)
			{
				DynamicMesh.AppendVertex(FVector3d(V));
			}
			for (int32 i = 0; i + 2 < Triangles.Num(); i += 3)
			{
				DynamicMesh.AppendTriangle(Triangles[i], Triangles[i + 1], Triangles[i + 2]);
			}
			UE::Geometry::FMeshNormals::QuickComputeVertexNormals(DynamicMesh);
			UDynamicMeshComponent* C = NewObject<UDynamicMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
			C->SetMesh(MoveTemp(DynamicMesh));
			MeshComponent = C;
			RenderedAs = TEXT("dynamic mesh of the conform vertices (a Nanite static mesh), the tool's skin material");
		}
		Scene.AddComponent(MeshComponent, FTransform::Identity);
		UMaterialInterface* Skin = LoadObject<UMaterialInterface>(nullptr, TEXT("/MetaHumanCharacter/Materials/MI_M2M_Head.MI_M2M_Head"));
		if (!Skin)
		{
			Skin = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		for (int32 i = 0; i < FMath::Max(1, MeshComponent->GetNumMaterials()); ++i)
		{
			if (!MeshComponent->GetMaterial(i) || Cast<UDynamicMeshComponent>(MeshComponent))
			{
				MeshComponent->SetMaterial(i, Skin);
			}
		}
		Track->SetStringField(TEXT("rendered_as"), RenderedAs);
		Track->SetNumberField(TEXT("render_triangles"), Triangles.Num() / 3);

		UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		RenderTarget->RenderTargetFormat = RTF_RGBA8;
		RenderTarget->InitAutoFormat(ImageSize, ImageSize);
		RenderTarget->UpdateResourceImmediate(false);
		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Capture->TextureTarget = RenderTarget;
		Capture->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->bAlwaysPersistRenderingState = true;
		Capture->ProjectionType = ECameraProjectionMode::Perspective;
		Capture->FOVAngle = FOVDegrees;
		Scene.AddComponent(Capture, FTransform(CameraRotation, CameraPosition));
		Capture->CaptureScene();

		TArray<FColor> Pixels;
		if (!UKismetRenderingLibrary::ReadRenderTarget(Capture, RenderTarget, Pixels) || Pixels.Num() != ImageSize * ImageSize)
		{
			OutError = TEXT("could not read the face render");
			return false;
		}
		// the render on disk, success or not: the agent reads it when the tracker finds no face
		const FString DumpPath = FPaths::ProjectSavedDir() / TEXT("VibeMetaHumans") / FString::Printf(TEXT("FaceTrack_%s.png"), *Mesh->GetName());
		{
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(ImageSize, ImageSize, Pixels, Png);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(DumpPath), true);
			FFileHelper::SaveArrayToFile(Png, *DumpPath);
			Track->SetStringField(TEXT("render"), DumpPath);
			if (Report.IsValid())
			{
				Report->SetStringField(TEXT("face_render"), DumpPath);
			}
		}
		if (!Subsystem->TrackFaceLandmarksFromImage(Pixels, ImageSize, ImageSize, OutCurves) || OutCurves.IsEmpty())
		{
			OutError = FString::Printf(TEXT("the face landmark tracker found no face in the front-on render %s (is the head bald and facing +Y, the mesh in centimetres with its feet on z=0?)"), *DumpPath);
			return false;
		}

		FMinimalViewInfo ViewInfo;
		Capture->GetCameraView(0.0f, ViewInfo);
		ViewInfo.AspectRatio = 1.0f;
		OutResults.CameraViewInfo = ViewInfo;
		OutResults.ImageSize = FIntPoint(ImageSize, ImageSize);
		OutResults.CurveTrackingPoints.Reset();
		int32 PointCount = 0;
		for (const TPair<FString, FTrackingPoints>& Pair : OutCurves)
		{
			FMetaHumanCharacterCurveTrackingPoints Curve;
			Curve.Points = Pair.Value.TrackingPoints;
			PointCount += Curve.Points.Num();
			OutResults.CurveTrackingPoints.Add(Pair.Key, Curve);
		}
		Track->SetNumberField(TEXT("curves"), OutCurves.Num());
		Track->SetNumberField(TEXT("points"), PointCount);
		return true;
	}
}

FString UMetaHumanCharacterService::ImportFromCustomMesh(const FString& CharacterPath, const FString& MeshPath, const FString& HeadMeshPath, bool bAutoSolve, bool bTrackFace, int32 TrackingImageSize)
{
	FMetaHumanCharacterTargetMeshKey Key;
	UObject* Mesh = nullptr;
	UObject* Head = nullptr;
	FString Error;
	if (!BuildTargetMeshKey(MeshPath, HeadMeshPath, Key, Mesh, Head, Error))
	{
		return VmhJson::Error(TEXT("MESH_NOT_FOUND"), Error);
	}
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}

	FConformTargetMesh Target;
	if (!UMetaHumanCharacterEditorSubsystem::GetMeshDataForConforming(Mesh, Target.BodyVertices, Target.BodyVertexIndices) || Target.BodyVertices.IsEmpty())
	{
		return VmhJson::Error(TEXT("MESH_DATA_FAILED"), FString::Printf(TEXT("GetMeshDataForConforming gave no vertices for %s"), *MeshPath));
	}
	Target.TargetPartsType = ETargetPartsType::Combined;
	if (Head)
	{
		if (!UMetaHumanCharacterEditorSubsystem::GetMeshDataForConforming(Head, Target.HeadVertices, Target.HeadVertexIndices) || Target.HeadVertices.IsEmpty())
		{
			return VmhJson::Error(TEXT("MESH_DATA_FAILED"), FString::Printf(TEXT("GetMeshDataForConforming gave no vertices for %s"), *HeadMeshPath));
		}
		Target.TargetPartsType = ETargetPartsType::HeadAndBody;
	}

	const FBox MeshBounds = BoundsOf(Target.BodyVertices);
	const FVector Facing = FacingOf(Target.BodyVertices, MeshBounds);
	const FBox HeadBounds = Head ? BoundsOf(Target.HeadVertices) : HeadBoundsOf(MeshBounds);

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("mesh"), Mesh->GetPathName());
	Out->SetStringField(TEXT("parts"), Head ? TEXT("HeadAndBody") : TEXT("Combined"));
	Out->SetNumberField(TEXT("vertices"), Target.BodyVertices.Num() + Target.HeadVertices.Num());
	Out->SetNumberField(TEXT("triangles"), (Target.BodyVertexIndices.Num() + Target.HeadVertexIndices.Num()) / 3);
	Out->SetNumberField(TEXT("height_cm"), MeshBounds.Max.Z - MeshBounds.Min.Z);
	Out->SetNumberField(TEXT("feet_z_cm"), MeshBounds.Min.Z);
	Out->SetStringField(TEXT("facing"), Facing.ToString());
	TArray<FString> Warnings;
	if (FMath::Abs(MeshBounds.Min.Z) > 5.0)
	{
		Warnings.Add(FString::Printf(TEXT("the feet stand at z=%.1f cm, not on the floor"), MeshBounds.Min.Z));
	}
	const double HeightCm = MeshBounds.Max.Z - MeshBounds.Min.Z;
	if (HeightCm < 100 || HeightCm > 260)
	{
		Warnings.Add(FString::Printf(TEXT("the mesh is %.0f cm tall: not a human in centimetres (a metre-unit import?)"), HeightCm));
	}
	if (FVector::DotProduct(Facing, FVector(0, 1, 0)) < 0.94)
	{
		Warnings.Add(FString::Printf(TEXT("the mesh faces %s; MetaHumans face +Y - re-import it turned, or expect a rough solve"), *Facing.ToString()));
	}

	FConformTargetParams Params;
	Params.ConformTargetMesh = Target;
	Params.bEstimateBodyJointsFromMesh = false;
	Params.bAutoSolve = bAutoSolve;
	if (bAutoSolve && Params.BodyConformSolveSettings.PipelineName.IsEmpty())
	{
		Params.BodyConformSolveSettings.PipelineName = Head ? TEXT("head_body") : TEXT("combined");
	}
	if (bTrackFace)
	{
		FMetaHumanCharacterTargetTrackingResults Tracking;
		TMap<FString, FTrackingPoints> Curves;
		FString TrackError;
		if (TrackFaceOnMesh(Ctx.Subsystem, Head ? Head : Mesh, Head ? Target.HeadVertices : Target.BodyVertices, Head ? Target.HeadVertexIndices : Target.BodyVertexIndices,
		                    HeadBounds, Facing, FMath::Clamp(TrackingImageSize, 256, 4096), Tracking, Curves, Out, TrackError))
		{
			Ctx.Subsystem->CommitTargetMeshTrackingResults(Ctx.Character, Key, Tracking);
			Params.CurveTrackingPoints = Curves;
			Params.CameraViewInfo = Tracking.CameraViewInfo;
			Params.ImageSize = Tracking.ImageSize;
		}
		else
		{
			Warnings.Add(FString::Printf(TEXT("face tracking failed (%s): the solve runs on the body alone and the face stays the archetype's"), *TrackError));
		}
	}
	Out->SetArrayField(TEXT("warnings"), VmhJson::StringArray(Warnings));

	const double Started = FPlatformTime::Seconds();
	const bool bSolved = Ctx.Subsystem->ConformToTargetMeshes(Ctx.Character, Key, Params);
	Out->SetBoolField(TEXT("solved"), bSolved);
	Out->SetNumberField(TEXT("seconds"), FPlatformTime::Seconds() - Started);
	if (!bSolved)
	{
		Out->SetStringField(TEXT("character"), Ctx.Character->GetPathName());
		return VmhJson::Error(TEXT("CONFORM_FAILED"), TEXT("ConformToTargetMeshes returned false (see the Output Log; a mesh in metres, lying down, or without a head is the usual cause)"), Out);
	}
	Out->SetStringField(TEXT("next"), TEXT("save_posed_dna (the baking target, in this pose) then commit_a_pose, request_auto_rig, request_texture_sources, build"));
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::SavePosedDna(const FString& CharacterPath, const FString& MeshPath, const FString& HeadMeshPath, const FString& ProjectPath, const FString& ExternalPath, const FString& AssetName)
{
	if (ProjectPath.IsEmpty() && ExternalPath.IsEmpty())
	{
		return VmhJson::Error(TEXT("NO_DESTINATION"), TEXT("give ProjectPath (a content folder) and/or ExternalPath (a disk folder)"));
	}
	FMetaHumanCharacterTargetMeshKey Key;
	UObject* Mesh = nullptr;
	UObject* Head = nullptr;
	FString Error;
	if (!BuildTargetMeshKey(MeshPath, HeadMeshPath, Key, Mesh, Head, Error))
	{
		return VmhJson::Error(TEXT("MESH_NOT_FOUND"), Error);
	}
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	if (!ExternalPath.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*ExternalPath, true);
	}
	FMetaHumanPosedDNAExportParams Params;
	Params.ProjectPath = ProjectPath;
	Params.ExternalPath = ExternalPath;
	Params.TargetMeshKey = Key;
	Params.AssetName = AssetName;
	Params.bOverwriteExistingAssets = true;
	UMetaHumanCharacterExportBlueprintLibrary::ExportPosedDNA(Ctx.Character, Params);

	const FString Name = AssetName.IsEmpty() ? Ctx.Character->GetName() + TEXT("_Posed") : AssetName;
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	if (!ProjectPath.IsEmpty())
	{
		const FString AssetPath = ProjectPath / Name;
		Out->SetStringField(TEXT("dna_asset"), AssetPath);
		Out->SetBoolField(TEXT("dna_asset_exists"), VmhAssets::Load(AssetPath) != nullptr);
	}
	if (!ExternalPath.IsEmpty())
	{
		const FString File = ExternalPath / Name + TEXT(".dna");
		Out->SetStringField(TEXT("dna_file"), File);
		Out->SetBoolField(TEXT("dna_file_exists"), FPaths::FileExists(File));
		Out->SetNumberField(TEXT("dna_file_bytes"), FPaths::FileExists(File) ? static_cast<double>(IFileManager::Get().FileSize(*File)) : 0.0);
	}
	Out->SetStringField(TEXT("hint"), TEXT("nothing written means no posed state was stored for this mesh key: run import_from_custom_mesh with the same MeshPath first"));
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::CommitAPose(const FString& CharacterPath, const FString& MeshPath, const FString& HeadMeshPath)
{
	FMetaHumanCharacterTargetMeshKey Key;
	UObject* Mesh = nullptr;
	UObject* Head = nullptr;
	FString Error;
	if (!BuildTargetMeshKey(MeshPath, HeadMeshPath, Key, Mesh, Head, Error))
	{
		return VmhJson::Error(TEXT("MESH_NOT_FOUND"), Error);
	}
	FEditContext Ctx = OpenForEdit(CharacterPath, true);
	if (!Ctx.IsValid())
	{
		return Ctx.ToError();
	}
	Ctx.Subsystem->CommitPosedStateAsAPose(Ctx.Character, Key);
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("state"), TEXT("the body and face states are now in the MetaHuman A-pose; a bake against the target mesh no longer lines up (use the posed DNA)"));
	return CharacterResult(Ctx, Out);
}

FString UMetaHumanCharacterService::GenerateSkeletalMeshFromDna(const FString& DnaPath, const FString& PackagePath, const FString& AssetName, const FString& Skeleton)
{
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		return VmhJson::Error(TEXT("BAD_ARGS"), TEXT("PackagePath and AssetName are required"));
	}
	TSharedPtr<IDNAReader> Reader;
	UDNA* DnaAsset = nullptr;
	FDNAConfig Config = FDNAConfig::Legacy(ECoordinateSystemTransformPolicy::Transform);
	if (DnaPath.EndsWith(TEXT(".dna"), ESearchCase::IgnoreCase) || DnaPath.EndsWith(TEXT(".mhdna"), ESearchCase::IgnoreCase))
	{
		if (!FPaths::FileExists(DnaPath))
		{
			return VmhJson::Error(TEXT("DNA_NOT_FOUND"), FString::Printf(TEXT("No file at %s"), *DnaPath));
		}
		Reader = ReadDNAFromFile(DnaPath, EDNADataLayer::All);
	}
	else
	{
		DnaAsset = VmhAssets::Load<UDNA>(DnaPath);
		if (!DnaAsset)
		{
			return VmhJson::Error(TEXT("DNA_NOT_FOUND"), FString::Printf(TEXT("No DNA asset at %s"), *DnaPath));
		}
		Reader = DnaAsset->GetDNAReader();
		Config = DnaAsset->DNAConfig;
	}
	if (!Reader.IsValid())
	{
		return VmhJson::Error(TEXT("DNA_UNREADABLE"), FString::Printf(TEXT("Could not read the DNA at %s"), *DnaPath));
	}
	USkeleton* SkeletonAsset = nullptr;
	const FString Kind = Skeleton.ToLower();
	if (Kind == TEXT("body"))
	{
		SkeletonAsset = LoadObject<USkeleton>(nullptr, TEXT("/MetaHumanCharacter/Female/Medium/NormalWeight/Body/metahuman_base_skel.metahuman_base_skel"));
	}
	else if (Kind == TEXT("face"))
	{
		SkeletonAsset = LoadObject<USkeleton>(nullptr, TEXT("/MetaHumanCharacter/Face/Face_Archetype_Skeleton.Face_Archetype_Skeleton"));
	}
	else if (!Kind.IsEmpty() && Kind != TEXT("none"))
	{
		SkeletonAsset = VmhAssets::Load<USkeleton>(Skeleton);
		if (!SkeletonAsset)
		{
			return VmhJson::Error(TEXT("SKELETON_NOT_FOUND"), FString::Printf(TEXT("No skeleton at %s (use body, face, none or a path)"), *Skeleton));
		}
	}
	USkeletalMesh* SkeletalMesh = FInterchangeDnaModule::GetModule().ImportSync(AssetName, PackagePath, Reader, SkeletonAsset, Config);
	if (!SkeletalMesh)
	{
		return VmhJson::Error(TEXT("GENERATE_FAILED"), TEXT("the DNA interchange import made no skeletal mesh (see the Output Log)"));
	}
	if (DnaAsset)
	{
		UDNAAssetUserData* UserData = NewObject<UDNAAssetUserData>(SkeletalMesh);
		UserData->DNAAsset = DnaAsset;
		SkeletalMesh->AddAssetUserData(UserData);
	}
	else
	{
		FInterchangeDnaModule::GetModule().CreateAndAttachDNAToSkeletalMesh(SkeletalMesh, Reader);
	}
	SkeletalMesh->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("skeletal_mesh"), SkeletalMesh->GetPathName());
	Out->SetStringField(TEXT("skeleton"), SkeletalMesh->GetSkeleton() ? SkeletalMesh->GetSkeleton()->GetPathName() : TEXT(""));
	Out->SetNumberField(TEXT("bones"), SkeletalMesh->GetRefSkeleton().GetNum());
	const FBoxSphereBounds Bounds = SkeletalMesh->GetBounds();
	Out->SetStringField(TEXT("bounds_extent_cm"), Bounds.BoxExtent.ToString());
	Out->SetStringField(TEXT("bounds_origin_cm"), Bounds.Origin.ToString());
	if (SkeletalMesh->GetImportedModel() && SkeletalMesh->GetImportedModel()->LODModels.Num() > 0)
	{
		Out->SetNumberField(TEXT("lod0_vertices"), SkeletalMesh->GetImportedModel()->LODModels[0].NumVertices);
	}
	Out->SetStringField(TEXT("next"), TEXT("export_fbx it for a bake against the target mesh; save the package when it should stay"));
	return VmhJson::Ok(Out);
}

// =====================================================================
// Round trips with a DCC
// =====================================================================

FString UMetaHumanCharacterService::ExportFbx(const FString& AssetPath, const FString& FilePath)
{
	FString Error;
	UObject* Asset = LoadMeshAsset(AssetPath, Error);
	if (!Asset)
	{
		return VmhJson::Error(TEXT("MESH_NOT_FOUND"), Error);
	}
	if (FilePath.IsEmpty() || !FilePath.EndsWith(TEXT(".fbx"), ESearchCase::IgnoreCase))
	{
		return VmhJson::Error(TEXT("BAD_PATH"), TEXT("FilePath must end in .fbx"));
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);

	// an empty material slot took the exporter down with an array assertion (2026-10-04): fill them for the export
	TArray<int32> Filled;
	UMaterialInterface* Default = UMaterial::GetDefaultMaterial(MD_Surface);
	if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
	{
		TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
		for (int32 i = 0; i < Materials.Num(); ++i)
		{
			if (!Materials[i].MaterialInterface)
			{
				Materials[i].MaterialInterface = Default;
				Filled.Add(i);
			}
		}
		if (!SkeletalMesh->GetResourceForRendering() || SkeletalMesh->GetResourceForRendering()->LODRenderData.IsEmpty())
		{
			return VmhJson::Error(TEXT("NO_RENDER_DATA"), TEXT("the skeletal mesh has no LOD render data to export"));
		}
		// a section pointing past the material slots takes the exporter down with an array assertion even after the
		// slots are filled (the optional content's archetype_SkelMesh, 2026-10-04): refuse instead of crashing
		for (const FSkelMeshRenderSection& Section : SkeletalMesh->GetResourceForRendering()->LODRenderData[0].RenderSections)
		{
			if (Section.MaterialIndex < 0 || Section.MaterialIndex >= Materials.Num())
			{
				for (int32 i : Filled) { Materials[i].MaterialInterface = nullptr; }
				return VmhJson::Error(TEXT("BAD_MATERIAL_INDEX"), FString::Printf(TEXT("LOD0 section uses material index %d but the mesh has %d material slots; the FBX exporter would assert. Duplicate the mesh and fix its materials first"), Section.MaterialIndex, Materials.Num()));
			}
		}
		// (a mesh generated from a DNA carries 8 LOD info entries over 4 LODs and exports fine: not a reason to refuse)
	}
	else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
	{
		TArray<FStaticMaterial>& Materials = StaticMesh->GetStaticMaterials();
		for (int32 i = 0; i < Materials.Num(); ++i)
		{
			if (!Materials[i].MaterialInterface)
			{
				Materials[i].MaterialInterface = Default;
				Filled.Add(i);
			}
		}
		if (!StaticMesh->GetRenderData() || StaticMesh->GetRenderData()->LODResources.IsEmpty())
		{
			return VmhJson::Error(TEXT("NO_RENDER_DATA"), TEXT("the static mesh has no LOD render data to export"));
		}
	}

	UAssetExportTask* Task = NewObject<UAssetExportTask>();
	Task->Object = Asset;
	Task->Filename = FilePath;
	Task->bAutomated = true;
	Task->bReplaceIdentical = true;
	Task->bPrompt = false;
	Task->bSelected = false;
	Task->bUseFileArchive = false;
	Task->bWriteEmptyFiles = false;
	Task->Exporter = Asset->IsA<USkeletalMesh>() ? static_cast<UExporter*>(NewObject<USkeletalMeshExporterFBX>()) : static_cast<UExporter*>(NewObject<UStaticMeshExporterFBX>());
	UFbxExportOption* Options = NewObject<UFbxExportOption>();
	Options->bASCII = false;
	Options->LevelOfDetail = false;
	Options->Collision = false;
	Options->VertexColor = false;
	Options->bExportMorphTargets = false;
	Options->bExportPreviewMesh = false;
	Options->bForceFrontXAxis = false;
	Options->MapSkeletalMotionToRoot = false;
	Task->Options = Options;
	const bool bOk = UExporter::RunAssetExportTask(Task);

	// put the empty slots back
	if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
	{
		for (int32 i : Filled) { SkeletalMesh->GetMaterials()[i].MaterialInterface = nullptr; }
	}
	else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
	{
		for (int32 i : Filled) { StaticMesh->GetStaticMaterials()[i].MaterialInterface = nullptr; }
	}

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("asset"), Asset->GetPathName());
	Out->SetStringField(TEXT("file"), FilePath);
	Out->SetBoolField(TEXT("exists"), FPaths::FileExists(FilePath));
	Out->SetNumberField(TEXT("bytes"), FPaths::FileExists(FilePath) ? static_cast<double>(IFileManager::Get().FileSize(*FilePath)) : 0.0);
	Out->SetNumberField(TEXT("empty_slots_filled"), Filled.Num());
	Out->SetArrayField(TEXT("errors"), VmhJson::StringArray(Task->Errors));
	Out->SetStringField(TEXT("units"), TEXT("centimetres; Blender's FBX importer reads it in metres with the scale on the root"));
	if (!bOk || !FPaths::FileExists(FilePath))
	{
		return VmhJson::Error(TEXT("EXPORT_FAILED"), TEXT("the FBX exporter wrote nothing (see errors and the Output Log)"), Out);
	}
	return VmhJson::Ok(Out);
}

FString UMetaHumanCharacterService::ImportTexture(const FString& FilePath, const FString& PackagePath, const FString& AssetName, const FString& Kind, bool bFlipGreen)
{
	if (!FPaths::FileExists(FilePath))
	{
		return VmhJson::Error(TEXT("FILE_NOT_FOUND"), FString::Printf(TEXT("No file at %s"), *FilePath));
	}
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		return VmhJson::Error(TEXT("BAD_ARGS"), TEXT("PackagePath and AssetName are required"));
	}
	const FString K = Kind.ToLower();
	const bool bNormal = K == TEXT("normal");
	const bool bColor = K.IsEmpty() || K == TEXT("color") || K == TEXT("colour") || K == TEXT("basecolor");
	const bool bMask = K == TEXT("mask") || K == TEXT("masks");

	UTextureFactory* Factory = NewObject<UTextureFactory>();
	UTextureFactory::SuppressImportOverwriteDialog();
	Factory->bFlipNormalMapGreenChannel = bNormal && bFlipGreen;
	Factory->CompressionSettings = bNormal ? TC_Normalmap : bMask ? TC_Masks : TC_Default;
	Factory->LODGroup = bNormal ? TEXTUREGROUP_WorldNormalMap : TEXTUREGROUP_World;
	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename = FilePath;
	Task->DestinationPath = PackagePath;
	Task->DestinationName = AssetName;
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = false;
	Task->Factory = Factory;
	FAssetToolsModule::GetModule().Get().ImportAssetTasks({ Task });

	UTexture2D* Texture = nullptr;
	for (UObject* Object : Task->GetObjects())
	{
		Texture = Cast<UTexture2D>(Object);
		if (Texture) break;
	}
	if (!Texture)
	{
		Texture = VmhAssets::Load<UTexture2D>(PackagePath / AssetName);
	}
	if (!Texture)
	{
		return VmhJson::Error(TEXT("IMPORT_FAILED"), FString::Printf(TEXT("no Texture2D came out of %s (see the Output Log)"), *FilePath));
	}
	Texture->PreEditChange(nullptr);
	Texture->SRGB = bColor;
	Texture->CompressionSettings = bNormal ? TC_Normalmap : bMask ? TC_Masks : TC_Default;
	Texture->LODGroup = bNormal ? TEXTUREGROUP_WorldNormalMap : TEXTUREGROUP_World;
	if (bNormal && bFlipGreen && !Factory->bFlipNormalMapGreenChannel)
	{
		Texture->bFlipGreenChannel = true;
	}
	Texture->PostEditChange();
	Texture->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("texture"), Texture->GetPathName());
	Out->SetNumberField(TEXT("width"), Texture->GetSizeX());
	Out->SetNumberField(TEXT("height"), Texture->GetSizeY());
	Out->SetBoolField(TEXT("srgb"), Texture->SRGB);
	Out->SetStringField(TEXT("compression"), StaticEnum<TextureCompressionSettings>()->GetNameStringByValue(static_cast<int64>(Texture->CompressionSettings)));
	Out->SetBoolField(TEXT("flip_green"), bNormal && bFlipGreen);
	return VmhJson::Ok(Out);
}

FString UMetaHumanCharacterService::SetMaterialTexture(const FString& MaterialInstancePath, const FString& ParameterName, const FString& TexturePath)
{
	UMaterialInstanceConstant* Instance = VmhAssets::Load<UMaterialInstanceConstant>(MaterialInstancePath);
	if (!Instance)
	{
		return VmhJson::Error(TEXT("MATERIAL_NOT_FOUND"), FString::Printf(TEXT("No material instance constant at %s"), *MaterialInstancePath));
	}
	UTexture* Texture = VmhAssets::Load<UTexture>(TexturePath);
	if (!Texture)
	{
		return VmhJson::Error(TEXT("TEXTURE_NOT_FOUND"), FString::Printf(TEXT("No texture at %s"), *TexturePath));
	}
	TArray<FMaterialParameterInfo> Infos;
	TArray<FGuid> Ids;
	Instance->GetAllTextureParameterInfo(Infos, Ids);
	TArray<FString> Names;
	bool bKnown = false;
	for (const FMaterialParameterInfo& Info : Infos)
	{
		Names.Add(Info.Name.ToString());
		bKnown |= Info.Name == FName(*ParameterName);
	}
	if (!bKnown)
	{
		return VmhJson::Error(TEXT("NO_SUCH_PARAMETER"), FString::Printf(TEXT("%s has no texture parameter %s; it has: %s"), *MaterialInstancePath, *ParameterName, *FString::Join(Names, TEXT(", "))));
	}
	// a texture imported a moment ago is still compiling, and the editor library refuses it (2026-10-04)
	FTextureCompilingManager::Get().FinishCompilation({ Texture });
	const FName ParamName(*ParameterName);
	const FMaterialParameterInfo Info{ ParamName };
	const bool bSet = UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, ParamName, Texture);
	if (!bSet)
	{
		Instance->SetTextureParameterValueEditorOnly(Info, Texture);
	}
	UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
	Instance->MarkPackageDirty();
	UTexture* ReadBack = nullptr;
	Instance->GetTextureParameterValue(FHashedMaterialParameterInfo(Info), ReadBack);
	const bool bApplied = ReadBack == Texture;
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("material_instance"), Instance->GetPathName());
	Out->SetStringField(TEXT("parameter"), ParameterName);
	Out->SetStringField(TEXT("texture"), Texture->GetPathName());
	Out->SetBoolField(TEXT("set"), bApplied);
	Out->SetStringField(TEXT("reads_back"), ReadBack ? ReadBack->GetPathName() : TEXT(""));
	return bApplied ? VmhJson::Ok(Out) : VmhJson::Error(TEXT("SET_FAILED"), TEXT("the parameter does not read back as the texture after setting it"), Out);
}

FString UMetaHumanCharacterService::AttachSkeletalMeshToBlueprint(const FString& BlueprintPath, const FString& ComponentName, const FString& SkeletalMeshPath, const FString& ParentComponentName, bool bLeaderPose)
{
	UBlueprint* Blueprint = VmhAssets::Load<UBlueprint>(BlueprintPath);
	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		return VmhJson::Error(TEXT("BLUEPRINT_NOT_FOUND"), FString::Printf(TEXT("No Blueprint with a construction script at %s"), *BlueprintPath));
	}
	USkeletalMesh* Mesh = VmhAssets::Load<USkeletalMesh>(SkeletalMeshPath);
	if (!Mesh)
	{
		return VmhJson::Error(TEXT("MESH_NOT_FOUND"), FString::Printf(TEXT("No skeletal mesh at %s"), *SkeletalMeshPath));
	}
	if (ComponentName.IsEmpty())
	{
		return VmhJson::Error(TEXT("BAD_ARGS"), TEXT("ComponentName is required"));
	}
	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	USCS_Node* Parent = nullptr;
	if (!ParentComponentName.IsEmpty())
	{
		Parent = SCS->FindSCSNode(FName(*ParentComponentName));
		if (!Parent)
		{
			TArray<FString> Names;
			for (USCS_Node* Node : SCS->GetAllNodes()) { Names.Add(Node->GetVariableName().ToString()); }
			return VmhJson::Error(TEXT("PARENT_NOT_FOUND"), FString::Printf(TEXT("%s has no component %s; it has: %s"), *BlueprintPath, *ParentComponentName, *FString::Join(Names, TEXT(", "))));
		}
	}
	if (SCS->FindSCSNode(FName(*ComponentName)))
	{
		return VmhJson::Error(TEXT("NAME_TAKEN"), FString::Printf(TEXT("%s already has a component named %s"), *BlueprintPath, *ComponentName));
	}
	USCS_Node* Node = SCS->CreateNode(USkeletalMeshComponent::StaticClass(), FName(*ComponentName));
	USkeletalMeshComponent* Template = Cast<USkeletalMeshComponent>(Node->ComponentTemplate);
	if (!Template)
	{
		return VmhJson::Error(TEXT("NODE_FAILED"), TEXT("the construction script made no SkeletalMeshComponent template"));
	}
	Template->SetSkeletalMeshAsset(Mesh);
	bool bLeader = false;
	if (Parent)
	{
		Parent->AddChildNode(Node);
		if (bLeaderPose)
		{
			if (USkinnedMeshComponent* ParentTemplate = Cast<USkinnedMeshComponent>(Parent->ComponentTemplate))
			{
				Template->LeaderPoseComponent = ParentTemplate;
				bLeader = true;
			}
		}
	}
	else
	{
		SCS->AddNode(Node);
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	Blueprint->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("blueprint"), Blueprint->GetPathName());
	Out->SetStringField(TEXT("component"), ComponentName);
	Out->SetStringField(TEXT("parent"), Parent ? ParentComponentName : TEXT(""));
	Out->SetStringField(TEXT("skeletal_mesh"), Mesh->GetPathName());
	Out->SetBoolField(TEXT("leader_pose"), bLeader);
	Out->SetBoolField(TEXT("compiled"), Blueprint->Status != BS_Error);
	Out->SetStringField(TEXT("skeleton"), Mesh->GetSkeleton() ? Mesh->GetSkeleton()->GetPathName() : TEXT(""));
	Out->SetStringField(TEXT("hint"), TEXT("the mesh follows the parent only when it shares the parent's skeleton (import the accessory FBX onto the MetaHuman body skeleton); save the Blueprint's package when it should stay"));
	return VmhJson::Ok(Out);
}
