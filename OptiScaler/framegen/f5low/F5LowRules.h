#pragma once

#include <optional>
#include <string_view>

// FG Output F5Low: our xefg_cuda.dll from the plugins folder (it implements XeFG's API, so OptiScaler drives it
// through the XeFG output code). The Frame Generator choice says who makes the frames inside it. Pure rules,
// host-tested in tests/f5low_output_smoke.cpp; F5Low.cpp feeds them the ini, the GPU and the folder.
//
// Three files in plugins: xefg_cuda.dll, xefg_cuda.ini and Intel's signed libxess_fg.dll (the library xefg_cuda
// hands over to where it cannot run, and the "libxess" generator later). Not in the release zip yet
// (package_release.ps1 -XefgCudaKit can stage them); without xefg_cuda.dll F5Low is not available.
namespace F5Low
{
inline constexpr wchar_t kLibraryFile[] = L"xefg_cuda.dll";

// xefg_cuda reads this with GetEnvironmentVariable when it creates each context; it wins over its own ini.
inline constexpr wchar_t kGeneratorEnv[] = L"XEFG_CUDA_GENERATOR";

// [F5Low] FrameGenerator. Only XeFG Tensor exists so far; DLSS-G and libxess are planned (xefg_cuda's
// Generator=dlssg / intel).
enum class Generator
{
    XefgTensor, // xefg_cuda's own generator on the tensor cores (Generator=own)
};

inline constexpr Generator kDefaultGenerator = Generator::XefgTensor;

// nullopt: the default. valid turns false for text that is not a generator; planned generators read as not valid yet.
inline std::optional<Generator> ParseGenerator(std::string_view text, bool& valid)
{
    const auto equals = [text](std::string_view word)
    {
        if (text.size() != word.size())
            return false;
        for (size_t index = 0; index < text.size(); ++index)
        {
            const char c =
                text[index] >= 'A' && text[index] <= 'Z' ? static_cast<char>(text[index] - 'A' + 'a') : text[index];
            if (c != word[index])
                return false;
        }
        return true;
    };

    valid = true;
    if (text.empty() || equals("auto"))
        return std::nullopt;
    if (equals("xefg_tensor"))
        return Generator::XefgTensor;
    valid = false;
    return std::nullopt;
}

// The text saved for a value: nullopt (the default) is saved as auto.
inline const char* GeneratorText(std::optional<Generator> generator)
{
    if (!generator.has_value())
        return "auto";
    switch (generator.value())
    {
    case Generator::XefgTensor:
        return "xefg_tensor";
    }
    return "auto";
}

// What the menu shows.
inline const char* GeneratorLabel(Generator generator)
{
    switch (generator)
    {
    case Generator::XefgTensor:
        return "XeFG Tensor";
    }
    return "?";
}

// The XEFG_CUDA_GENERATOR value for a generator.
inline const wchar_t* GeneratorEnvValue(Generator generator)
{
    switch (generator)
    {
    case Generator::XefgTensor:
        return L"own";
    }
    return L"own";
}

// The most interpolated frames F5Low offers: what its library reports (at least 1), capped by [XeFG]
// MaxInterpolations (1-5, 5 = up to 6x). Unlike the XeFG unlock it never goes above what the library reports.
inline int MaxInterpolations(unsigned reported, int configured)
{
    const int library = reported < 1 ? 1 : static_cast<int>(reported);
    const int cap = configured < 1 ? 1 : (configured > 5 ? 5 : configured);
    return library < cap ? library : cap;
}

// Whether F5Low can be offered, and why not (menu tooltip, log). XeFG Tensor needs CUDA, so an NVIDIA GPU.
struct Availability
{
    bool available = false;
    const char* reason = "";
};

inline Availability CheckAvailable(bool nvidia, bool libraryFound)
{
    if (!nvidia)
        return { false, "F5Low frame generation needs an NVIDIA GPU (XeFG Tensor runs on CUDA)" };
    if (!libraryFound)
        return { false, "xefg_cuda.dll is not in the plugins folder" };
    return { true, "" };
}
} // namespace F5Low
