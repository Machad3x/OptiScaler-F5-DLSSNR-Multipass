#pragma once

#include "F5LowRules.h"

#include <Windows.h>

#include <string>

// FG Output F5Low at run time: where its library is, whether it can run here, and which generator it uses.
namespace F5Low
{
// The startup nvapi check's answer; DXGI cannot be asked inside DLL_PROCESS_ATTACH, where XeFG is first loaded.
void SetStartupNvidia(bool nvidia);

// <plugins folder>\xefg_cuda.dll
std::wstring LibraryPath();

// The Intel libxess_fg.dll OptiScaler's XeFG output would use (XeFGPath, else OptiScaler's folder), for xefg_cuda;
// empty when there is none
std::wstring IntelLibraryPath();

// Whether F5Low can run on this PC (NVIDIA GPU, library present), with the reason when not.
Availability Available();

// Loads xefg_cuda.dll from the plugins folder (XeFGPath is the XeFG output's, not used here). Null when F5Low is not
// available or the load failed; the reason is logged.
HMODULE LoadLibraryForOutput();

// Tells xefg_cuda which generator to run (XEFG_CUDA_GENERATOR) before XeFG's API creates an F5Low context. Only F5Low
// sets it: the XeFG output with a hand-set XeFGPath to xefg_cuda keeps following its own xefg_cuda.ini.
void ApplyGenerator();
} // namespace F5Low
