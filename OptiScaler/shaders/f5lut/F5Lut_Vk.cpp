#include "pch.h"

#include "F5Lut_Vk.h"

#include <dlssnr/DlssNr_VkExtensions.h>

bool F5Lut_Vk::ShouldRun(bool InLinearHdr)
{
    if (!_init)
        return false;

    const bool run = PrepareChain(InLinearHdr);
    if (!_chain.wanted && _pass && _pass->HoldsImage())
    {
        // Earlier frames may still read the lattice; one drain, then every frame without a stack costs nothing.
        vkDeviceWaitIdle(_device);
        _pass->ReleaseLutImage();
    }
    return run;
}

VkImageInfo F5Lut_Vk::Setup(VkCommandBuffer InCmdBuffer, const VkImageInfo& InNextOutput, VkImageUsageFlags InUsage)
{
    if (!_anyFormat && InNextOutput.Format != VK_FORMAT_R16G16B16A16_SFLOAT)
    {
        Report(false, "Not applied: this output's format needs a Vulkan device feature the game did not turn on.");
        return {};
    }

    if (!_pass)
        _pass = std::make_unique<DlssNr_LutVk>(_device, _physicalDevice, _anyFormat);

    const DlssNr::Lut3D& lut = _chain.baked;
    if (_pass->Ready() && _pass->NeedsDrainFor(lut.size, _chain.key))
        vkDeviceWaitIdle(_device);

    if (!_pass->Ready() || !_pass->EnsureLutImage(InCmdBuffer, lut.rgb.data(), lut.size, _chain.key))
    {
        Report(false, "could not upload the LUT to the GPU");
        return {};
    }

    if (!CreateImageResource(InNextOutput.Width, InNextOutput.Height, InNextOutput.Format, InUsage))
        return {};

    SetImageLayout(InCmdBuffer, GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                   InNextOutput.SubresourceRange);

    VkImageInfo info = InNextOutput;
    info.Image = GetImage();
    info.ImageView = GetImageView();
    return info;
}

bool F5Lut_Vk::Dispatch(VkCommandBuffer InCmdBuffer, const VkImageInfo& InResource, const VkImageInfo& OutResource,
                        bool InLinearHdr)
{
    if (!_init || !_pass || InCmdBuffer == VK_NULL_HANDLE)
        return false;

    SetImageLayout(InCmdBuffer, InResource.Image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   InResource.SubresourceRange);

    const DlssNrLutConstants constants =
        ChainConstants(OutResource.Width, OutResource.Height, InLinearHdr, false, 1.0f);
    const bool graded = _pass->Dispatch(InCmdBuffer, InResource.ImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                        OutResource.ImageView, constants);
    Report(graded);
    return graded;
}

F5Lut_Vk::F5Lut_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    if (InDevice == VK_NULL_HANDLE || InPhysicalDevice == VK_NULL_HANDLE)
        return;

    _anyFormat = DlssNr::VkExt::WritesWithoutFormat(InDevice);
    _init = true;
}

F5Lut_Vk::~F5Lut_Vk()
{
    if (!_pass || State::Instance().isShuttingDown)
    {
        _pass.release();
        return;
    }

    // The lattice and rings may still be read by frames in flight; the pass frees them as it goes.
    vkDeviceWaitIdle(_device);
}
