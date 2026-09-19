#pragma once

namespace ivx {
namespace pitch {

constexpr int kParamMin = 26;
constexpr int kParamMax = 100;
constexpr int kHertzMin = 30;
constexpr int kHertzMax = 250;

constexpr int clamp_param(int param)
{
    return param < kParamMin ? kParamMin : (param > kParamMax ? kParamMax : param);
}

constexpr int clamp_hertz(int hertz)
{
    return hertz < kHertzMin ? kHertzMin : (hertz > kHertzMax ? kHertzMax : hertz);
}

constexpr int reported_hertz(int param)
{
    return kHertzMin + 110 * (clamp_param(param) - kParamMin) / 37;
}

constexpr int selected_param(int hertz)
{
    return kParamMin + 37 * (clamp_hertz(hertz) - kHertzMin) / 110;
}

constexpr int spoken_param(int param)
{
    return selected_param(reported_hertz(param));
}

constexpr int param_behind(int hertz)
{
    return clamp_param(kParamMin + (37 * (hertz - kHertzMin + 1) - 1) / 110);
}

constexpr int tag_value(int param)
{
    return (22 * clamp_param(param) + 16) / 10;
}

constexpr int param_from_tag(int value)
{
    const int param = value * 5 / 11;
    return param < 0 ? 0 : (param > 100 ? 100 : param);
}

namespace detail {

constexpr bool every_step_survives()
{
    for (int param = kParamMin; param <= kParamMax; ++param) {
        const bool lossless = param == 26 || param == 63 || param == 100;
        if (param_behind(reported_hertz(param)) != param ||
            (param > kParamMin && param_behind(reported_hertz(param) - 1) != param - 1) ||
            spoken_param(param) != (lossless ? param : param - 1)) {
            return false;
        }
        const int tag = tag_value(param);
        if (param_from_tag(tag) != param || (tag * 5) % 11 == 0) {
            return false;
        }
    }
    return true;
}

}  // namespace detail

static_assert(detail::every_step_survives(),
              "a Pitch did not survive the way to the engine and back");
static_assert(reported_hertz(50) == 101 && spoken_param(50) == 49 && param_from_tag(101) == 45,
              "the arithmetic no longer describes the engine it was taken from");

}  // namespace pitch
}  // namespace ivx
