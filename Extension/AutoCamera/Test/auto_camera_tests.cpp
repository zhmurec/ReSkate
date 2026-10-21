// Auto camera plan, apart from the game: the same seed plans the same shot, every key is inside the clip
// and the keys rise, the skater stays in frame, the camera is low and wide with a push-in on the trick,
// a human differs from a tripod only a little, and a clip that can't be shot is refused with a reason.
#include "Extension/AutoCamera/auto_camera_plan.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>

using namespace dingosdk::auto_camera;
namespace {
constexpr double pi = std::numbers::pi;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

// The skater rolls along +Z at 5 m/s for `length` seconds; during an ollie, flip or bail it is in the air.
Clip straight_clip(double length, std::initializer_list<TrickEvent> tricks) {
    Clip clip;
    clip.length = length;
    clip.tricks = tricks;
    for (double time = 0; time <= length + 1e-9; time += 0.05) {
        double height = 0;
        for (const auto& trick : clip.tricks) {
            if (time >= trick.start && time <= trick.end && trick.kind != TrickKind::grind && trick.kind != TrickKind::manual) {
                const double share = (time - trick.start) / std::max(trick.end - trick.start, 1e-6);
                height = std::max(height, 0.6 * 4 * share * (1 - share));
            }
        }
        clip.path.push_back({time, {0, height, 5.0 * time}});
    }
    return clip;
}

Plan planned(const Clip& clip, const Style& style, std::uint64_t seed) {
    Plan plan;
    std::string error;
    check(make_plan(clip, style, seed, plan, error), ("The clip is shot: " + error).c_str());
    return plan;
}

void the_same_seed_plans_the_same_shot() {
    const Clip clip = straight_clip(4.0, {{TrickKind::flip, 1.5, 2.3}});
    const Plan a = planned(clip, Style{}, 7), b = planned(clip, Style{}, 7), c = planned(clip, Style{}, 8);
    check(a.keys.size() == b.keys.size(), "The same seed gives the same number of keys");
    for (std::size_t i = 0; i < a.keys.size(); ++i)
        check(a.keys[i].time == b.keys[i].time && a.keys[i].position.x == b.keys[i].position.x &&
                  a.keys[i].yaw == b.keys[i].yaw && a.keys[i].fov_degrees == b.keys[i].fov_degrees,
              "and the same keys");
    bool differs = a.keys.size() != c.keys.size();
    for (std::size_t i = 0; !differs && i < a.keys.size(); ++i) differs = a.keys[i].position.x != c.keys[i].position.x;
    check(differs, "Another seed gives another shot");
}

void every_plan_is_valid_and_inside_the_clip() {
    const Clip clips[] = {
        straight_clip(4.0, {{TrickKind::flip, 1.5, 2.3}}),
        straight_clip(2.0, {{TrickKind::grind, 0.0, 1.0}}),   // a trick right at the start
        straight_clip(3.0, {{TrickKind::ollie, 2.4, 3.0}}),   // and one that ends with the clip
        straight_clip(0.6, {{TrickKind::manual, 0.2, 0.4}}),  // a very short clip
        straight_clip(8.0, {{TrickKind::ollie, 1.0, 1.4}, {TrickKind::grind, 3.0, 4.5}, {TrickKind::bail, 6.0, 6.8}}),
    };
    for (const Clip& clip : clips) {
        for (std::uint64_t seed = 0; seed < 100; ++seed) {
            const Plan plan = planned(clip, Style{}, seed);
            std::string error;
            check(validate(plan, clip, error), ("A plan is valid: " + error).c_str());
            check(plan.keys.front().time == 0.0 && plan.keys.back().time == clip.length, "A plan runs the whole clip");
            for (std::size_t i = 1; i < plan.keys.size(); ++i)
                check(plan.keys[i].time - plan.keys[i - 1].time >= min_key_gap * 0.99, "Key times rise by the least gap");
        }
    }
}

void the_skater_stays_in_frame() {
    const Clip clip = straight_clip(5.0, {{TrickKind::flip, 2.0, 2.8}});
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
        const Plan plan = planned(clip, Style{}, seed);
        for (const Key& key : plan.keys) {
            const Vec3 subject = skater_at(clip.path, key.time);
            const Vec3 view = view_direction(key);
            const Vec3 to{subject.x - key.position.x, subject.y + 0.55 - key.position.y, subject.z - key.position.z};
            const double size = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
            const double angle = std::acos(std::clamp((view.x * to.x + view.y * to.y + view.z * to.z) / size, -1.0, 1.0));
            check(angle < 0.5 * key.fov_degrees * pi / 180.0 * 0.6 + 1e-9, "The skater is in the middle of the frame at every key");
        }
    }
}

void the_shot_is_low_wide_and_pushes_in() {
    const Clip clip = straight_clip(5.0, {{TrickKind::flip, 2.0, 2.8}});
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        const Plan plan = planned(clip, Style{}, seed);
        double mean = 0;
        for (const Key& key : plan.keys) {
            check(key.position.y >= 0.1 - 1e-9 && key.position.y <= 0.40 + 0.04 + 1e-9, "The camera stays low, near the ground");
            check(key.fov_degrees >= fov_min_degrees && key.fov_degrees <= fov_max_degrees, "The field of view stays in range");
            mean += key.fov_degrees;
        }
        mean /= static_cast<double>(plan.keys.size());
        check(mean > 90.0, "The look is wide, a fisheye's");
        const Key* heart = &plan.keys[0];
        for (const Key& key : plan.keys)
            if (std::abs(key.time - 2.4) < std::abs(heart->time - 2.4)) heart = &key;
        check(plan.keys.front().fov_degrees - heart->fov_degrees > 5.0, "The camera pushes in on the trick");
    }
}

void a_tripod_holds_still_and_a_hand_only_drifts() {
    const Clip clip = straight_clip(6.0, {{TrickKind::grind, 2.5, 3.5}});
    Style tripod;
    tripod.humanness = 0;
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        const Plan fixed = planned(clip, tripod, seed);
        for (const Key& key : fixed.keys) {
            check(key.position.x == fixed.keys[0].position.x && key.position.y == fixed.keys[0].position.y &&
                      key.position.z == fixed.keys[0].position.z,
                  "A tripod holds one position");
            check(key.roll == 0.0, "and doesn't lean");
        }
        check(fixed.keys.size() <= 5, "A tripod has only the keys that matter");
        const Plan human = planned(clip, Style{}, seed);
        check(human.keys.size() > fixed.keys.size(), "A hand adds small keys between the ones that matter on long gaps");
        for (const Key& key : human.keys) {
            const double away = std::sqrt(std::pow(key.position.x - fixed.keys[0].position.x, 2) +
                                          std::pow(key.position.y - fixed.keys[0].position.y, 2) +
                                          std::pow(key.position.z - fixed.keys[0].position.z, 2));
            check(away < 0.15, "A hand drifts only a little from where it stood");
            check(std::abs(key.roll) < 0.06, "and leans only a little");
        }
    }
}

void the_shot_is_built_round_the_best_trick() {
    const Clip clip = straight_clip(8.0, {{TrickKind::ollie, 1.0, 1.4}, {TrickKind::flip, 3.0, 3.8}, {TrickKind::grind, 5.0, 6.0}});
    check(planned(clip, Style{}, 1).trick == 1, "A flip is worth more than an ollie and a grind");
    const Clip bails = straight_clip(5.0, {{TrickKind::bail, 1.0, 1.8}, {TrickKind::manual, 3.0, 4.0}});
    check(planned(bails, Style{}, 1).trick == 1, "A manual is worth more than a bail");
}

void angles_never_jump_round_the_circle() {
    // The skater circles the camera's end of the park: the direction to it passes through the ±π seam.
    Clip clip;
    clip.length = 6.0;
    clip.tricks = {{TrickKind::grind, 2.0, 3.0}};
    for (double time = 0; time <= clip.length + 1e-9; time += 0.05) {
        const double angle = time * 0.9;
        clip.path.push_back({time, {6.0 * std::cos(angle), 0, 6.0 * std::sin(angle)}});
    }
    int shot = 0;
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
        Plan plan;
        std::string error;
        if (!make_plan(clip, Style{}, seed, plan, error)) continue;
        ++shot;
        for (std::size_t i = 1; i < plan.keys.size(); ++i)
            check(std::abs(plan.keys[i].yaw - plan.keys[i - 1].yaw) <= pi, "The yaw never turns more than half a circle between keys");
    }
    check(shot > 0, "A skater circling can be shot");
}

void a_clip_that_cannot_be_shot_is_refused_with_a_reason() {
    Plan plan;
    std::string error;
    Clip clip = straight_clip(4.0, {{TrickKind::flip, 1.5, 2.3}});

    Clip no_path = clip;
    no_path.path.clear();
    check(!make_plan(no_path, Style{}, 1, plan, error) && !error.empty() && plan.keys.empty(), "No path is refused");

    Clip no_trick = clip;
    no_trick.tricks.clear();
    check(!make_plan(no_trick, Style{}, 1, plan, error) && !error.empty(), "No trick is refused");

    Clip unsorted = clip;
    std::swap(unsorted.path[3], unsorted.path[4]);
    check(!make_plan(unsorted, Style{}, 1, plan, error), "A path out of time order is refused");

    Clip outside = clip;
    outside.tricks[0].end = 9.0;
    check(!make_plan(outside, Style{}, 1, plan, error), "A trick outside the clip is refused");

    Clip tiny = straight_clip(0.3, {{TrickKind::ollie, 0.1, 0.2}});
    check(!make_plan(tiny, Style{}, 1, plan, error), "A clip under half a second is refused");

    Style far;
    far.clearance_metres = 1000;
    check(!make_plan(clip, far, 1, plan, error) && plan.keys.empty() && !error.empty(),
          "When no camera position is clear of the skater there is no plan, and a reason");
}

void validate_catches_what_the_game_cannot_play() {
    const Clip clip = straight_clip(4.0, {{TrickKind::flip, 1.5, 2.3}});
    const Plan good = planned(clip, Style{}, 3);
    std::string error;
    check(validate(good, clip, error), "A good plan is valid");

    Plan backwards = good;
    std::swap(backwards.keys[1], backwards.keys[2]);
    check(!validate(backwards, clip, error), "Keys out of order are caught");

    Plan jump = good;
    jump.keys[1].yaw += 4.0;
    check(!validate(jump, clip, error), "A turn of more than half a circle is caught");

    Plan blind = good;
    blind.keys[1].yaw += 1.2;
    check(!validate(blind, clip, error), "A camera looking away from the skater is caught");

    Plan odd = good;
    odd.keys[1].fov_degrees = 170;
    check(!validate(odd, clip, error), "A field of view out of range is caught");

    Plan nan = good;
    nan.keys[1].position.x = std::nan("");
    check(!validate(nan, clip, error), "A value that is not a number is caught");
}
} // namespace

int main() {
    try {
        the_same_seed_plans_the_same_shot();
        every_plan_is_valid_and_inside_the_clip();
        the_skater_stays_in_frame();
        the_shot_is_low_wide_and_pushes_in();
        a_tripod_holds_still_and_a_hand_only_drifts();
        the_shot_is_built_round_the_best_trick();
        angles_never_jump_round_the_circle();
        a_clip_that_cannot_be_shot_is_refused_with_a_reason();
        validate_catches_what_the_game_cannot_play();
        std::cout << "auto camera tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "auto camera test failed: " << error.what() << '\n';
        return 1;
    }
}
