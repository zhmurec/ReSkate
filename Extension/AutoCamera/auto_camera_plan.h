#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Auto camera: plans the free camera's keyframes for a replay clip so that the shot looks as if a person
// filmed it. This part is apart from the game: it takes the skater's path and the tricks in the clip
// and returns a list of keys (time, position, angles, field of view). Writing the keys into the replay
// editor is the game's side (see contrib/auto-camera.md).
//
// Conventions of this plan (to be checked against the game before the keys are written into it):
//   - world metres, Y up; time in seconds from the clip's start;
//   - yaw 0 looks along +Z and turns towards +X; pitch is positive up; roll is positive clockwise
//     seen from behind; angles in radians, the field of view (vertical) in degrees;
//   - the plan never moves a key out of the clip, and its key times rise by at least min_key_gap, as
//     the keyframe smoothing (Extension/ReplayKeyframes) needs.
//
// The shot ("fisheye from below"): the camera sits low beside the line of the trick, a little ahead of
// it, with a wide field of view. It looks at the skater a moment late, as a hand does, drifts a little,
// pushes in on the trick and leans slightly. At humanness 0 it is a tripod: one position, no lean, no
// lag and no drift. The same seed always gives the same plan.
namespace dingosdk::auto_camera {
struct Vec3 {
    double x = 0, y = 0, z = 0;
};
double distance(const Vec3& a, const Vec3& b) noexcept;

// One point of the skater's path through the clip, in time order.
struct PathSample {
    double time;
    Vec3 position; // at the skater's feet
};

enum class TrickKind { ollie, flip, grind, manual, bail };
struct TrickEvent {
    TrickKind kind;
    double start, end; // seconds from the clip's start
};

struct Clip {
    double length = 0;
    std::vector<PathSample> path;
    std::vector<TrickEvent> tricks;
};

struct Style {
    double fov_degrees = 100;       // the wide, fisheye look
    double punch_in_degrees = 8;    // how much narrower at the trick
    double height_min = 0.15;       // metres above the ground at the trick
    double height_max = 0.40;
    double distance_min = 1.8;      // metres to the side of the trick
    double distance_max = 2.8;
    double ahead_min = 0.4;         // metres ahead of the trick along the skater's heading
    double ahead_max = 1.4;
    double clearance_metres = 1.0;  // how near the skater may pass the camera
    double humanness = 1.0;         // 0 tripod .. 1 a hand's lag, drift and lean
};

struct Key {
    double time;
    Vec3 position;
    double yaw, pitch, roll; // radians
    double fov_degrees;
};

struct Plan {
    std::vector<Key> keys;
    std::size_t trick = 0; // the trick the shot is built round (index in Clip::tricks)
    int side = 1;          // which side of the skater's heading the camera stands on (1 right, -1 left)
};

inline constexpr double min_key_gap = 0.12;
inline constexpr double fov_min_degrees = 60;
inline constexpr double fov_max_degrees = 120;
inline constexpr double hard_clearance_metres = 0.5;

// The skater's position at a time (linear between samples, held beyond the ends). The path must not be empty.
Vec3 skater_at(const std::vector<PathSample>& path, double time) noexcept;
// Where a key looks, as a unit vector.
Vec3 view_direction(const Key& key) noexcept;

// Plans the shot. False, with the reason in `error` and `out` empty, when the clip can't be shot: no path
// or tricks, a path out of time order, a trick outside the clip, a clip under half a second, or no
// camera position that keeps the skater in frame and clear of the camera.
bool make_plan(const Clip& clip, const Style& style, std::uint64_t seed, Plan& out, std::string& error);

// Whether a plan meets what the game's keyframes need and what a viewer would see: key times rising
// within the clip, finite values, angles continuous (no turn of more than half a circle between keys),
// the field of view in range, and the skater in the middle of the frame and not too close at every
// key. `error` says which rule failed.
bool validate(const Plan& plan, const Clip& clip, std::string& error);
} // namespace dingosdk::auto_camera
