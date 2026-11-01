#pragma once

#include "F5LowRules.h"

#include <Windows.h>

#include <string>

// FG Output F5Low at run time: where its library is, whether it can run here, and which generator it uses.
namespace F5Low
{
// The startup nvapi check's answer; DXGI cannot be asked inside DLL_PROCESS_ATTACH, where XeFG is first loaded.
void SetStartupNvidia(bool nvidia);

// <plugins folder>\f5low.dll
std::wstring LibraryPath();

// The Intel libxess_fg.dll OptiScaler's XeFG output would use (XeFGPath, else OptiScaler's folder), for f5low.dll;
// empty when there is none
std::wstring IntelLibraryPath();

// What f5low.dll reports (f5lowGetStatus) with its one-line reason. Asks the module LoadLibraryForOutput loaded;
// NotLoaded before that, Unknown when that f5low.dll has no such export.
struct Status
{
    RunState state = RunState::NotLoaded;
    std::string reason;
};
Status QueryStatus();

// Logs QueryStatus once per change (call after a context was made).
void LogStatus();

// A once-per-session toast for an F5Low that cannot generate frames, plus the log line.
void ReportFailure(const char* what);

// Whether F5Low can run on this PC (NVIDIA GPU, f5low.dll and libxess_fg.dll present), with the reason when not.
Availability Available();

// Loads f5low.dll from the plugins folder (XeFGPath is the XeFG output's, not used here). Null when F5Low is not
// available or the load failed; the reason is logged.
HMODULE LoadLibraryForOutput();

// Tells f5low.dll which generator to run (XEFG_CUDA_GENERATOR) before XeFG's API creates an F5Low context. Only F5Low
// sets it: the XeFG output with a hand-set XeFGPath to f5low.dll keeps following its own f5low.ini.
void ApplyGenerator();
} // namespace F5Low
