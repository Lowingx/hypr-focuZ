#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>

inline HANDLE PHANDLE = nullptr;

inline SP<Config::Values::CBoolValue>  g_bEnabled;
inline SP<Config::Values::CIntValue>   g_iMaxLayers;
inline SP<Config::Values::CFloatValue> g_fLayer1Scale;
inline SP<Config::Values::CFloatValue> g_fLayer2Scale;
inline SP<Config::Values::CFloatValue> g_fLayer1Opacity;
inline SP<Config::Values::CFloatValue> g_fLayer2Opacity;
inline SP<Config::Values::CBoolValue>  g_bLayer1Blur;
inline SP<Config::Values::CBoolValue>  g_bLayer2Blur;
inline SP<Config::Values::CFloatValue> g_fAnimationSpeed;
inline SP<Config::Values::CBoolValue>  g_bCenterScale;
inline SP<Config::Values::CBoolValue>  g_bDebug;
