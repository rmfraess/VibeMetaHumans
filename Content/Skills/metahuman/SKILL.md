---
name: metahuman
display_name: MetaHumans with VibeMetaHumans
description: Create, edit, rig, animate and place MetaHumans in Unreal Engine 5.9 through the VibeMetaHumans services (unreal.MetaHumanCharacterService, MetaHumanAnimatorService, MetaHumanIdentityService, MetaHumanLiveLinkService, MetaHumanSetupService, MetaHumanObjectService). Use for any MetaHuman task - new character, body/skin/eyes/hair changes, facial animation from webcam video, iPhone Live Link Face takes or audio, real-time Live Link, assembly into a Blueprint.
vibeue_classes:
  - MetaHumanSetupService
  - MetaHumanCharacterService
  - MetaHumanAnimatorService
  - MetaHumanIdentityService
  - MetaHumanLiveLinkService
  - MetaHumanObjectService
keywords:
  - metahuman
  - metahuman creator
  - metahuman animator
  - live link face
  - facial animation
  - audio to face
  - mesh to metahuman
---

# MetaHumans with VibeMetaHumans

Every tool is a static method on an `unreal.<Service>` class (editor Python, or `call_tool` on the
MCP endpoint under `VibeMetaHumans.<Service>.<Tool>`). Every tool returns a JSON string with
`success`, so always `json.loads(...)` the result and branch on it. Asset arguments accept package
paths (`/Game/MetaHumans/MH_Hero`) or object paths.

## Start every MetaHuman session here

```python
import json, unreal
s = json.loads(unreal.MetaHumanSetupService.get_status())
```

- `plugins` must show `MetaHumanCharacter`, `MetaHuman`, `MetaHumanLiveLink`, `LiveLink`, `Takes` and
  `CaptureManagerEditor` as `enabled`. If not, stop and tell the user which plugin to enable
  (it needs an editor restart).
- `optional_content_installed == false` means the "MetaHuman Creator Core Data" launcher option is
  missing: no presets, no wardrobe library, no texture synthesis. Characters can still be created
  from the blank template and Animator / Live Link work normally. Tell the user; do not try to
  work around it.
- Cloud steps (character auto-rig `request_auto_rigging` and `request_texture_sources` on the native subsystem, identity auto-rig) need the editor to
  be signed in to the MetaHuman service with an Epic account. A failed/blocked rig request is the
  usual symptom; load the Identity asset and use `unreal.MetaHumanIdentity.log_in_to_auto_rig_service` to open the login.

## Routing table

| Task | Read this pack | Service |
|---|---|---|
| Environment, what exists, enum names, open an asset editor | `metahuman-setup` | `MetaHumanSetupService` |
| Make or change a character (body, skin, eyes, makeup, teeth, lashes, hair, clothes), rig it, build the Blueprint, place it | `metahuman-creator` | `MetaHumanCharacterService` |
| Facial animation from a video file (webcam/DSLR), an iPhone take, or audio; export AnimSequence/LevelSequence | `metahuman-animator` | `MetaHumanAnimatorService` (+ `MetaHumanIdentityService` for iPhone depth takes) |
| Real-time face capture in the editor, calibrate, bind to a placed MetaHuman, Take Recorder | `metahuman-live-link` | `MetaHumanLiveLinkService` |
| Anything not wrapped: read/patch properties, call a Blueprint library, describe a struct | (this pack, below) | `MetaHumanObjectService` |

## Rules of thumb

1. **One character at a time.** Native subsystem edits require an edit session: add the character
   with `try_add_object_to_edit`, and release it with `remove_object_to_edit` only if acquired.
   The MetaHumanGenerator toolset also provides session-based editing. Use deferred save and check status.
2. **Rig before you animate.** A character shows `rig_state: "Rigged"` in `get_summary` only after
   rigging. Only a rigged character assembles with a working face rig for Animator and Live Link.
3. **Pick the Animator route by input.** Plain video → `MonoFootage`; an iPhone take in MetaHuman
   Animator mode → `DepthFootage` plus a prepared Identity; audio → `Audio`.
4. **Long operations may outlive a request.** Native ingestion and performance processing are
   asynchronous; `build_async` queues a build. Check the result or status before continuing or retrying.
5. **Commit settings structs.** Read the relevant character settings struct, modify its copy, then
   call the matching native subsystem commit function. Inspect shapes with
   `MetaHumanObjectService.describe_struct` or `MetaHumanCharacterService.get_settings`.
6. **Keep the user's content layout.** Default output folders are `/Game/MetaHumans` (characters,
   builds) and `/Game/MHA` (capture data, identities, performances); follow the project's own
   convention when one exists.

## MetaHumanObjectService (escape hatch)

```python
o = unreal.MetaHumanObjectService
# Native ObjectToolset.GetProperties and SetProperties: inspect schema before calling.
# SetProperties does not run PostEditChangeProperty; prefer the relevant native setter/commit.
o.list_functions("MetaHumanCharacterEditorSubsystem", "Commit")
o.call_object_function(subject_settings_path, "SetSmoothing", '{"0": "/MetaHumanLiveLink/..."}')  # positional args by index
o.describe_struct("MetaHumanCharacterEyesSettings")
```

Enum values are passed by name (`"Cinematic"`, `"JointsOnly"`, `"MonoFootage"`); object properties by
asset path; nested structs as nested JSON objects.
