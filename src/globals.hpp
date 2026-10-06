#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>

inline HANDLE PHANDLE = nullptr;

// Access config values through a function that returns a reference to a
// function-local static. This avoids global SP<> initialization-order issues
// in shared libraries — the struct is constructed on first call and persists.
// Every value here is registered in PLUGIN_INIT AND read at runtime. Keys that
// only existed to be registered (canvas, frost, borders, blur, animation speed,
// center_scale) were removed: a config key nobody reads is a lie to the user.
struct FocusZConfig {
    SP<Config::Values::CBoolValue>   enabled;
    SP<Config::Values::CBoolValue>   stacking;
    SP<Config::Values::CIntValue>    maxLayers;
    SP<Config::Values::CFloatValue>  layer1Scale;
    SP<Config::Values::CFloatValue>  layer2Scale;
    SP<Config::Values::CFloatValue>  layer1Opacity;
    SP<Config::Values::CFloatValue>  layer2Opacity;
    SP<Config::Values::CFloatValue>  frontScale;
    SP<Config::Values::CBoolValue>   edgeScatter;
    SP<Config::Values::CBoolValue>   scatterReshuffle;
    SP<Config::Values::CFloatValue>  peekMin;
    SP<Config::Values::CFloatValue>  peekMax;
    SP<Config::Values::CBoolValue>   debug;
};

inline FocusZConfig& cfg() {
    static FocusZConfig instance;
    return instance;
}
