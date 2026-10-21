#include "Extension/AutoCamera/auto_camera_plan.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace dingosdk::auto_camera {
namespace {
constexpr double pi = std::numbers::pi;
constexpr double degrees = pi / 180.0;
constexpr double aim_height = 0.55;       // metres above the feet: the skater's middle
constexpr double airborne_extra = 0.30;   // the camera tips up a little for a trick in the air
constexpr double frame_fraction = 0.6;    // the skater stays inside this share of the half field of view
constexpr double lag_offset_share = 0.3;  // the lag may put the aim off by at most this share of the distance

Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double length(const Vec3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
bool finite(const Vec3& a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// The same numbers on every platform (the standard's distributions are not): splitmix64.
struct Rng {
    std::uint64_t state;
    double next() {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
    }
    double range(double low, double high) { return low + (high - low) * next(); }
};

bool check_clip(const Clip& clip, std::string& error) {
    if (!(clip.length >= 0.5) || !std::isfinite(clip.length)) {
        error = "The clip is shorter than half a second";
        return false;
    }
    if (clip.path.size() < 2) {
        error = "The clip has no path of the skater";
        return false;
    }
    if (clip.tricks.empty()) {
        error = "The clip has no trick";
        return false;
    }
    for (std::size_t i = 0; i < clip.path.size(); ++i) {
        if (!std::isfinite(clip.path[i].time) || !finite(clip.path[i].position)) {
            error = "The path has a value that is not a number";
            return false;
        }
        if (i > 0 && !(clip.path[i].time > clip.path[i - 1].time)) {
            error = "The path is not in time order";
            return false;
        }
    }
    for (const auto& trick : clip.tricks) {
        if (!(trick.start >= 0.0) || !(trick.end >= trick.start) || !(trick.end <= clip.length)) {
            error = "A trick lies outside the clip";
            return false;
        }
    }
    return true;
}

double trick_weight(const TrickEvent& trick) {
    double weight = 0;
    switch (trick.kind) {
    case TrickKind::flip: weight = 4; break;
    case TrickKind::grind: weight = 3; break;
    case TrickKind::manual: weight = 2; break;
    case TrickKind::ollie: weight = 1; break;
    case TrickKind::bail: weight = 0.5; break;
    }
    return weight + 0.1 * (trick.end - trick.start);
}

// The trick the shot is built round: the one worth most; the first of equals.
std::size_t pick_trick(const std::vector<TrickEvent>& tricks) {
    std::size_t best = 0;
    for (std::size_t i = 1; i < tricks.size(); ++i)
        if (trick_weight(tricks[i]) > trick_weight(tricks[best])) best = i;
    return best;
}

bool in_the_air(TrickKind kind) { return kind == TrickKind::ollie || kind == TrickKind::flip || kind == TrickKind::bail; }

// The skater's heading on the ground at a time: horizontal, unit; along +Z when the skater stands still.
Vec3 heading_at(const std::vector<PathSample>& path, double time) {
    Vec3 step = skater_at(path, time + 0.1) - skater_at(path, time - 0.1);
    step.y = 0;
    double size = length(step);
    if (size < 1e-3) {
        step = path.back().position - path.front().position;
        step.y = 0;
        size = length(step);
    }
    if (size < 1e-3) return {0, 0, 1};
    return step * (1.0 / size);
}

// The lowest the skater is within a window round the trick: the ground the camera stands on.
double ground_near(const std::vector<PathSample>& path, double from, double to) {
    double low = skater_at(path, from).y;
    for (const auto& sample : path)
        if (sample.time >= from && sample.time <= to) low = std::min(low, sample.position.y);
    return std::min(low, skater_at(path, to).y);
}

// How strongly a time is at the trick's heart: 1 at its middle, 0 half a second away.
double punch_weight(double time, double middle) { return std::max(0.0, 1.0 - std::abs(time - middle) / 0.5); }

std::vector<double> key_times(double clip_length, const TrickEvent& trick, bool micro_keys) {
    std::vector<double> raw{0.0, trick.start - 0.6, 0.5 * (trick.start + trick.end), trick.end + 0.3, clip_length};
    for (double& value : raw) value = std::clamp(value, 0.0, clip_length);
    std::sort(raw.begin(), raw.end());
    std::vector<double> times;
    for (const double value : raw)
        if (times.empty() || value - times.back() >= min_key_gap) times.push_back(value);
    if (times.back() != clip_length) {
        if (clip_length - times.back() < min_key_gap && times.size() > 1)
            times.back() = clip_length;
        else
            times.push_back(clip_length);
    }
    if (!micro_keys) return times;
    // A hand keeps moving between the keys that matter: a few small ones on long gaps.
    std::vector<double> filled;
    for (std::size_t i = 0; i < times.size(); ++i) {
        filled.push_back(times[i]);
        if (i + 1 == times.size()) break;
        const double gap = times[i + 1] - times[i];
        if (gap <= 1.4) continue;
        const int inner = static_cast<int>(gap / 0.9);
        for (int k = 1; k <= inner; ++k) filled.push_back(times[i] + gap * k / (inner + 1));
    }
    return filled;
}

// Keeps an angle within half a circle of the one before it.
double unwrap(double angle, double previous) {
    while (angle - previous > pi) angle -= 2 * pi;
    while (angle - previous < -pi) angle += 2 * pi;
    return angle;
}

Plan build(const Clip& clip, const Style& style, std::uint64_t seed, std::size_t trick_index, int side, double scale) {
    const TrickEvent& trick = clip.tricks[trick_index];
    const double humanness = std::clamp(style.humanness, 0.0, 1.0);
    Rng rng{seed};
    const double lateral = rng.range(style.distance_min, style.distance_max) * scale;
    const double ahead = rng.range(style.ahead_min, style.ahead_max);
    const double height = rng.range(style.height_min, style.height_max);
    const double middle = 0.5 * (trick.start + trick.end);

    // The camera's place: beside the trick, a little ahead of it, low.
    const Vec3 heading = heading_at(clip.path, trick.start);
    const Vec3 right{heading.z, 0, -heading.x};
    const Vec3 at_trick = skater_at(clip.path, middle);
    const double ground = ground_near(clip.path, trick.start - 1.5, trick.end + 1.5);
    Vec3 base = at_trick + heading * ahead + right * (static_cast<double>(side) * lateral);
    base.y = ground + height;

    Plan plan;
    plan.trick = trick_index;
    plan.side = side;
    Vec3 drift;
    double previous_yaw = 0;
    bool first = true;
    for (const double time : key_times(clip.length, trick, humanness > 0)) {
        Key key{};
        key.time = time;
        const double weight = punch_weight(time, middle);
        if (humanness > 0) {
            // Drift: small, slow, and never carried far from where the hand started.
            drift.x = std::clamp(0.6 * drift.x + rng.range(-1, 1) * 0.03 * humanness, -0.08, 0.08);
            drift.y = std::clamp(0.6 * drift.y + rng.range(-1, 1) * 0.015 * humanness, -0.04, 0.04);
            drift.z = std::clamp(0.6 * drift.z + rng.range(-1, 1) * 0.03 * humanness, -0.08, 0.08);
        }
        key.position = base + drift;
        key.position.y = std::max(key.position.y, ground + 0.1);

        // The aim: at where the skater was a moment ago, never so far behind that the skater leaves the frame.
        const double lag = humanness * rng.range(0.08, 0.22);
        const Vec3 now = skater_at(clip.path, time);
        Vec3 offset = skater_at(clip.path, std::max(0.0, time - lag)) - now;
        const double distance_now = std::max(distance(key.position, now), 0.1);
        const double limit = lag_offset_share * distance_now;
        const double offset_size = length(offset);
        if (offset_size > limit) offset = offset * (limit / offset_size);
        const double lift = aim_height + (in_the_air(trick.kind) ? airborne_extra * weight : 0.0);
        const Vec3 aim = now + offset + Vec3{0, lift, 0};
        const Vec3 towards = aim - key.position;
        const double flat = std::sqrt(towards.x * towards.x + towards.z * towards.z);
        key.yaw = std::atan2(towards.x, towards.z);
        key.pitch = std::atan2(towards.y, std::max(flat, 1e-6));
        if (!first) key.yaw = unwrap(key.yaw, previous_yaw);
        previous_yaw = key.yaw;
        first = false;

        key.roll = humanness * (static_cast<double>(side) * 0.02 + rng.range(-0.01, 0.01));
        double fov = style.fov_degrees - style.punch_in_degrees * weight + rng.range(-1, 1) * 1.2 * humanness;
        if (plan.keys.empty()) fov += 2.0 * humanness; // opens a touch wider
        key.fov_degrees = std::clamp(fov, fov_min_degrees, fov_max_degrees);
        plan.keys.push_back(key);
    }
    return plan;
}
} // namespace

double distance(const Vec3& a, const Vec3& b) noexcept { return length(a - b); }

Vec3 skater_at(const std::vector<PathSample>& path, double time) noexcept {
    if (time <= path.front().time) return path.front().position;
    if (time >= path.back().time) return path.back().position;
    const auto after = std::upper_bound(path.begin(), path.end(), time,
                                        [](double value, const PathSample& sample) { return value < sample.time; });
    const PathSample& b = *after;
    const PathSample& a = *(after - 1);
    const double share = (time - a.time) / (b.time - a.time);
    return a.position + (b.position - a.position) * share;
}

Vec3 view_direction(const Key& key) noexcept {
    return {std::sin(key.yaw) * std::cos(key.pitch), std::sin(key.pitch), std::cos(key.yaw) * std::cos(key.pitch)};
}

bool make_plan(const Clip& clip, const Style& style, std::uint64_t seed, Plan& out, std::string& error) {
    out = {};
    error.clear();
    if (!check_clip(clip, error)) return false;
    const std::size_t trick = pick_trick(clip.tricks);
    Rng first_rng{seed ^ 0xA5A5A5A5A5A5A5A5ull};
    const int first_side = first_rng.next() < 0.5 ? 1 : -1;
    // A path that curves into the camera is met by trying the other side and standing further away.
    for (const double scale : {1.0, 1.4, 1.8}) {
        for (const int side : {first_side, -first_side}) {
            Plan plan = build(clip, style, seed, trick, side, scale);
            std::string reason;
            if (!validate(plan, clip, reason)) continue;
            double nearest = 1e300;
            for (double time = 0; time <= clip.length; time += 0.05) {
                // (the camera's place between keys is a line from key to key)
                const auto after = std::upper_bound(plan.keys.begin(), plan.keys.end(), time,
                                                    [](double value, const Key& key) { return value < key.time; });
                Vec3 camera = plan.keys.back().position;
                if (after == plan.keys.begin())
                    camera = plan.keys.front().position;
                else if (after != plan.keys.end()) {
                    const Key& b = *after;
                    const Key& a = *(after - 1);
                    camera = a.position + (b.position - a.position) * ((time - a.time) / (b.time - a.time));
                }
                nearest = std::min(nearest, distance(camera, skater_at(clip.path, time)));
            }
            if (nearest < style.clearance_metres) continue;
            out = std::move(plan);
            return true;
        }
    }
    error = "No camera position keeps the skater in frame and clear of the camera";
    return false;
}

bool validate(const Plan& plan, const Clip& clip, std::string& error) {
    error.clear();
    if (plan.keys.size() < 2) {
        error = "The plan has fewer than two keys";
        return false;
    }
    if (clip.path.empty()) {
        error = "The clip has no path of the skater";
        return false;
    }
    for (std::size_t i = 0; i < plan.keys.size(); ++i) {
        const Key& key = plan.keys[i];
        if (!finite(key.position) || !std::isfinite(key.yaw) || !std::isfinite(key.pitch) || !std::isfinite(key.roll) ||
            !std::isfinite(key.fov_degrees) || !std::isfinite(key.time)) {
            error = "A key has a value that is not a number";
            return false;
        }
        if (key.time < 0 || key.time > clip.length + 1e-9) {
            error = "A key lies outside the clip";
            return false;
        }
        if (i > 0) {
            const Key& before = plan.keys[i - 1];
            if (key.time - before.time < min_key_gap * 0.99) {
                error = "Key times don't rise by the least gap";
                return false;
            }
            if (std::abs(key.yaw - before.yaw) > pi) {
                error = "The yaw turns more than half a circle between two keys";
                return false;
            }
        }
        if (key.fov_degrees < fov_min_degrees || key.fov_degrees > fov_max_degrees) {
            error = "The field of view is out of range";
            return false;
        }
        if (std::abs(key.pitch) > 80 * degrees) {
            error = "The camera points too far up or down";
            return false;
        }
        const Vec3 subject = skater_at(clip.path, key.time);
        const double away = distance(key.position, subject);
        if (away < hard_clearance_metres) {
            error = "The skater is too close to the camera";
            return false;
        }
        const Vec3 to_subject = (subject + Vec3{0, aim_height, 0}) - key.position;
        const double size = length(to_subject);
        const double cosine = std::clamp(dot(view_direction(key), to_subject) / size, -1.0, 1.0);
        if (std::acos(cosine) > frame_fraction * 0.5 * key.fov_degrees * degrees) {
            error = "The skater is not in the middle of the frame";
            return false;
        }
    }
    if (plan.keys.front().time != 0.0 || plan.keys.back().time != clip.length) {
        error = "The plan does not run from the clip's start to its end";
        return false;
    }
    return true;
}
} // namespace dingosdk::auto_camera
