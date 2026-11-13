// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UMetaHumanAnimatorService.generated.h"

/**
 * MetaHuman Animator Service - offline facial performance capture: footage or audio in,
 * animation out.
 *
 * Three routes, matching the input types of a MetaHuman Performance asset:
 *  - MonoFootage: a webcam/DSLR video file (CaptureManagerIngestTools.IngestMonoVideo) - no identity needed.
 *  - DepthFootage: an iPhone Live Link Face take recorded in MetaHuman Animator mode
 *    (CaptureManagerIngestTools.IngestLiveLinkFace) plus a prepared MetaHuman Identity of the performer.
 *  - Audio: a SoundWave asset - no footage, no identity.
 * Footage ingest, processing (MetaHumanAnimatorPerformanceTools.ProcessPerformance, async) and
 * animation export (MetaHumanAnimatorExportTools.ExportPerformanceAnimationSequence) are Epic's own
 * AI tools, as are cancelling and the Level Sequence export; this service creates and configures the
 * Performance and reports status.
 *
 * Python Usage:
 *   svc = unreal.MetaHumanAnimatorService
 *   # cd = (await call_tool("CaptureManagerToolset.CaptureManagerIngestTools.IngestMonoVideo", videoFilePath=..., audioFilePath="", slate="line01", takeNumber=1))["captureDataAssetPath"]
 *   p  = json.loads(svc.create_performance("/Game/MHA", "Perf_line01", "MonoFootage", cd, "", ""))["performance"]
 *   # await call_tool("MetaHumanAnimatorToolset.MetaHumanAnimatorPerformanceTools.ProcessPerformance", performance=p, params={})
 *   # audio only:
 *   p = json.loads(svc.create_performance("/Game/MHA", "Perf_vo01", "Audio", "", "", "/Game/Audio/vo01"))["performance"]
 *   # mood / output controls / head movement: EditorToolset.ObjectToolset.SetProperties on the Performance
 */
UCLASS(BlueprintType)
class VIBEMETAHUMANS_API UMetaHumanAnimatorService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	// =================================================================
	// Performance
	// =================================================================

	/**
	 * Create a MetaHuman Performance asset. InputType: MonoFootage | DepthFootage | Audio.
	 * CaptureDataPath for footage routes; IdentityPath required for DepthFootage; AudioPath (SoundWave) for Audio.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString CreatePerformance(const FString& PackagePath, const FString& AssetName, const FString& InputType, const FString& CaptureDataPath, const FString& IdentityPath, const FString& AudioPath);

	/**
	 * Patch any performance property (JSON by property name): HeadMovementMode, SolveType, bSkipDiagnostics,
	 * bHeadStabilization, StartFrameToProcess/EndFrameToProcess, FocalLength, UserExcludedFrames, ...
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString ConfigurePerformance(const FString& PerformancePath, const FString& JsonPatch);


	/** Status JSON: input type, inputs, can_process, is_processing, processed_frames, can_export, diagnostics. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString GetStatus(const FString& PerformancePath);

};
