// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UMetaHumanIdentityService.generated.h"

/**
 * MetaHuman Identity Service - "Mesh to MetaHuman" from captured footage or DNA.
 *
 * A MetaHuman Identity asset turns a neutral-expression frame of depth footage (an iPhone
 * Live Link Face take captured in MetaHuman Animator mode, ingested with
 * CaptureManagerToolset.CaptureManagerIngestTools.IngestLiveLinkFace) into a conformed template mesh, then the
 * MetaHuman cloud service turns that into a rigged face (DNA). A prepared identity is required to
 * process DEPTH footage performances; it can also be imported into a MetaHuman Character
 * (unreal.MetaHumanCharacterEditorSubsystem.import_from_identity) to get a likeness.
 *
 * Monocular webcam video does not produce depth, so it cannot create an identity; use it for
 * MonoFootage performances instead.
 *
 * Python Usage:
 *   # Via call_tool or vibeue.exec_tool, await each MetaHumanAnimatorToolset.MetaHumanAnimatorIdentityTools step:
 *   # CreateIdentityFromFootage -> PromoteAndTrackFrame -> ConformIdentityFace -> GenerateIdentityRig
 *   # -> optional FitIdentityTeeth -> PrepareIdentityForPerformance. InspectIdentity reads state;
 *   # confirm rigging and DNA before preparing for performance.
 */
UCLASS(BlueprintType)
class VIBEMETAHUMANS_API UMetaHumanIdentityService : public UToolsetDefinition
{
	GENERATED_BODY()

public:

	/** Status JSON: logged_in, auto_rig_in_progress, tracking_in_progress, has_dna, has_predictive_solvers, conformal_rig_valid, poses/frames, body type, diagnostics. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Identity")
	static FString GetStatus(const FString& IdentityPath);

	/** Send the conformed face to the MetaHuman service for auto-rigging (asynchronous). Poll get_status. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Identity")
	static FString RequestAutoRig(const FString& IdentityPath);

	/** Export the identity's DNA (and brows json) to files on disk. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Identity")
	static FString ExportDna(const FString& IdentityPath, const FString& DnaOutPath, const FString& BrowsOutPath);
};
