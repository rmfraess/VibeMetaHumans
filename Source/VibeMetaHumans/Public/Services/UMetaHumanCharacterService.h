// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UMetaHumanCharacterService.generated.h"

/**
 * MetaHuman Character Service - create and edit MetaHuman Character assets (the in-editor
 * MetaHuman Creator), then rig, texture, assemble and place them.
 *
 * Mental model: a UMetaHumanCharacter asset holds face/body state plus skin, eyes, makeup, head
 * (eyelashes/teeth) settings and an internal wardrobe collection. Edits go through the
 * MetaHumanCharacterEditorSubsystem; every tool here opens the character for edit automatically
 * (begin_edit) and most changes are committed immediately. Cloud steps (auto-rig, high-res textures)
 * require an Epic account signed in to the MetaHuman service. The character must be rigged
 * (request_auto_rig) before it can be assembled with a working face rig for Animator / Live Link.
 *
 * Python Usage:
 *   svc = unreal.MetaHumanCharacterService
 *   r = svc.create_character("/Game/MetaHumans", "MH_Hero", "")                  # blank template (or a preset path)
 *   svc.set_body_shape("/Game/MetaHumans/MH_Hero", 0.2, 0.3, 0.7, 182)            # masculine, lean, muscular, 182 cm
 *   svc.set_skin_tone("/Game/MetaHumans/MH_Hero", 0.55, 0.45)
 *   svc.set_settings("/Game/MetaHumans/MH_Hero", "eyes", '{"EyeLeft":{"Iris":{"PrimaryColorU":0.2,"PrimaryColorV":0.8}}}')
 *   svc.request_auto_rig("/Game/MetaHumans/MH_Hero", "JointsOnly", True)         # cloud, blocking
 *   svc.build("/Game/MetaHumans/MH_Hero", "Cinematic", "Cinematic", "/Game/MetaHumans", "")
 *   svc.spawn_in_level("/Game/MetaHumans/MH_Hero", unreal.Vector(0,0,0), unreal.Rotator(0,0,0), False)
 */
UCLASS(BlueprintType)
class VIBEMETAHUMANS_API UMetaHumanCharacterService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	// =================================================================
	// Lifecycle
	// =================================================================

	/**
	 * Create a MetaHuman Character asset. PresetCharacterPath may be empty (blank template) or point to a
	 * preset / existing character to copy from (requires the optional content for Epic presets).
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString CreateCharacter(const FString& PackagePath, const FString& AssetName, const FString& PresetCharacterPath);

	/** Open the character for editing in the subsystem (loads the texture synthesis model on first use). Idempotent. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString BeginEdit(const FString& CharacterPath);

	/** Release the character from the editing subsystem (frees preview meshes). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString EndEdit(const FString& CharacterPath);

	/** Summary JSON: rig state, DNA/texture flags, body type, skin/eye tone, active body constraints, wardrobe selections. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString GetSummary(const FString& CharacterPath);

	/** Save the character asset. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString Save(const FString& CharacterPath);

	// =================================================================
	// Look: skin, eyes, makeup, head model, face evaluation
	// =================================================================

	/** Read a settings section as JSON. Section: skin | eyes | makeup | head | face_evaluation | viewport. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString GetSettings(const FString& CharacterPath, const FString& Section);

	/**
	 * Patch a settings section from JSON (only the fields present change) and commit it. Use get_settings or
	 * MetaHumanObjectService.describe_struct("MetaHumanCharacterSkinSettings") to see the shape.
	 * Section: skin | eyes | makeup | head | face_evaluation | viewport.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString SetSettings(const FString& CharacterPath, const FString& Section, const FString& JsonPatch);

	/** Set the skin tone on the MetaHuman skin tone map. U = lightness (0 light .. 1 dark), V = redness (0 cool .. 1 red). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString SetSkinTone(const FString& CharacterPath, float U, float V);

	/** Set iris colour. U = temperature (0 cool blue/grey .. 1 warm brown), V = brightness (0 dark .. 1 light). Eye: both | left | right. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString SetEyeColor(const FString& CharacterPath, float U, float V, const FString& Eye);

	// =================================================================
	// Body
	// =================================================================

	/** Body constraints (parametric body sliders) as JSON: name, active, target, min, max. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString GetBodyConstraints(const FString& CharacterPath);

	/**
	 * Set body constraints from JSON {"Height":182,"Fat":0.3,"Muscularity":0.7,"Masculine/Feminine":0.2,...}.
	 * Height is in cm; other values are 0..1 normalised across the constraint's range unless bNormalized is
	 * false (raw measurements). Commits the body and re-aligns the neck.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString SetBodyConstraints(const FString& CharacterPath, const FString& ValuesJson, bool bNormalized = true);

	/** Convenience body shape: each value 0..1 (negative = leave unchanged), HeightCm 135..220 (negative = unchanged). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString SetBodyShape(const FString& CharacterPath, float MasculineFeminine, float Fat, float Muscularity, float HeightCm);

	/** Switch to a fixed legacy body type (f_med_nrw, m_tal_ovw, ...) or back to BlendableBody. See MetaHumanSetupService.get_enum_options. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString SetBodyType(const FString& CharacterPath, const FString& BodyType);

	// =================================================================
	// Face
	// =================================================================

	/** Face model (PCA) coefficients currently driving the head shape. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString GetFaceCoefficients(const FString& CharacterPath);

	/** Replace the face model coefficients (same length as get_face_coefficients returns) and commit. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString SetFaceCoefficients(const FString& CharacterPath, const TArray<float>& Coefficients);

	/** Reset the face back to the archetype (removes sculpting; keeps skin/eyes settings). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString ResetFace(const FString& CharacterPath);

	// =================================================================
	// Mesh to MetaHuman: import from Identity / DNA / template meshes
	// =================================================================

	/** Fit the face to a conformed MetaHuman Identity (from MetaHuman Animator). The identity must be conformed first. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString ImportFromIdentity(const FString& CharacterPath, const FString& IdentityPath, bool bUseEyeMeshes = true, bool bUseTeethMesh = true, bool bUseMetricScale = false);

	/** Import a face from a .dna file (whole rig, or fit when bImportWholeRig is false). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString ImportFromFaceDna(const FString& CharacterPath, const FString& DnaFilePath, bool bImportWholeRig = true, bool bIsolateHeadFromBody = false);

	/** Conform the face to MetaHuman-topology template meshes (head + optional eyes/teeth static or skeletal meshes). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString ImportFromTemplate(const FString& CharacterPath, const FString& HeadMeshPath, const FString& LeftEyeMeshPath, const FString& RightEyeMeshPath, const FString& TeethMeshPath);

	// =================================================================
	// Cloud: rig and textures
	// =================================================================

	/** Request the MetaHuman service auto-rig ("Mesh to MetaHuman"). RigType: JointsOnly | JointsAndBlendShapes. Needs Epic login. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString RequestAutoRig(const FString& CharacterPath, const FString& RigType, bool bBlocking = false);

	/** Remove the face rig (back to archetype DNA). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString RemoveFaceRig(const FString& CharacterPath);

	/** Download high-resolution skin texture sources from the MetaHuman service (resolutions from skin settings). Needs Epic login. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString RequestTextureSources(const FString& CharacterPath, bool bBlocking = true);

	// =================================================================
	// Wardrobe (hair, brows, lashes, beard, outfits)
	// =================================================================

	/** Slots, the items available in the character's collection, and the current selection per slot. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString GetWardrobe(const FString& CharacterPath);

	/**
	 * Add a Wardrobe Item asset (e.g. /MetaHumanCharacter/Optional/Grooms/Bindings/Hair/WI_Hair_L_StraightBangs) to a slot
	 * (Hair, Eyebrows, Eyelashes, Mustache, Beard, Peachfuzz, Outfits) and optionally select it.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString AddWardrobeItem(const FString& CharacterPath, const FString& SlotName, const FString& WardrobeItemPath, bool bSelect = true);

	/** Select an already-added wardrobe item in a slot (empty WardrobeItemPath clears the slot). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString SelectWardrobeItem(const FString& CharacterPath, const FString& SlotName, const FString& WardrobeItemPath);

	// =================================================================
	// Assembly and placement
	// =================================================================

	/** Rebuild the editor preview (after wardrobe changes). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString AssembleForPreview(const FString& CharacterPath);

	/** Can the character be built (assembled) right now? Logs the reason when not. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString CanBuild(const FString& CharacterPath);

	/**
	 * Assemble the character into a placeable Blueprint plus meshes/materials/grooms.
	 * PipelineType: Cinematic | Optimized | UEFN. Quality: Low | Medium | High | Cinematic (Optimized/UEFN only).
	 * BuildPath: content folder root (default /Game/MetaHumans); NameOverride: folder/asset name instead of the character name.
	 * Returns the Blueprints found under the output folder.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString Build(const FString& CharacterPath, const FString& PipelineType, const FString& Quality, const FString& BuildPath, const FString& NameOverride);

	/** Spawn the live-editing MetaHuman actor in the current editor level at a transform. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString SpawnInLevel(const FString& CharacterPath, FVector Location, FRotator Rotation, bool bKeepTransient = false);

	/** Export DNA files (head/body) for the character to a project folder and/or an external directory. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString ExportDna(const FString& CharacterPath, const FString& ProjectPath, const FString& ExternalPath, bool bHead = true, bool bBody = true);
};
