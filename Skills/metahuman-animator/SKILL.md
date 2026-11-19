---
name: metahuman-animator
display_name: MetaHuman Animator (offline facial animation)
description: Turn footage or audio into MetaHuman facial animation, and play it in a game (a face AnimBP with a slot, blinks added to baked clips), with unreal.MetaHumanAnimatorService and unreal.MetaHumanIdentityService - ingest a webcam/DSLR video, an iPhone Live Link Face take or a take archive into Footage Capture Data (Epic CaptureManagerToolset), build and auto-rig an Identity from a neutral frame (Mesh to MetaHuman), create MonoFootage / DepthFootage / Audio Performances, process (Epic ProcessPerformance) and export an AnimSequence (Epic) or LevelSequence onto a MetaHuman. Use for "animate the face from this video/phone take/voice line".
vibeue_classes:
  - MetaHumanAnimatorService
  - MetaHumanIdentityService
  - MetaHumanObjectService
unreal_classes:
  - UMetaHumanPerformance
  - UMetaHumanIdentity
  - UFootageCaptureData
  - UMetaHumanPerformanceExportUtils
keywords:
  - metahuman animator
  - facial animation
  - webcam
  - live link face take
  - identity
  - mesh to metahuman
  - audio driven animation
  - lip sync
  - export anim sequence
  - face anim blueprint
  - blinks
  - head snap
---

# MetaHuman Animator (offline facial animation)

```python
import json, unreal
anim, ident = unreal.MetaHumanAnimatorService, unreal.MetaHumanIdentityService
# Epic AI tools (inside execute_python): await call_tool("<Toolset.Full.Name>.<Tool>", camelCaseParam=value)
CM   = "CaptureManagerToolset.CaptureManagerIngestTools"
PERF = "MetaHumanAnimatorToolset.MetaHumanAnimatorPerformanceTools"
IDT  = "MetaHumanAnimatorToolset.MetaHumanAnimatorIdentityTools"
INS  = "CaptureManagerToolset.CaptureManagerInspectionTools"
EXP  = "MetaHumanAnimatorToolset.MetaHumanAnimatorExportTools"
```

Ingest, processing, identity auto-rig / prepare and AnimSequence export are Epic's own tools now
(`CaptureManagerToolset`, `MetaHumanAnimatorToolset`, `EditorToolset`; the plugin declares them as optional
dependencies, and the project must enable them, e.g. via `AllToolsets` / `AllMetaHumanToolsets`).
This service keeps Performance creation, configuration, status, cancel and Level Sequence export.

## Choose the route by input

| Input | Native capture tool | Identity | Performance input |
|---|---|---|---|
| Webcam / DSLR / phone video | `CaptureManagerToolset.CaptureManagerIngestTools.IngestMonoVideo` | none | `MonoFootage` |
| Live Link Face take with depth | `CaptureManagerToolset.CaptureManagerIngestTools.IngestLiveLinkFace` | prepared | `DepthFootage` |
| `.cptake` archive | `CaptureManagerToolset.CaptureManagerIngestTools.IngestTakeArchive` | depends on contents | as appropriate |
| SoundWave asset | none | none | `Audio` |

Use `CaptureManagerToolset.CaptureManagerIngestTools.ScanForTakes` to find takes. Check its
truncation flag. ARKit-mode takes have no depth and use the MonoFootage route.

## Workflow

1. Ingest footage with the matching Capture Manager tool. These ingests are asynchronous: wait for
   completion and use the returned capture-data asset, not a guessed path. Call through
   `vibeue.exec_tool(toolset, tool, args)` and inspect `vibeue.get_toolset_schema(toolset)` for
   parameters and async result handling. Good video has a well-lit, forward-facing face.
2. For depth, use `MetaHumanAnimatorToolset.MetaHumanAnimatorIdentityTools` via `call_tool` or
   `vibeue.exec_tool`: create the Identity from footage (inspect the toolset schema for its exact
   name) → `PromoteAndTrackFrame` → `ConformIdentityFace`
   → `GenerateIdentityRig` → optionally `FitIdentityTeeth` → `PrepareIdentityForPerformance`.
   These are asynchronous: wait for each result and use `InspectIdentity` to inspect state; confirm
   rigging has completed and DNA exists before preparing and processing. To seed a character from the Identity, use the native
   `unreal.MetaHumanCharacterEditorSubsystem.import_from_identity` in an acquired edit session.
   Identity auto-rig requires Epic login; `unreal.MetaHumanIdentity.log_in_to_auto_rig_service` is
   an instance method on the loaded Identity. Body type index 1–6 picks the preview preset.
3. Create, configure and process in one call with Epic's
   `MetaHumanAnimatorToolset.MetaHumanAnimatorPerformanceTools`: `InspectFootageForPerformance(footage)`
   first, then `GenerateAnimationFromFootage(footage, params)` (pick `params.inputType` from the
   inspection; DepthFootage needs `params.identity`, MonoFootage needs `params.cameraIndex`) or
   `GenerateAnimationFromAudio(audio, params)` (mood, intensity and MouthOnly/FullFace are in `params`).
   Both are async; after completion read `animationDataStatus` and `diagnostics`, not just `bSucceeded`,
   and reuse `performanceAssetPath` instead of re-running. To re-solve an existing Performance with
   overrides, use `ProcessPerformance`.
4. To create a blank Performance without solving it: `f = unreal.MetaHumanPerformanceFactoryNew()`,
   `p = unreal.AssetToolsHelpers.get_asset_tools().create_asset('Perf_x', '/Game/MHA', unreal.MetaHumanPerformance, f)`,
   then set `input_type` / `audio` with `p.set_editor_property`. Edit settings with
   `EditorToolset.ObjectToolset.SetProperties` and read them back with `GetProperties`. Inspect any Performance
   with `p.can_process()`, `p.is_processing()`, `p.can_export_animation()` and
   `p.diagnostics_indicates_processing_issue()` (the engine has no status tool);
   `unreal.MetaHumanPerformance.cancel_pipeline` cancels a running pipeline on the loaded asset.
5. Export animation with `unreal.MetaHumanAnimatorExportTools.export_performance_animation_sequence`,
   or a Level Sequence with `unreal.MetaHumanPerformanceExportUtils.export_level_sequence`.
   Inspect the engine API signatures and settings before calling: these APIs do not accept the old
   wrapper's path-and-flags argument list. Set the target skeleton explicitly for animation export;
   a built MetaHuman's Face component can play the sequence. Open results with
   `unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets`.

For AI tool schemas:

```python
import vibeue
print(vibeue.get_toolset_schema("CaptureManagerToolset.CaptureManagerIngestTools"))
print(vibeue.get_toolset_schema("MetaHumanAnimatorToolset.MetaHumanAnimatorPerformanceTools"))
print(vibeue.get_toolset_schema("MetaHumanAnimatorToolset.MetaHumanAnimatorIdentityTools"))
```

## Face clips in a game (baked lip sync)

```python
# a face AnimBP that can play clips: copy pose from the body + DefaultSlot below FACIAL_C_FacialRoot + HeadControlSwitch x 0
r = json.loads(anim.create_face_anim_blueprint("/Game/Anim", "ABP_Face_Slot", "", "", "DefaultSlot"))   # 4th arg: optional looping blink clip
# set it on every built Blueprint (and again after every rebuild)
unreal.MetaHumanWardrobeService.post_build_fixup("/Game/MetaHumans/Vale/BP_Vale", json.dumps({"face_anim_class": r["anim_blueprint"]}), False)
# blinks: audio-driven bakes barely blink (curves peaked at 0.05); a blink every 2.8-4.2 s where the clip has none
print(json.loads(anim.add_blinks("/Game/Anims/Face", 2.8, 4.2, True, '{"name_prefix": "FA_"}')))   # dry run: planned times
anim.add_blinks("/Game/Anims/Face", 2.8, 4.2, False, '{"name_prefix": "FA_", "save": true}')
```

- Play a line with `PlaySlotAnimationAsDynamicMontage(clip, "DefaultSlot", ...)` on the Face component's anim instance and
  start the voice with it. Log an error when there is no anim instance; never fall back to `PlayAnimation` (single-node
  mode destroys the AnimBP instance for the line and a seated character's head drops off the neck).
- `add_blinks` is deterministic per asset name (a re-run adds nothing) and keeps the clip's own blinks (>= 0.6). Curve
  names come back lowercased from bakes; the tool matches case-insensitively.
- For blinks in the silence between lines pass a looping blink-only clip as `BlinkLoopAnimPath`: it is blended over the
  face by max curve value. One shared loop blinks every character in phase.
- Bake offline, do not solve at run time: the realtime solver models are 318 MB of cook (see
  `/Skills/VibeMetaHumans/metahuman-shipping`). Verify each bake: clip length within 0.1 s of the audio, and
  `CTRL_expressions_jawOpen` peaking above 0.3.

## Gotchas

- Capture data assets land under `/Game/MHA` style folders chosen by Capture Manager settings; the
  ingest result gives the real path, use it.
- One performance processes at a time. Check the async result and `p.is_processing()` after a timeout
  rather than starting the operation again.
- An identity must be prepared (`face.has_predictive_solvers`) before a DepthFootage performance
  reports `can_process`.
- Epic's own sample scripts use mood names like `HAPPY`; the 5.8 enum names are `Happiness`,
  `Sadness`, etc. The service accepts enum names case-insensitively.
- **Face clips played as montages do not show on the plugin's face AnimBP.** `/MetaHumanCharacter/Face/ABP_Face`
  is CopyPoseFromMesh only, with no Slot node, so a face AnimSequence played on a slot (`PlaySlotAnimationAsDynamicMontage`,
  a sequencer slot track) is silently ignored: voice plays, face still. `create_face_anim_blueprint` makes one that
  works; a plain Slot after the copy is wrong too (an exported clip is a root plus ~260 curves: it snapped head and
  neck to the reference pose), so the slot is blended in below `FACIAL_C_FacialRoot` with curves overriding.
- **Exported head movement snaps the head.** In `ABP_Face_PostProcess`, `HeadControlSwitch` is an on/off gate for
  the head-movement IK (0.35 = off, no blend); when on, it snaps the head to the archetype pose (about 4.4 cm forward
  and down on one character). Scaling HeadYaw/Pitch/Translation does not remove that offset: the AnimBP from
  `create_face_anim_blueprint` multiplies `HeadControlSwitch` by 0 (what shipped). Exporting without head movement is
  the other route (not tried in the jam).
- **A rebuild resets the Face anim class** to the plugin's ABP_Face: `post_build_fixup` with `face_anim_class`.
- An AnimGraph node's `show_pin_for_properties` cannot be applied from Python (no ReconstructNode; set_editor_property
  does not fire the PostEditChange). Use node property maps such as ModifyCurve's `curve_map`.
- **Never duplicate an engine-plugin AnimBP into /Game as a test fixture** (e.g. `ABP_MH_LiveLink`, `ABP_Face`).
  Deleting the copy runs ForceDeleteObjects, which cannot unload a package that references MetaHuman Control Rig
  classes, and the editor then fatals on a `RigVMHost` cast. Build fixtures from a minimal AnimBP you create.
- Engine-level reference: UnrealEngineSkills `ue-metahuman-animator`.
