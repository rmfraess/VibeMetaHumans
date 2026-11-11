// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UMetaHumanLiveLinkService.generated.h"

/**
 * MetaHuman Live Link Service - real-time facial animation into the editor.
 *
 * Three live sources:
 *  - MetaHuman Video source: any webcam/capture card on this machine, solved locally by the
 *    MetaHuman Animator real-time model (add_video_source).
 *  - Live Link Face app on an iPhone/iPad on the same network (unreal.LiveLinkFaceSourceBlueprint.connect; the app
 *    must be in MetaHuman Animator mode and show this machine's address).
 *  - MetaHuman Audio source: a microphone driving audio-to-face (add_audio_source).
 * Each creates a Live Link subject. Bind the subject to a placed, assembled MetaHuman actor with
 * bind_subject_to_actor, then record with start_take_recording (Take Recorder) to get an Animation
 * Sequence, or process offline with MetaHumanAnimatorService for best quality.
 *
 * Python Usage:
 *   svc = unreal.MetaHumanLiveLinkService
 *   print(unreal.MetaHumanLocalLiveLinkSourceBlueprint.get_video_devices())
 *   r = json.loads(svc.add_video_source("", "Webcam", -1))        # first camera, auto format
 *   # Capture neutrals on the unreal.MetaHumanLiveLinkSubjectSettings object while performer holds a neutral face.
 *   svc.bind_subject_to_actor("BP_MH_Hero", "Webcam")
 *   svc.start_take_recording(["Webcam"]); ...; unreal.TakeRecorderBlueprintLibrary.open_take_recorder_panel().stop_recording()
 */
UCLASS(BlueprintType)
class VIBEMETAHUMANS_API UMetaHumanLiveLinkService : public UToolsetDefinition
{
	GENERATED_BODY()

public:

	/**
	 * Add a MetaHuman Video Live Link source + subject from a local camera. DeviceName matches part of the device
	 * name (empty = first device). FormatIndex picks from the device's formats (native MetaHumanLocalLiveLinkSourceBlueprint.get_video_formats) (negative = a sensible 720p-1080p
	 * 24-60 fps format). Blocks for a few seconds while the device starts.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|LiveLink")
	static FString AddVideoSource(const FString& DeviceName, const FString& SubjectName, int32 FormatIndex = -1);

	/** Add a MetaHuman Audio Live Link source + subject from a microphone (DeviceName matches part of the name; empty = first). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|LiveLink")
	static FString AddAudioSource(const FString& DeviceName, const FString& SubjectName);

	/** All Live Link sources: guid, type, status, machine. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|LiveLink")
	static FString ListSources();

	/** Remove one source by guid (from list_sources), or every source when SourceGuid is "all". */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|LiveLink")
	static FString RemoveSource(const FString& SourceGuid);

	/** Read/patch a subject's settings object (smoothing, calibration alpha, mood for audio subjects...). Empty patch = read only. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|LiveLink")
	static FString SubjectSettings(const FString& SubjectName, const FString& JsonPatch);

	// =================================================================
	// Character binding and recording
	// =================================================================

	/**
	 * Point a placed MetaHuman actor at a Live Link subject: sets every Live Link subject property found on the actor,
	 * its components, any LiveLinkComponentController, and the face AnimBP's LLink_Face_Subj variable. When the face
	 * component is not running a Live Link AnimBP yet, bSwitchFaceToLiveLinkAnimBP swaps it to ABP_MH_LiveLink first.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|LiveLink")
	static FString BindSubjectToActor(const FString& ActorLabel, const FString& SubjectName, bool bSwitchFaceToLiveLinkAnimBP = true);

	/** Start a Take Recorder recording of the given Live Link subjects (opens the Take Recorder panel). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|LiveLink")
	static FString StartTakeRecording(const TArray<FString>& SubjectNames);

};
