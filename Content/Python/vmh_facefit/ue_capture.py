"""VibeMetaHumans face-fit kit, step 2 (inside the editor): the MetaHuman's front capture for measure.py / fit.py.

    import json, sys, unreal
    sys.path.append(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_plugins_dir() + "VibeMetaHumans/Content/Python"))
    from vmh_facefit import ue_capture
    r = ue_capture.capture("/Game/MetaHumans/MH_Lila", "D:/fit/Lila", "iter0")   # -> mh_iter0.png + mh_iter0.json

capture_face_for_fit replaces the jam's per-character ue_capture.py (copied into five folders): the Creator's editing actor
is rendered in a private preview world, so no level actor, no "/Temp unsaved" LoadLevel trouble and no empty capture
from rendering in the same call as spawn_in_level. Gotchas found in the jam, kept here:
- A base-colour capture (bBaseColor=True) is written with alpha 0; the service forces alpha to 255.
- The character must wear no grooms while its landmarks move: remove hair/brows first.
- Preset skin and face indices are readable without an edit session; only the landmark moves need one.
- Measure the LIT render; MediaPipe missed faces on flat base colour more often.
"""
import json

import unreal


def capture(character, out_dir, tag="front", size=768, width_cm=30.0, base_color=False):
    r = json.loads(unreal.MetaHumanCharacterService.capture_face_for_fit(character, out_dir, tag, size, width_cm, base_color, True))
    if not r.get("success"):
        raise RuntimeError(r.get("error"))
    return r


def apply_deltas(character, deltas_json_path, tolerance_cm=0.06):
    """Not on this fork: move_face_landmarks (hold + converge loop) is cut in favour of the native
    MetaHumanCharacterEditorSubsystem landmark calls, which neither hold the other landmarks nor converge.
    Apply fit.py's deltas through the metahuman-creator skill's landmark steps instead."""
    raise NotImplementedError("move_face_landmarks is cut on this fork; apply the deltas with the native "
                              "MetaHumanCharacterEditorSubsystem landmark calls (metahuman-creator skill)")
