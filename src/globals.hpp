#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>

inline HANDLE PHANDLE = nullptr;

// Access config values through a function that returns a reference to a
// function-local static. This avoids global SP<> initialization-order issues
// in shared libraries — the struct is constructed on first call and persists.
struct FocusZConfig {
    SP<Config::Values::CBoolValue>   enabled;
    SP<Config::Values::CBoolValue>   stacking;
    SP<Config::Values::CIntValue>    maxLayers;
    SP<Config::Values::CFloatValue>  layer1Scale;
    SP<Config::Values::CFloatValue>  layer2Scale;
    SP<Config::Values::CFloatValue>  layer1Opacity;
    SP<Config::Values::CFloatValue>  layer2Opacity;
    SP<Config::Values::CBoolValue>   layer1Blur;
    SP<Config::Values::CBoolValue>   layer2Blur;
    SP<Config::Values::CFloatValue>  animationSpeed;
    SP<Config::Values::CFloatValue>  wallpaperDim;
    SP<Config::Values::CFloatValue>  wallpaperZoom;
    SP<Config::Values::CFloatValue>  canvasZoomFloor;
    SP<Config::Values::CFloatValue>  canvasDimFloor;
    SP<Config::Values::CFloatValue>  canvasPlateAlpha;
    SP<Config::Values::CFloatValue>  canvasPlateAlphaMax;
    SP<Config::Values::CFloatValue>  canvasShadowBoost;
    SP<Config::Values::CBoolValue>   canvasPlate;
    SP<Config::Values::CFloatValue>  plateFrost;
    SP<Config::Values::CBoolValue>   cardBorder;
    SP<Config::Values::CIntValue>    borderWidth;
    SP<Config::Values::CIntValue>    borderColor;
    SP<Config::Values::CFloatValue>  frontScale;
    SP<Config::Values::CBoolValue>   edgeScatter;
    SP<Config::Values::CBoolValue>   scatterReshuffle;
    SP<Config::Values::CFloatValue>  peekMin;
    SP<Config::Values::CFloatValue>  peekMax;
    SP<Config::Values::CBoolValue>   centerScale;
    SP<Config::Values::CBoolValue>   cardFrost;
    SP<Config::Values::CFloatValue>  cardFrostStrength;
    SP<Config::Values::CBoolValue>   debug;
};

inline FocusZConfig& cfg() {
    static FocusZConfig instance;
    return instance;
}
