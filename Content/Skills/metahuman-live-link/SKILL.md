---
name: metahuman-live-link
display_name: MetaHuman Live Link (real-time face capture)
description: Drive a MetaHuman's face live in the editor with unreal.MetaHumanLiveLinkService - add a MetaHuman Video source from a webcam, a MetaHuman Audio source from a microphone, or connect the Live Link Face app on an iPhone; calibrate neutrals, head tracking and smoothing; bind the subject to a placed MetaHuman actor (face AnimBP); record with Take Recorder. Use for "hook my webcam/phone up to the MetaHuman", "it is not moving", "record a take".
vibeue_classes:
  - MetaHumanLiveLinkService
  - MetaHumanObjectService
unreal_classes:
  - UMetaHumanLocalLiveLinkSourceBlueprint
  - ULiveLinkFaceSourceBlueprint
  - UMetaHumanLiveLinkSubjectSettings
  - UTakeRecorderBlueprintLibrary
keywords:
  - live link
  - live link face
  - webcam
  - real time facial animation
  - take recorder
  - calibrate neutral
---

# MetaHuman Live Link (real-time face capture)

```python
import json, unreal
ll = unreal.MetaHumanLiveLinkService
```

## Sources

| Source | Tool | Needs |
|---|---|---|
| Webcam / capture card (local MetaHuman Animator real-time solve) | `add_video_source(device_substring, subject, format_index)` | a camera; the MetaHuman Live Link plugin's real-time models (installed with the engine) |
| Microphone (audio-to-face) | `add_audio_source(device_substring, subject)` | a mic |
| iPhone / iPad running Live Link Face | `unreal.LiveLinkFaceSourceBlueprint.connect` (create a source first) | app open, **MetaHuman Animator** mode, same network |

Use `unreal.MetaHumanLocalLiveLinkSourceBlueprint.get_video_devices` and the track/format
APIs for device discovery. `format_index` -1 lets the remaining video service select automatically.
Use `unreal.LiveLinkBlueprintLibrary.get_live_link_subjects` and subject-state APIs to inspect sources:
`Connected` is good; `Unresponsive` after a phone connection means it is not streaming; `Disconnected`
means the device went away.
`remove_source(guid)` / `remove_source("all")` cleans up.

## Calibrate

With the performer relaxed and facing the camera, call `capture_neutrals` on the native
`unreal.MetaHumanLiveLinkSubjectSettings` object. For video subject settings, use
`unreal.MetaHumanVideoBaseLiveLinkSubjectSettings.set_head_orientation` and the corresponding
translation control. The remaining `subject_settings(subject, json_patch)` exposes other settings.
Inspect native method signatures before calling; the former service accepted different arguments.

## Bind to a character

```python
r = json.loads(ll.bind_subject_to_actor("BP_MH_Hero", "Webcam", True))
print(r["properties_set"], r["anim_blueprint_switched_on"])
```

Works on a placed, **built** MetaHuman Blueprint: it sets the Blueprint's Live Link subject
properties, any `LiveLinkComponentController`, and the Face component's `LLink_Face_Subj` variable
(switching the Face to `ABP_MH_LiveLink` when needed). `NO_LIVE_LINK_INPUT` means the actor is not a
MetaHuman Blueprint (the Creator preview actor does not take Live Link); build the character first
(`MetaHumanCharacterService.build`) and place `BP_<Name>`.

## Record

```python
ll.start_take_recording(["Webcam"])     # opens Take Recorder, adds a Live Link source per subject, starts
...
panel = unreal.TakeRecorderBlueprintLibrary.open_take_recorder_panel()
panel.stop_recording()
print(unreal.TakeRecorderBlueprintLibrary.is_recording())
```

Take Recorder writes a Level Sequence under `/Game/Cinematics/Takes/<date>/` with an animation track
per subject. For a clean AnimSequence of the character, record the actor itself in Take Recorder
too, or process the same take offline with `MetaHumanAnimatorService` (higher quality than the
real-time solve).

## Gotchas

- Subjects persist for the editor session only; re-add after a restart (or save a Live Link preset).
- The real-time video solve needs the GPU; expect a few seconds of warm-up on the first frames.
- The phone must show the same IP you pass; port 14785 is the Live Link Face control port.
- Capture neutrals again whenever the performer or camera moves.
- Engine-level reference: UnrealEngineSkills `ue-metahuman-live-link`.
