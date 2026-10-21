# Auto camera

Plans the replay editor's free camera for a clip so that the shot looks as if a person filmed it.
The first style is "fisheye from below".

Status: the plan (stage 1) is written and tested apart from the game. Nothing writes keys into the
replay editor yet (stage 2), so the module is built only into its tests.

Code: [auto_camera_plan.h](../Extension/AutoCamera/auto_camera_plan.h) and
[auto_camera_plan.cpp](../Extension/AutoCamera/auto_camera_plan.cpp), with
[tests](../Extension/AutoCamera/Test/auto_camera_tests.cpp) (`DINGOSDK_BUILD_AUTO_CAMERA_TESTS`).

## What the plan does

Input: the clip's length, the skater's path (time, position) and the tricks (kind, start, end).
Output: keys (time, position, yaw, pitch, roll, field of view) for the free camera.

- The shot is built round the trick worth most (flip, grind, manual, ollie, bail, in that order).
- The camera stands low (0.15 to 0.40 m above the ground at the trick), 1.8 to 2.8 m to the side of
  the trick and a little ahead of it, with a field of view near 100 degrees.
- Keys: the clip's start, half a second before the trick, its middle, a moment after it, and the
  clip's end; on long gaps a few small keys between them.
- It looks where the skater was a moment ago (a hand's lag), but never so far behind that the skater
  leaves the middle of the frame; it tips up a little for a trick in the air.
- It pushes in on the trick (the field of view narrows by 8 degrees at its middle), leans a little
  and drifts a few centimetres. At `humanness` 0 it is a tripod.
- A path that comes too near the camera is met by trying the other side and standing further away.
  When no place is clear the plan is refused, with a reason.
- The same seed always gives the same plan, on every platform (its own random numbers).
- Key times rise by at least 0.12 s and angles never turn more than half a circle between keys, as the
  [keyframe smoothing](keyframe-smoothing.md) needs (it solves a spline over rising times).

## Key dump (diagnostic, read-only)

Before keys are written into the game, what a key holds has to be read off a real replay: its units, the
order of its three angles and the unit of its field of view. The dump does that and changes nothing.

- After every key edit in the replay editor the game rebuilds the session's keys; the dump hooks that
  (the hook the smoothing already has) and writes to `logs\ReSkate.log`, at info level:
  - the offsets into `Skate.exe` of the code that called the rebuild (`rebuild called from +0x... +0x...`),
    which is where the code that adds a key is to be looked for;
  - for every key (up to 64): its time, its raw value (twenty-four 32-bit words from the start of the
    value, so more than the value holds; a word that isn't a plain float is shown as hex bits) and the
    free camera's slot as the game's own slot builder makes it, with the floats that Behaviour 1 plays
    starred (`*`).
- At most one dump every 1.5 s and sixty a run, so that dragging a key doesn't fill the log.
- `keydump` in the console (`~`) writes the same for the replay the camera plays now, whenever it is run.
- Code: [replay_keyframes.cpp](../Extension/ReplayKeyframes/replay_keyframes.cpp) (the walk, with the
  game's own stepping through its key map and a guard against a fault) and
  [key_format.cpp](../Extension/AutoCamera/key_format.cpp) (the text, tested).

To use it: put a build's `ReSkate.dll` in the game folder, open a replay in the editor, place three or
more free-camera keys at positions you can describe (one low and near the skater, one high and far,
one with the camera tilted, say) and send `logs\ReSkate.log`.

## Stage 2: into the game

Needed from `Skate.exe` build `20260929`, none of it found yet:

1. **Keys out of the editor.** A function that adds a framing key (Behaviour 1, Subject 0) to a
   replay session: the keyframe code only rebuilds keys that already exist
   (`Engine/Game/Build/20260929/replay_keyframes.h`). Find it with the editor's "add keyframe" action,
   then add its fingerprint and address like the others.
2. **The skater's path and the tricks of a clip.** Either from the replay recording (the poses of the
   skater per frame) or from the game's trick events while skating. Which of the two exists is to be
   found out from a trace log.
3. **The units and angle order of a key.** This plan assumes metres, Y up and angles in radians, yaw 0
   along +Z; the lanes the free camera plays are position (slot floats 12 to 14), three angles (4 to
   6) and the field of view (21). The order of the three angles and the unit of the field of view are
   to be checked in game.
4. **Ground.** The plan takes the lowest point of the skater's path near the trick as the ground; a
   ray down from the camera is better.
5. **When it runs.** Chosen: when the editor opens, and only if the replay has no keys of the player's
   own; a switch in the menus, off by default.

## Not tried yet

- Any of it in the game.
- Other styles (follow and orbit, a static tripod at the spot) and a mix of styles.
