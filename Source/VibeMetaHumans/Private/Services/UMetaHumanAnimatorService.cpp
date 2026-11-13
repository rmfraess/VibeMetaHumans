// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Services/UMetaHumanAnimatorService.h"

#include "Animation/AnimSequence.h"
#include "CaptureData.h"
#include "LevelSequence.h"
#include "MetaHumanIdentity.h"
#include "MetaHumanPerformance.h"
#include "MetaHumanPerformanceExportUtils.h"
#include "Misc/Paths.h"
#include "Sound/SoundWave.h"
#include "Utils/VmhAssets.h"
#include "Utils/VmhJson.h"
#include "VibeMetaHumansModule.h"

namespace
{
	const TCHAR* PerformanceFactoryPath = TEXT("/Script/MetaHumanPerformance.MetaHumanPerformanceFactoryNew");

	UMetaHumanPerformance* LoadPerformance(const FString& Path, FString& OutError)
	{
		UObject* Object = VmhAssets::Load(Path);
		UMetaHumanPerformance* Performance = Cast<UMetaHumanPerformance>(Object);
		if (!Performance)
		{
			OutError = Object
				? FString::Printf(TEXT("%s is a %s, not a MetaHumanPerformance"), *Path, *Object->GetClass()->GetName())
				: FString::Printf(TEXT("No MetaHuman Performance at %s"), *Path);
		}
		return Performance;
	}

	FString EnumValueName(const UEnum* Enum, int64 Value)
	{
		return Enum ? Enum->GetNameStringByValue(Value) : FString::FromInt(static_cast<int32>(Value));
	}

	TSharedPtr<FJsonObject> StatusJson(UMetaHumanPerformance* Performance)
	{
		TSharedPtr<FJsonObject> Out = VmhJson::Obj();
		Out->SetStringField(TEXT("performance"), Performance->GetPathName());
		static const TArray<FString> Props = {
			TEXT("InputType"), TEXT("FootageCaptureData"), TEXT("Identity"), TEXT("Audio"), TEXT("Camera"),
			TEXT("HeadMovementMode"), TEXT("SolveType"), TEXT("StartFrameToProcess"), TEXT("EndFrameToProcess"),
			TEXT("bSkipDiagnostics"), TEXT("bHeadStabilization"), TEXT("AudioDrivenAnimationOutputControls"),
			TEXT("AudioDrivenAnimationSolveOverrides"), TEXT("bBodyTracking"),
		};
		Out->SetObjectField(TEXT("settings"), VmhJson::ObjectToJson(Performance, Props));
		Out->SetBoolField(TEXT("can_process"), Performance->CanProcess());
		Out->SetBoolField(TEXT("is_processing"), Performance->IsProcessing());
		Out->SetNumberField(TEXT("processed_frames"), Performance->GetNumberOfProcessedFrames());
		Out->SetBoolField(TEXT("can_export_animation"), Performance->CanExportAnimation());
		// Reflection call: the diagnostics UFUNCTION is not part of the module's exported API.
		TSharedPtr<FJsonObject> Diag;
		FString DiagError;
		if (VmhJson::CallObjectFunction(Performance, TEXT("DiagnosticsIndicatesProcessingIssue"), VmhJson::Obj(), Diag, DiagError))
		{
			bool bIssue = false;
			FString Message;
			if (Diag->TryGetBoolField(TEXT("ReturnValue"), bIssue) && bIssue && Diag->TryGetStringField(TEXT("OutDiagnosticsWarningMessage"), Message))
			{
				Out->SetStringField(TEXT("diagnostics_warning"), Message);
			}
		}
		return Out;
	}
}

// =====================================================================
// Performance
// =====================================================================

FString UMetaHumanAnimatorService::CreatePerformance(const FString& PackagePath, const FString& AssetName, const FString& InputType, const FString& CaptureDataPath, const FString& IdentityPath, const FString& AudioPath)
{
	const UEnum* InputEnum = StaticEnum<EDataInputType>();
	const int64 InputValue = VmhJson::ParseEnum(InputEnum, InputType);
	if (InputValue == INDEX_NONE)
	{
		return VmhJson::Error(TEXT("BAD_INPUT_TYPE"), FString::Printf(TEXT("InputType must be one of: %s"), *VmhJson::EnumNames(InputEnum)));
	}
	const EDataInputType Type = static_cast<EDataInputType>(InputValue);

	UFootageCaptureData* CaptureData = nullptr;
	UMetaHumanIdentity* Identity = nullptr;
	USoundWave* Audio = nullptr;
	if (Type == EDataInputType::Audio)
	{
		Audio = VmhAssets::Load<USoundWave>(AudioPath);
		if (!Audio)
		{
			return VmhJson::Error(TEXT("AUDIO_NOT_FOUND"), FString::Printf(TEXT("No SoundWave at %s"), *AudioPath));
		}
	}
	else
	{
		CaptureData = VmhAssets::Load<UFootageCaptureData>(CaptureDataPath);
		if (!CaptureData)
		{
			return VmhJson::Error(TEXT("CAPTURE_DATA_NOT_FOUND"), FString::Printf(TEXT("No Footage Capture Data at %s"), *CaptureDataPath));
		}
		if (Type == EDataInputType::DepthFootage)
		{
			Identity = VmhAssets::Load<UMetaHumanIdentity>(IdentityPath);
			if (!Identity)
			{
				return VmhJson::Error(TEXT("IDENTITY_NOT_FOUND"), TEXT("DepthFootage needs a prepared MetaHuman Identity (IdentityPath)"));
			}
			if (CaptureData->DepthSequences.Num() == 0)
			{
				return VmhJson::Error(TEXT("NO_DEPTH"), TEXT("The capture data has no depth sequence; use InputType MonoFootage for plain video"));
			}
		}
	}

	FString Error;
	UObject* Created = VmhAssets::Create(PackagePath, AssetName, UMetaHumanPerformance::StaticClass(), PerformanceFactoryPath, Error);
	UMetaHumanPerformance* Performance = Cast<UMetaHumanPerformance>(Created);
	if (!Performance)
	{
		return VmhJson::Error(TEXT("CREATE_FAILED"), Error.IsEmpty() ? TEXT("Factory returned no MetaHumanPerformance") : Error);
	}

	// Setters trigger the PostEditChange handling the asset relies on.
	Performance->SetInputType(Type);
	if (CaptureData)
	{
		Performance->SetFootageCaptureData(CaptureData);
	}
	if (Identity)
	{
		Performance->SetIdentity(Identity);
	}
	if (Audio)
	{
		Performance->SetAudio(Audio);
	}

	FString SaveError;
	if (!VmhAssets::Save(Performance, SaveError))
	{
		UE_LOG(LogVibeMetaHumans, Warning, TEXT("%s"), *SaveError);
	}
	TSharedPtr<FJsonObject> Out = StatusJson(Performance);
	Out->SetStringField(TEXT("input_type"), EnumValueName(InputEnum, InputValue));
	return VmhJson::Ok(Out);
}

FString UMetaHumanAnimatorService::ConfigurePerformance(const FString& PerformancePath, const FString& JsonPatch)
{
	FString Error;
	UMetaHumanPerformance* Performance = LoadPerformance(PerformancePath, Error);
	if (!Performance)
	{
		return VmhJson::Error(TEXT("PERFORMANCE_NOT_FOUND"), Error);
	}
	FString ParseError;
	TSharedPtr<FJsonObject> Patch = VmhJson::Parse(JsonPatch, ParseError);
	if (!Patch.IsValid())
	{
		return VmhJson::Error(TEXT("INVALID_JSON"), ParseError);
	}
	TArray<FString> Changed;
	if (!VmhJson::PatchObject(Performance, Patch, Changed, Error))
	{
		return VmhJson::Error(TEXT("PATCH_FAILED"), Error);
	}
	TSharedPtr<FJsonObject> Out = StatusJson(Performance);
	Out->SetArrayField(TEXT("changed"), VmhJson::StringArray(Changed));
	return VmhJson::Ok(Out);
}

FString UMetaHumanAnimatorService::GetStatus(const FString& PerformancePath)
{
	FString Error;
	UMetaHumanPerformance* Performance = LoadPerformance(PerformancePath, Error);
	if (!Performance)
	{
		return VmhJson::Error(TEXT("PERFORMANCE_NOT_FOUND"), Error);
	}
	return VmhJson::Ok(StatusJson(Performance));
}

