#pragma once
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/replay_keyframes.h"
#include <cstdint>
#include <span>
#include <string>

// Text for the replay keyframe dump (Extension/ReplayKeyframes/replay_keyframes.h, `keydump`): a framing
// key as the game stores it and as its slot builder turns it into a segment slot. Apart from the game, so
// that the format is tested. The dump exists to learn what a key's floats mean (units, order of the angles,
// the field of view's unit) before keys are written into the game, so it asserts no meaning: it prints
// every number under its index and marks the ones a Behaviour plays.
namespace dingosdk::auto_camera {
namespace layout = addr::replay_keyframes;

// "0=1.5 1=0 2=-3.25 ...": each 32-bit word of a key's raw value as a float under its index. A word that
// is not a plain float (not finite, a tiny non-zero value, or over a billion) is shown as its hex bits
// ("3=0x7fc00000"), since it may be an integer, a flag or half of a double.
std::string describe_raw(std::span<const std::uint32_t> words);

// "4*=0.2 5*=-1.1 ...": the floats of a slot under their index, with `*` after the index of each float the
// lanes play. A float that is not finite is shown as its value (nan, inf), not hidden.
std::string describe_slot(std::span<const float> slot, std::span<const layout::Lane> lanes);
} // namespace dingosdk::auto_camera
