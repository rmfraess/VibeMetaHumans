---
name: metahuman-animator
display_name: MetaHuman Animator (offline facial animation)
description: Turn footage or audio into MetaHuman facial animation with unreal.MetaHumanAnimatorService and unreal.MetaHumanIdentityService - ingest a webcam/DSLR video, an iPhone Live Link Face take or a take archive into Footage Capture Data, build and auto-rig an Identity from a neutral frame (Mesh to MetaHuman), create MonoFootage / DepthFootage / Audio Performances, set audio mood, process, and export an AnimSequence or LevelSequence onto a MetaHuman. Use for "animate the face from this video/phone take/voice line".
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
---

# MetaHuman Animator (offline facial animation)

```python
import json, unreal
anim, ident = unreal.MetaHumanAnimatorService, unreal.MetaHumanIdentityService
```

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
2. For depth, create an Identity using `ident.create_identity_from_footage`; auto-rig is asynchronous,
   so poll `ident.get_status` until rigging completes and DNA exists. Run
   `MetaHumanAnimatorToolset.MetaHumanAnimatorIdentityTools.PrepareIdentityForPerformance` and wait
   for its result before processing. To seed a character from the Identity, use the native
   `unreal.MetaHumanCharacterEditorSubsystem.import_from_identity` in an acquired edit session.
   Identity auto-rig requires Epic login; `unreal.MetaHumanIdentity.log_in_to_auto_rig_service` is
   an instance method on the loaded Identity. Body type index 1–6 picks the preview preset.
3. Use `anim.create_performance` with the capture data and, for depth, the prepared Identity.
   For audio, use the SoundWave asset; `anim.set_audio_mood` controls mood and output.
   `anim.configure_performance` can adjust the remaining settings.
4. Process using `MetaHumanAnimatorToolset.MetaHumanAnimatorPerformanceTools.ProcessPerformance`.
   Inspect its schema and await the async result; use `anim.get_status` to inspect the performance.
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

## Gotchas

- Capture data assets land under `/Game/MHA` style folders chosen by Capture Manager settings; the
  ingest result gives the real path, use it.
- One performance processes at a time. Check the async result and `get_status` after a timeout
  rather than starting the operation again.
- An identity must be prepared (`face.has_predictive_solvers`) before a DepthFootage performance
  reports `can_process`.
- Epic's own sample scripts use mood names like `HAPPY`; the 5.8 enum names are `Happiness`,
  `Sadness`, etc. The service accepts enum names case-insensitively.
- Engine-level reference: UnrealEngineSkills `ue-metahuman-animator`.
