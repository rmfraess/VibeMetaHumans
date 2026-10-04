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
	const TCHAR* IngestLibraryName = TEXT("CaptureManagerIngestBlueprintLibrary");

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

	/** Call one of the Capture Manager ingest functions by reflection (its header is private). */
	FString RunIngest(const TCHAR* FunctionName, const TSharedPtr<FJsonObject>& Args, const TCHAR* IngestLabel)
	{
		UClass* Library = VmhJson::FindClass(IngestLibraryName);
		if (!Library)
		{
			return VmhJson::Error(TEXT("NO_CAPTURE_MANAGER"), TEXT("CaptureManagerIngestBlueprintLibrary not found; enable the CaptureManagerEditor plugin"));
		}
		if (!Args->HasField(TEXT("Params")))
		{
			Args->SetObjectField(TEXT("Params"), VmhJson::Obj());
		}
		TSharedPtr<FJsonObject> Result;
		FString Error;
		if (!VmhJson::CallStaticFunction(Library, FunctionName, Args, Result, Error))
		{
			return VmhJson::Error(TEXT("INGEST_CALL_FAILED"), Error);
		}
		FString CaptureDataPath;
		Result->TryGetStringField(TEXT("ReturnValue"), CaptureDataPath);
		FString ErrorMessage;
		Result->TryGetStringField(TEXT("OutErrorMessage"), ErrorMessage);
		if (CaptureDataPath.IsEmpty() || CaptureDataPath == TEXT("None"))
		{
			return VmhJson::Error(TEXT("INGEST_FAILED"), FString::Printf(TEXT("%s failed: %s"), IngestLabel, ErrorMessage.IsEmpty() ? TEXT("no capture data returned") : *ErrorMessage));
		}
		TSharedPtr<FJsonObject> Out = VmhJson::Obj();
		Out->SetStringField(TEXT("capture_data"), CaptureDataPath);
		if (UFootageCaptureData* Footage = VmhAssets::Load<UFootageCaptureData>(CaptureDataPath))
		{
			Out->SetNumberField(TEXT("image_sequences"), Footage->ImageSequences.Num());
			Out->SetNumberField(TEXT("depth_sequences"), Footage->DepthSequences.Num());
			Out->SetNumberField(TEXT("audio_tracks"), Footage->AudioTracks.Num());
			Out->SetBoolField(TEXT("has_depth"), Footage->DepthSequences.Num() > 0);
		}
		return VmhJson::Ok(Out);
	}
}

// =====================================================================
// Ingest
// =====================================================================

FString UMetaHumanAnimatorService::FindTakeDirectories(const FString& SearchDirectory, bool bRecursive)
{
	if (!FPaths::DirectoryExists(SearchDirectory))
	{
		return VmhJson::Error(TEXT("DIR_NOT_FOUND"), FString::Printf(TEXT("No directory at %s"), *SearchDirectory));
	}
	UClass* Library = VmhJson::FindClass(IngestLibraryName);
	if (!Library)
	{
		return VmhJson::Error(TEXT("NO_CAPTURE_MANAGER"), TEXT("CaptureManagerIngestBlueprintLibrary not found; enable the CaptureManagerEditor plugin"));
	}
	TSharedPtr<FJsonObject> Args = VmhJson::Obj();
	Args->SetStringField(TEXT("SearchDirectory"), SearchDirectory);
	Args->SetBoolField(TEXT("bRecursive"), bRecursive);
	TSharedPtr<FJsonObject> Result;
	FString Error;
	if (!VmhJson::CallStaticFunction(Library, TEXT("FindTakeDirectories"), Args, Result, Error))
	{
		return VmhJson::Error(TEXT("CALL_FAILED"), Error);
	}
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("search_directory"), SearchDirectory);
	const TArray<TSharedPtr<FJsonValue>>* Takes = nullptr;
	if (Result->TryGetArrayField(TEXT("ReturnValue"), Takes) && Takes)
	{
		Out->SetNumberField(TEXT("count"), Takes->Num());
		Out->SetArrayField(TEXT("takes"), *Takes);
	}
	else
	{
		Out->SetNumberField(TEXT("count"), 0);
	}
	return VmhJson::Ok(Out);
}

FString UMetaHumanAnimatorService::IngestMonoVideo(const FString& VideoFilePath, const FString& AudioFilePath, const FString& Slate, int32 TakeNumber)
{
	if (!FPaths::FileExists(VideoFilePath))
	{
		return VmhJson::Error(TEXT("FILE_NOT_FOUND"), FString::Printf(TEXT("No video at %s"), *VideoFilePath));
	}
	TSharedPtr<FJsonObject> Args = VmhJson::Obj();
	Args->SetStringField(TEXT("VideoFilePath"), VideoFilePath);
	Args->SetStringField(TEXT("AudioFilePath"), AudioFilePath);
	Args->SetStringField(TEXT("Slate"), Slate.IsEmpty() ? FPaths::GetBaseFilename(VideoFilePath) : Slate);
	Args->SetNumberField(TEXT("TakeNumber"), FMath::Max(1, TakeNumber));
	return RunIngest(TEXT("IngestMonoVideoSync"), Args, TEXT("Mono video ingest"));
}

FString UMetaHumanAnimatorService::IngestLiveLinkFaceTake(const FString& TakeDirectoryPath)
{
	if (!FPaths::DirectoryExists(TakeDirectoryPath))
	{
		return VmhJson::Error(TEXT("DIR_NOT_FOUND"), FString::Printf(TEXT("No take directory at %s"), *TakeDirectoryPath));
	}
	TSharedPtr<FJsonObject> Args = VmhJson::Obj();
	Args->SetStringField(TEXT("TakeDirectoryPath"), TakeDirectoryPath);
	return RunIngest(TEXT("IngestLiveLinkFaceSync"), Args, TEXT("Live Link Face take ingest"));
}

FString UMetaHumanAnimatorService::IngestTakeArchive(const FString& TakeArchivePath)
{
	if (!FPaths::FileExists(TakeArchivePath))
	{
		return VmhJson::Error(TEXT("FILE_NOT_FOUND"), FString::Printf(TEXT("No archive at %s"), *TakeArchivePath));
	}
	TSharedPtr<FJsonObject> Args = VmhJson::Obj();
	Args->SetStringField(TEXT("TakeArchivePath"), TakeArchivePath);
	return RunIngest(TEXT("IngestTakeArchiveSync"), Args, TEXT("Take archive ingest"));
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

FString UMetaHumanAnimatorService::SetAudioMood(const FString& PerformancePath, const FString& Mood, float Intensity, const FString& OutputControls, const FString& HeadMovementMode)
{
	TSharedPtr<FJsonObject> Patch = VmhJson::Obj();
	if (!Mood.IsEmpty() || Intensity >= 0.f)
	{
		TSharedPtr<FJsonObject> Overrides = VmhJson::Obj();
		if (!Mood.IsEmpty())
		{
			Overrides->SetStringField(TEXT("Mood"), Mood);
		}
		if (Intensity >= 0.f)
		{
			Overrides->SetNumberField(TEXT("MoodIntensity"), FMath::Clamp(Intensity, 0.f, 1.f));
		}
		Patch->SetObjectField(TEXT("AudioDrivenAnimationSolveOverrides"), Overrides);
	}
	if (!OutputControls.IsEmpty())
	{
		Patch->SetStringField(TEXT("AudioDrivenAnimationOutputControls"), OutputControls);
	}
	if (!HeadMovementMode.IsEmpty())
	{
		Patch->SetStringField(TEXT("HeadMovementMode"), HeadMovementMode);
	}
	if (Patch->Values.Num() == 0)
	{
		return VmhJson::Error(TEXT("NO_VALUES"), TEXT("Pass a mood, intensity, output controls or head movement mode"));
	}
	return ConfigurePerformance(PerformancePath, VmhJson::Stringify(Patch));
}

FString UMetaHumanAnimatorService::Process(const FString& PerformancePath, int32 StartFrame, int32 EndFrame)
{
	FString Error;
	UMetaHumanPerformance* Performance = LoadPerformance(PerformancePath, Error);
	if (!Performance)
	{
		return VmhJson::Error(TEXT("PERFORMANCE_NOT_FOUND"), Error);
	}
	if (Performance->IsProcessing())
	{
		return VmhJson::Error(TEXT("ALREADY_PROCESSING"), TEXT("This performance is already processing"));
	}
	if (!Performance->CanProcess())
	{
		TSharedPtr<FJsonObject> Status = StatusJson(Performance);
		return VmhJson::Error(TEXT("CANNOT_PROCESS"),
			TEXT("CanProcess() is false: check the inputs (footage/identity/audio), that the identity is prepared for performance (depth route), "
			     "and that the NNE ORT runtime + a D3D12/Vulkan RHI are available for footage routes."), Status);
	}
	if (StartFrame >= 0 && EndFrame > StartFrame)
	{
		Performance->SetProcessingRange(StartFrame, EndFrame);
	}

	Performance->SetBlockingProcessing(true);
	const double Start = FPlatformTime::Seconds();
	const EStartPipelineErrorType Result = Performance->StartPipeline(/*bIsScriptedProcessing*/ true);
	const double Seconds = FPlatformTime::Seconds() - Start;

	TSharedPtr<FJsonObject> Out = StatusJson(Performance);
	Out->SetStringField(TEXT("start_pipeline_result"), EnumValueName(StaticEnum<EStartPipelineErrorType>(), static_cast<int64>(Result)));
	Out->SetNumberField(TEXT("seconds"), Seconds);
	if (Result != EStartPipelineErrorType::None)
	{
		return VmhJson::Error(TEXT("PIPELINE_FAILED"), FString::Printf(TEXT("StartPipeline returned %s"), *EnumValueName(StaticEnum<EStartPipelineErrorType>(), static_cast<int64>(Result))), Out);
	}
	FString SaveError;
	VmhAssets::Save(Performance, SaveError);
	return VmhJson::Ok(Out);
}

FString UMetaHumanAnimatorService::CancelProcessing(const FString& PerformancePath)
{
	FString Error;
	UMetaHumanPerformance* Performance = LoadPerformance(PerformancePath, Error);
	if (!Performance)
	{
		return VmhJson::Error(TEXT("PERFORMANCE_NOT_FOUND"), Error);
	}
	const bool bWas = Performance->IsProcessing();
	if (bWas)
	{
		TSharedPtr<FJsonObject> Result;
		FString CallError;
		if (!VmhJson::CallObjectFunction(Performance, TEXT("CancelPipeline"), VmhJson::Obj(), Result, CallError))
		{
			return VmhJson::Error(TEXT("CANCEL_FAILED"), CallError);
		}
	}
	TSharedPtr<FJsonObject> Out = StatusJson(Performance);
	Out->SetBoolField(TEXT("was_processing"), bWas);
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

// =====================================================================
// Export
// =====================================================================

FString UMetaHumanAnimatorService::ExportAnimSequence(const FString& PerformancePath, const FString& PackagePath, const FString& AssetName, const FString& TargetSkeletonPath, bool bEnableHeadMovement, bool bProcessingRangeOnly)
{
	FString Error;
	UMetaHumanPerformance* Performance = LoadPerformance(PerformancePath, Error);
	if (!Performance)
	{
		return VmhJson::Error(TEXT("PERFORMANCE_NOT_FOUND"), Error);
	}
	if (!Performance->CanExportAnimation())
	{
		return VmhJson::Error(TEXT("NOTHING_TO_EXPORT"), TEXT("The performance has no processed animation; call process first"));
	}
	UMetaHumanPerformanceExportAnimationSettings* Settings = UMetaHumanPerformanceExportUtils::GetExportAnimationSequenceSettings(Performance);
	if (!Settings)
	{
		return VmhJson::Error(TEXT("NO_SETTINGS"), TEXT("Could not create export settings"));
	}
	TSharedPtr<FJsonObject> Patch = VmhJson::Obj();
	Patch->SetBoolField(TEXT("bShowExportDialog"), false);
	if (!PackagePath.IsEmpty())
	{
		Patch->SetStringField(TEXT("PackagePath"), PackagePath);
	}
	if (!AssetName.IsEmpty())
	{
		Patch->SetStringField(TEXT("AssetName"), AssetName);
	}
	if (!TargetSkeletonPath.IsEmpty())
	{
		UObject* Target = VmhAssets::Load(TargetSkeletonPath);
		if (!Target)
		{
			return VmhJson::Error(TEXT("SKELETON_NOT_FOUND"), FString::Printf(TEXT("No skeleton / skeletal mesh at %s"), *TargetSkeletonPath));
		}
		Patch->SetStringField(TEXT("ExportSkeleton"), TEXT("ExistingSkeleton"));
		Patch->SetStringField(TEXT("TargetSkeletonOrSkeletalMesh"), Target->GetPathName());
	}
	Patch->SetBoolField(TEXT("bEnableHeadMovement"), bEnableHeadMovement);
	Patch->SetStringField(TEXT("ExportRange"), bProcessingRangeOnly ? TEXT("ProcessingRange") : TEXT("WholeSequence"));
	TArray<FString> Changed;
	if (!VmhJson::PatchObject(Settings, Patch, Changed, Error))
	{
		return VmhJson::Error(TEXT("SETTINGS_FAILED"), Error);
	}

	UAnimSequence* Anim = UMetaHumanPerformanceExportUtils::ExportAnimationSequence(Performance, Settings);
	if (!Anim)
	{
		return VmhJson::Error(TEXT("EXPORT_FAILED"), TEXT("ExportAnimationSequence returned null (see Output Log; a target skeleton missing the face curves is the usual cause)"));
	}
	FString SaveError;
	VmhAssets::Save(Anim, SaveError);
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("performance"), Performance->GetPathName());
	Out->SetStringField(TEXT("anim_sequence"), Anim->GetPathName());
	Out->SetNumberField(TEXT("frames"), Anim->GetNumberOfSampledKeys());
	Out->SetNumberField(TEXT("seconds"), Anim->GetPlayLength());
	return VmhJson::Ok(Out);
}

FString UMetaHumanAnimatorService::ExportLevelSequence(const FString& PerformancePath, const FString& PackagePath, const FString& AssetName, const FString& TargetMetaHumanBlueprintPath, bool bExportIdentity, bool bExportCamera, bool bExportAudio, bool bExportVideo)
{
	FString Error;
	UMetaHumanPerformance* Performance = LoadPerformance(PerformancePath, Error);
	if (!Performance)
	{
		return VmhJson::Error(TEXT("PERFORMANCE_NOT_FOUND"), Error);
	}
	if (!Performance->CanExportAnimation())
	{
		return VmhJson::Error(TEXT("NOTHING_TO_EXPORT"), TEXT("The performance has no processed animation; call process first"));
	}
	UMetaHumanPerformanceExportLevelSequenceSettings* Settings = UMetaHumanPerformanceExportUtils::GetExportLevelSequenceSettings(Performance);
	if (!Settings)
	{
		return VmhJson::Error(TEXT("NO_SETTINGS"), TEXT("Could not create export settings"));
	}
	TSharedPtr<FJsonObject> Patch = VmhJson::Obj();
	Patch->SetBoolField(TEXT("bShowExportDialog"), false);
	if (!PackagePath.IsEmpty())
	{
		Patch->SetStringField(TEXT("PackagePath"), PackagePath);
	}
	if (!AssetName.IsEmpty())
	{
		Patch->SetStringField(TEXT("AssetName"), AssetName);
	}
	if (!TargetMetaHumanBlueprintPath.IsEmpty())
	{
		UObject* Target = VmhAssets::Load(TargetMetaHumanBlueprintPath);
		if (!Target)
		{
			return VmhJson::Error(TEXT("BLUEPRINT_NOT_FOUND"), FString::Printf(TEXT("No Blueprint at %s"), *TargetMetaHumanBlueprintPath));
		}
		Patch->SetStringField(TEXT("TargetMetaHumanClass"), Target->GetPathName());
		Patch->SetBoolField(TEXT("bEnableMetaHumanHeadMovement"), true);
	}
	Patch->SetBoolField(TEXT("bExportIdentity"), bExportIdentity);
	Patch->SetBoolField(TEXT("bExportCamera"), bExportCamera);
	Patch->SetBoolField(TEXT("bExportAudioTrack"), bExportAudio);
	Patch->SetBoolField(TEXT("bExportVideoTrack"), bExportVideo);
	Patch->SetBoolField(TEXT("bExportControlRigTrack"), true);
	Patch->SetBoolField(TEXT("bKeepFrameRange"), true);
	TArray<FString> Changed;
	if (!VmhJson::PatchObject(Settings, Patch, Changed, Error))
	{
		return VmhJson::Error(TEXT("SETTINGS_FAILED"), Error);
	}

	ULevelSequence* Sequence = UMetaHumanPerformanceExportUtils::ExportLevelSequence(Performance, Settings);
	if (!Sequence)
	{
		return VmhJson::Error(TEXT("EXPORT_FAILED"), TEXT("ExportLevelSequence returned null (see Output Log)"));
	}
	FString SaveError;
	VmhAssets::Save(Sequence, SaveError);
	TSharedPtr<FJsonObject> Out = VmhJson::Obj();
	Out->SetStringField(TEXT("performance"), Performance->GetPathName());
	Out->SetStringField(TEXT("level_sequence"), Sequence->GetPathName());
	Out->SetArrayField(TEXT("settings_changed"), VmhJson::StringArray(Changed));
	return VmhJson::Ok(Out);
}
