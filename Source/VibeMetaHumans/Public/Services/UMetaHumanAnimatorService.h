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
 *  - MonoFootage: a webcam/DSLR video file (ingest_mono_video) - no identity needed.
 *  - DepthFootage: an iPhone Live Link Face take recorded in MetaHuman Animator mode
 *    (ingest_live_link_face_take) plus a prepared MetaHuman Identity of the performer.
 *  - Audio: a SoundWave asset - no footage, no identity.
 * Processing needs the NNE ORT runtimes (GPU for footage); it runs blocking on the game thread
 * here so one call = one processed take. Export to an Animation Sequence (plays on any MetaHuman
 * face via the Face_Archetype skeleton) or a Level Sequence that targets a MetaHuman Blueprint.
 *
 * Python Usage:
 *   svc = unreal.MetaHumanAnimatorService
 *   cd = json.loads(svc.ingest_mono_video("D:/Takes/line01.mp4", "", "line01", 1))["capture_data"]
 *   p  = json.loads(svc.create_performance("/Game/MHA", "Perf_line01", "MonoFootage", cd, "", ""))["performance"]
 *   svc.process(p, -1, -1)
 *   svc.export_anim_sequence(p, "/Game/MHA/Anims", "AS_line01", "", True, True)
 *   # audio only:
 *   p = json.loads(svc.create_performance("/Game/MHA", "Perf_vo01", "Audio", "", "", "/Game/Audio/vo01"))["performance"]
 *   svc.set_audio_mood(p, "Happiness", 0.6, "FullFace", "ControlRig"); svc.process(p, -1, -1)
 */
UCLASS(BlueprintType)
class VIBEMETAHUMANS_API UMetaHumanAnimatorService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	// =================================================================
	// Ingest (Capture Manager)
	// =================================================================

	/** Find Live Link Face take folders / take archives / loose video under a directory. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString FindTakeDirectories(const FString& SearchDirectory, bool bRecursive = true);

	/** Ingest a single-camera video (mp4/mov) into a Footage Capture Data asset. AudioFilePath may be empty (audio from the video). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString IngestMonoVideo(const FString& VideoFilePath, const FString& AudioFilePath, const FString& Slate, int32 TakeNumber = 1);

	/** Ingest a Live Link Face take folder (copied from the iPhone, MetaHuman Animator mode for depth) into a Footage Capture Data asset. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString IngestLiveLinkFaceTake(const FString& TakeDirectoryPath);

	/** Ingest a Capture Manager take archive (.zip exported by Live Link Hub / Capture Manager). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString IngestTakeArchive(const FString& TakeArchivePath);

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

	/** Run the processing pipeline (blocking). StartFrame/EndFrame < 0 = whole take (EndFrame is exclusive). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString Process(const FString& PerformancePath, int32 StartFrame = -1, int32 EndFrame = -1);

	/** Cancel a running pipeline. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString CancelProcessing(const FString& PerformancePath);

	/** Status JSON: input type, inputs, can_process, is_processing, processed_frames, can_export, diagnostics. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString GetStatus(const FString& PerformancePath);

	// =================================================================
	// Export
	// =================================================================

	/**
	 * Export the processed performance to an Animation Sequence. TargetSkeletonPath may be a Skeleton or Skeletal
	 * Mesh (e.g. /Game/MetaHumans/Common/Face/Face_Archetype_Skeleton); empty keeps the performer skeleton.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString ExportAnimSequence(const FString& PerformancePath, const FString& PackagePath, const FString& AssetName, const FString& TargetSkeletonPath, bool bEnableHeadMovement = true, bool bProcessingRangeOnly = true);

	/**
	 * Export to a Level Sequence. TargetMetaHumanBlueprintPath: an assembled MetaHuman Blueprint to drive (empty = identity
	 * + camera only). Toggles for the identity, camera, audio and video tracks.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Animator")
	static FString ExportLevelSequence(const FString& PerformancePath, const FString& PackagePath, const FString& AssetName, const FString& TargetMetaHumanBlueprintPath, bool bExportIdentity = false, bool bExportCamera = true, bool bExportAudio = true, bool bExportVideo = false);
};
