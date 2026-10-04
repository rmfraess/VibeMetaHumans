// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Services/UMetaHumanCharacterService.h"

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
