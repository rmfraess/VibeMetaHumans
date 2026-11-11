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
 * Processing needs the NNE ORT runtimes (GPU for footage); it runs blocking on the game thread
 * here so one call = one processed take. Export to an Animation Sequence (plays on any MetaHuman
 * face via the Face_Archetype skeleton) or a Level Sequence that targets a MetaHuman Blueprint.
 *
 * Python Usage:
 *   svc = unreal.MetaHumanAnimatorService
 *   # Ingest with CaptureManagerToolset.CaptureManagerIngestTools.IngestMonoVideo; await result to obtain cd.
 *   p  = json.loads(svc.create_performance("/Game/MHA", "Perf_line01", "MonoFootage", cd, "", ""))["performance"]
 *   # Process with MetaHumanAnimatorToolset.MetaHumanAnimatorPerformanceTools.ProcessPerformance.
 *   # Export with unreal.MetaHumanAnimatorExportTools.export_performance_animation_sequence.
 *   # audio only:
 *   p = json.loads(svc.create_performance("/Game/MHA", "Perf_vo01", "Audio", "", "", "/Game/Audio/vo01"))["performance"]
 *   svc.set_audio_mood(p, "Happiness", 0.6, "FullFace", "ControlRig")  # then ProcessPerformance as above.
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

	/**
	 * Audio-driven options. Mood: AutoDetect | Neutral | Happiness | Sadness | Disgust | Anger | Surprise | Fear | ...
	 * Intensity 0..1. OutputControls: FullFace | MouthOnly. HeadMovementMode: ControlRig | TransformTrack | Disabled.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString SetAudioMood(const FString& PerformancePath, const FString& Mood, float Intensity, const FString& OutputControls, const FString& HeadMovementMode);

	/** Status JSON: input type, inputs, can_process, is_processing, processed_frames, can_export, diagnostics. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString GetStatus(const FString& PerformancePath);

};
