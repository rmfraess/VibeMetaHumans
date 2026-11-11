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
 * (native edits require their own session) and most changes are committed immediately. Cloud steps (auto-rig, high-res textures)
 * require an Epic account signed in to the MetaHuman service. The character must be rigged
 * (MetaHumanCharacterEditorSubsystem.request_auto_rigging) before it can be assembled with a working face rig for Animator / Live Link.
 *
 * Python Usage:
 *   svc = unreal.MetaHumanCharacterService
 *   r = svc.create_character("/Game/MetaHumans", "MH_Hero", "")                  # blank template (or a preset path)
 *   # Body shape: metahuman_toolset.metahuman.MetaHumanToolset.set_body_shape with an edit session.
 *   # Skin tone: metahuman_toolset.metahuman.MetaHumanToolset.set_skin_tone with an edit session.
 *   # Settings: read the character struct and commit via unreal.MetaHumanCharacterEditorSubsystem.
 *   # Rig: unreal.MetaHumanCharacterEditorSubsystem.request_auto_rigging (cloud), then svc.get_auto_rig_status(path)
 *   svc.build("/Game/MetaHumans/MH_Hero", "Cinematic", "Cinematic", "/Game/MetaHumans", "")
 *   # Preview: unreal.MetaHumanCharacterEditorSubsystem.spawn_meta_human_actor with a character object.
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

	/** Summary JSON: rig state, DNA/texture flags, body type, skin/eye tone, active body constraints, wardrobe selections. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString GetSummary(const FString& CharacterPath);

	/** Save the character asset. Routes to save_deferred when the character is in an edit session (see there for why). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString Save(const FString& CharacterPath);

	/**
	 * End the edit session now and save on a LATER editor tick, at least DelaySeconds and MinFrames away, never
	 * inside this call. Why: saving a Creator character in the same call that changed it deadlocked the editor four
	 * times (2026-10-06/07) - the minidumps showed the game thread sleep-polling inside UPackage::Save for async
	 * work (face texture synthesis) that only the game thread advances, so the save waits on itself. Returns at
	 * once; poll get_save_status. A newer save_deferred on the same character re-arms the timer; if it is back in
	 * an edit session when the tick fires, that save is skipped (the next save_deferred tries again).
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString SaveDeferred(const FString& CharacterPath, float DelaySeconds = 15.f, int32 MinFrames = 45);

	/** pending / status (requested|skipped|saved|failed) / requested_at / at / seconds / error for the character's last (or in-flight) save_deferred. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString GetSaveStatus(const FString& CharacterPath);

	// =================================================================
	// Look: skin, eyes, makeup, head model, face evaluation
	// =================================================================

	/** Read a settings section as JSON. Section: skin | eyes | makeup | head | face_evaluation | viewport. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString GetSettings(const FString& CharacterPath, const FString& Section);

	/** Set iris colour. U = temperature (0 cool blue/grey .. 1 warm brown), V = brightness (0 dark .. 1 light). Eye: both | left | right. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString SetEyeColor(const FString& CharacterPath, float U, float V, const FString& Eye);

	/** Switch to a fixed legacy body type (f_med_nrw, m_tal_ovw, ...) or back to BlendableBody. See MetaHumanSetupService.get_enum_options. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString SetBodyType(const FString& CharacterPath, const FString& BodyType);

	/** Reset the face back to the archetype (removes sculpting; keeps skin/eyes settings). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString ResetFace(const FString& CharacterPath);

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

	/**
	 * The Creator's per-item settings of the worn wardrobe items, read from the character (not a built Blueprint):
	 * for each selection in SlotName (Hair, Eyebrows, Eyelashes, Beard, Mustache, Peachfuzz, Outfits ...; empty =
	 * every slot but Character) its current parameter values, their types, the item's defaults and which ones the
	 * character overrides. Grooms: Melanin, Redness, Roughness, Whiteness, Lightness, DyeColor; Hair adds the Ombre
	 * (bUseOmbre, OmbreU/V/Color/Shift/Contrast/Intensity) and Highlights (bUseHighlights, HighlightsU/V/Color/
	 * Blending/Intensity/Variation) groups. Opens the character for editing and assembles its preview when needed.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString GetWardrobeItemParameters(const FString& CharacterPath, const FString& SlotName = TEXT(""));

	/**
	 * Set per-item settings on the character's worn item in SlotName - what the Creator's Hair / Wardrobe panels
	 * edit, stored on the MetaHuman Character so every build carries them (colour a groom here, never on a built
	 * Blueprint's MI_WI_* materials: a rebuild resets those). ParametersJson maps parameter names (case-insensitive,
	 * from get_wardrobe_item_parameters) to values, e.g. {"Melanin": 0.48, "Redness": 0.53, "DyeColor": [1, 0.2, 0.2]};
	 * colours take [r, g, b(, a)], {"R": ...} or "#RRGGBB". ItemName (e.g. WI_Hair_S_Clean) picks one item when the
	 * slot holds several. bClearOverrides first resets the item to its defaults (alone = reset). Works on the
	 * Creator's live preview and commits it to the asset, so it is safe while the Creator is open. Not saved.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString SetWardrobeItemParameters(const FString& CharacterPath, const FString& SlotName, const FString& ParametersJson, const FString& ItemName = TEXT(""), bool bClearOverrides = false);

	/**
	 * Assemble the character into a placeable Blueprint plus meshes/materials/grooms.
	 * PipelineType: Cinematic | Optimized | UEFN. Quality: Low | Medium | High | Cinematic (Optimized/UEFN only).
	 * BuildPath: content folder root (default /Game/MetaHumans); NameOverride: folder/asset name instead of the character name.
	 * Returns the Blueprints found under the output folder.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString Build(const FString& CharacterPath, const FString& PipelineType, const FString& Quality, const FString& BuildPath, const FString& NameOverride);

	/**
	 * Queue a build() on a later editor tick and return at once (status queued). Why: a 4-5 minute blocking build
	 * made the MCP client time out and drop the session's Unreal tools (2026-10-06). The build itself still blocks
	 * the game thread for several minutes once it starts - poll get_build_status rather than waiting on this call.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString BuildAsync(const FString& CharacterPath, const FString& PipelineType, const FString& Quality, const FString& BuildPath, const FString& NameOverride);

	/** status (queued|running|done|failed) / seconds / blueprints / output_folder / error for the character's last (or in-flight) build_async. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString GetBuildStatus(const FString& CharacterPath);

	/** status (running|done|failed_or_not_requested) for a pending auto-rig request (the subsystem's request_auto_rigging already reports progress on the subsystem; this just shapes it the same way as get_build_status). */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString GetAutoRigStatus(const FString& CharacterPath);

	// =================================================================
	// Custom mesh to MetaHuman: the Mesh Import tool's "Import > from custom mesh" without the UI (2026-10-04)
	// =================================================================

	/**
	 * Conform the MetaHuman body and head to a custom humanoid mesh (a static or skeletal mesh already imported into
	 * the project): the UI's "Import > from custom mesh > Auto Solve". MeshPath is the combined body+head mesh;
	 * HeadMeshPath may name a separate head mesh (then MeshPath is the body only). The mesh must stand in the
	 * MetaHuman frame: feet on z=0, facing +Y, centimetres, A-pose with the arms clear of the body and the fingers
	 * apart, bald, no lashes. With bTrackFace the head is rendered front-on in a preview scene and the face landmarks
	 * are tracked (the auto solve needs them); the camera follows the mesh's own facing. Blocking: a 60k-vertex
	 * mesh takes 1-3 minutes. Afterwards the character holds the solved body in the MESH'S pose: MetaHumanCharacterExportBlueprintLibrary.export_posed_dna
	 * before the subsystem's commit_posed_state_as_a_pose.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString ImportFromCustomMesh(const FString& CharacterPath, const FString& MeshPath, const FString& HeadMeshPath, bool bAutoSolve = true, bool bTrackFace = true, int32 TrackingImageSize = 1024);

	/**
	 * A skeletal mesh asset from a DNA (a DNA asset path, or a .dna file on disk): the DNA asset's "Generate Skeletal
	 * Mesh" action, synchronous. Skeleton: "body" (the MetaHuman body skeleton; a posed/combined DNA), "face", "none"
	 * (a skeleton is generated), or a skeleton asset path.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString GenerateSkeletalMeshFromDna(const FString& DnaPath, const FString& PackagePath, const FString& AssetName, const FString& Skeleton = TEXT("body"));

	// =================================================================
	// Round trips with a DCC (Blender): FBX out, textures and accessories back in
	// =================================================================

	/**
	 * Export a static or skeletal mesh asset to an FBX file (centimetres, LOD0, no morph targets, no collision), as
	 * the Content Browser's Asset Actions > Export does. Empty material slots are filled with the default material
	 * for the export (an empty slot crashed the exporter, 2026-10-04).
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString ExportFbx(const FString& AssetPath, const FString& FilePath);

	/**
	 * Replace an existing skeletal mesh's geometry IN PLACE from an FBX, on its own skeleton, with its slot
	 * materials restored by slot name afterwards (bKeepMaterials): the Content Browser's "Reimport" without the
	 * dialog. Refuses (SKELETON_MISMATCH) rather than leaving the Blueprint pointing at a retargeted mesh.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString ReimportSkeletalMesh(const FString& AssetPath, const FString& FbxPath, bool bKeepMaterials = true);

	/**
	 * Import a PNG/TGA/EXR as a Texture2D. Kind: "color" (sRGB, default compression), "normal" (linear, Normalmap
	 * compression; bFlipGreen turns an OpenGL +Y map into Unreal's -Y), "mask" (linear, Masks), "linear".
	 * Finishes the texture's compilation and refuses (IMPORT_INCOMPLETE, nothing marked dirty) when it built as a
	 * 32x32 placeholder with no miplevels: a texture-pipeline stall after long editor sessions (2026-10-07) that
	 * rendered a face white and hangs the next save of any material instance that uses it - restart the editor first.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString ImportTexture(const FString& FilePath, const FString& PackagePath, const FString& AssetName, const FString& Kind = TEXT("color"), bool bFlipGreen = false);

	/**
	 * Add a SkeletalMeshComponent to a Blueprint under a parent component (a built MetaHuman's "Body"), set its mesh,
	 * drive it from the parent's pose (Leader Pose Component) and compile: how an accessory rigged to the MetaHuman
	 * skeleton (hair, horns, armour) is put on the character.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString AttachSkeletalMeshToBlueprint(const FString& BlueprintPath, const FString& ComponentName, const FString& SkeletalMeshPath, const FString& ParentComponentName = TEXT("Body"), bool bLeaderPose = true);

	/**
	 * Add a StaticMeshComponent to a Blueprint under a parent component, attached to one of its bone sockets ("head" on a
	 * MetaHuman's Body) with a relative transform (centimetres, degrees): a rigid accessory - a headset, a visor, a
	 * horn - rides the bone without any skinning. Replaces a component of that name if one exists.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString AttachStaticMeshToBlueprint(const FString& BlueprintPath, const FString& ComponentName, const FString& StaticMeshPath, const FString& ParentComponentName, const FString& SocketName, FVector RelativeLocation, FRotator RelativeRotation);

	/** Remove a component (by name) from a Blueprint's construction script, promoting its children. Compiles; marks the Blueprint dirty. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString RemoveComponentFromBlueprint(const FString& BlueprintPath, const FString& ComponentName);

	/**
	 * Add a GroomComponent under a built MetaHuman Blueprint's TargetComponent (default "Face"), bound to it:
	 * GroomAssetPath may be a groom Wardrobe Item (WI_Hair_...), the plugin's own GroomBindingAsset, or a bare
	 * UGroomAsset (e.g. from import_groom, which has no plugin binding to retarget). For a wardrobe item or plugin
	 * binding, the plugin's binding is DUPLICATED into the Blueprint's own Accessories/ComponentName folder and
	 * retargeted onto the Blueprint's built face mesh (DrHart's working recipe, 2026-10-07: Build()'s own groom
	 * pipeline does the same - DuplicateObject, SetTargetSkeletalMesh, let the async RBF rebuild run). With
	 * MaterialFromComponent, its override materials are copied onto the new component's matching slots (by name,
	 * then by position). bSaveBinding saves the duplicated binding (wait at least a minute after this call if you
	 * set it false and save it yourself - a save mid-build can hang the editor). No Blueprint save here either; the
	 * caller saves it once satisfied.
	 *
	 * SocketName attaches the groom to a bone/socket of TargetComponent (a custom groom with no binding rides it rigidly:
	 * binding 40000 Blender strands to DrHart's face mesh kept 12 cores busy for 16+ minutes, 2026-10-07).
	 * bInverseSocketTransform: the relative transform is the inverse of that bone's reference-pose component transform,
	 * so a groom authored in the mesh's component space (out/_HairTools exports) lands where it was authored;
	 * RelativeLocation/RelativeRotation are applied on top. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString AddGroomToBlueprint(const FString& BlueprintPath, const FString& ComponentName, const FString& GroomAssetPath, const FString& TargetComponent = TEXT("Face"), const FString& MaterialFromComponent = TEXT(""), bool bSaveBinding = true,
		const FString& SocketName = TEXT(""), bool bInverseSocketTransform = false, FVector RelativeLocation = FVector::ZeroVector, FRotator RelativeRotation = FRotator::ZeroRotator);

	/** Import a groom from an Alembic (.abc) file (guides/strands) as a UGroomAsset: UGroomImportOptions / AssetImportTask, automated (no dialog). Reports each group's strand/guide counts.
	 *
	 * Set up to render as a custom groom (2026-10-07, what made DrHart's Blender groom show): bRigidBinding sets every
	 * LOD's binding to Rigid (an imported groom defaults to Skinning, which draws nothing without a binding asset);
	 * HairWidthCm > 0 turns the width override on at that width with TipScale at the tips (Blender's Alembic carries no
	 * widths: without it every strand took a hair-thin default and the groom read as a translucent ghost). Pass
	 * HairWidthCm <= 0 to keep the file's own widths. Nothing is saved: save the groom in a LATER call. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeMetaHumans|Character")
	static FString ImportGroom(const FString& AlembicPath, const FString& PackagePath, const FString& AssetName, bool bRigidBinding = true, float HairWidthCm = 0.013f, float TipScale = 0.6f);
};
