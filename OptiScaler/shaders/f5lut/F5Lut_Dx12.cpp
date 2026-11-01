#include "pch.h"

#include "F5Lut_Dx12.h"

#include <shaders/dlssnr/precompile/dlssnr_lut_Shader.h>
#include <dlssnr/DlssNr_LutPack.h>
#include <Util.h>

bool F5Lut_Dx12::ShouldRun(bool InLinearHdr)
{
    if (!_init)
        return false;

    const bool run = PrepareChain(InLinearHdr);
    if (!_chain.wanted && _lutTexture != nullptr)
        ReleaseLutTexture();
    return run;
}

bool F5Lut_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState)
{
    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    auto result = Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer, resourceFlags);

    if (result)
    {
        _buffer->SetName(L"F5LUT_Buffer");
        _bufferState = InState;
    }

    return result;
}

void F5Lut_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

void F5Lut_Dx12::ReleaseLutTexture()
{
    // Frames already recorded may still sample it.
    Util::DelayedRelease(_lutTexture);
    _lutTexture = nullptr;
    _lutKey.clear();
}

bool F5Lut_Dx12::EnsureLutTexture(ID3D12GraphicsCommandList* InCmdList)
{
    const DlssNr::Lut3D& lut = _chain.baked;
    if (lut.size < 2)
        return false;

    // Keyed on the bake, not the size: two stacks of the same lattice size are different textures.
    if (_lutTexture != nullptr && _lutKey == _chain.key)
        return true;

    if (_lutTexture != nullptr)
        ReleaseLutTexture();

    const int size = lut.size;
    const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto texDesc =
        CD3DX12_RESOURCE_DESC::Tex3D(DXGI_FORMAT_R32G32B32A32_FLOAT, (UINT64) size, (UINT) size, (UINT16) size, 1);

    if (FAILED(_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&_lutTexture))))
    {
        LOG_ERROR("[{0}] the LUT texture could not be allocated ({1}x{1}x{1})", _name, size);
        _lutTexture = nullptr;
        return false;
    }
    _lutTexture->SetName(L"F5LUT_Lattice");

    // The destination's row pitch from GetCopyableFootprints, not size * 8 bytes.
    UINT64 totalBytes = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
    _device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);

    const auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(totalBytes);
    ID3D12Resource* upload = nullptr;
    uint8_t* mapped = nullptr;
    const CD3DX12_RANGE readRange(0, 0);

    if (FAILED(_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))) ||
        FAILED(upload->Map(0, &readRange, reinterpret_cast<void**>(&mapped))))
    {
        LOG_ERROR("[{0}] the LUT texture's upload buffer could not be made", _name);
        SAFE_RELEASE(upload);
        ReleaseLutTexture();
        return false;
    }

    DlssNrLutPack::PackFloat4(lut.rgb.data(), size, mapped, (size_t) footprint.Footprint.RowPitch,
                              (size_t) footprint.Footprint.RowPitch * (size_t) size);
    upload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst {};
    dst.pResource = _lutTexture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src {};
    src.pResource = upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    InCmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        _lutTexture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    InCmdList->ResourceBarrier(1, &barrier);

    // The copy is only recorded: the upload buffer goes once the GPU is past it.
    Util::DelayedRelease(upload);

    _lutKey = _chain.key;
    return true;
}

void F5Lut_Dx12::CopyThrough(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                             ID3D12Resource* OutResource)
{
    SetBufferState(InCmdList, D3D12_RESOURCE_STATE_COPY_SOURCE);

    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        OutResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    InCmdList->ResourceBarrier(1, &barrier);

    InCmdList->CopyResource(OutResource, InResource);

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(OutResource, D3D12_RESOURCE_STATE_COPY_DEST,
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    InCmdList->ResourceBarrier(1, &barrier);

    SetBufferState(InCmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

bool F5Lut_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* OutResource,
                          bool InLinearHdr, ID3D12Resource* InExposure, float InPreExposure)
{
    if (!_init || _device == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    ScopedGpuTime_Dx12 scopedGpuTime(GpuTime.get(), InCmdList);

    if (!EnsureLutTexture(InCmdList))
    {
        CopyThrough(InCmdList, InResource, OutResource);
        Report(false, "could not upload the LUT to the GPU");
        return true;
    }

    _counter = (_counter + 1) % F5LUT_NUM_OF_HEAPS;
    FrameDescriptorHeap& heap = _frameHeaps[_counter];

    const auto outDesc = OutResource->GetDesc();
    const DlssNrLutConstants constants =
        ChainConstants((unsigned int) outDesc.Width, outDesc.Height, InLinearHdr, InExposure != nullptr, InPreExposure);

    CreateShaderResourceView(_device, InResource, heap.GetSrvCPU(0));
    CreateShaderResourceView(_device, _lutTexture, heap.GetSrvCPU(1));
    // The source stands in when there is no exposure; the constants then say not to read it.
    CreateShaderResourceView(_device, InExposure != nullptr ? InExposure : InResource, heap.GetSrvCPU(2));
    CreateUnorderedAccessView(_device, OutResource, heap.GetUavCPU(0), 0);

    if (!CreateConstantsBuffer(_device, _constantBuffers[_counter], constants, heap.GetCbvCPU(0)))
    {
        LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
        CopyThrough(InCmdList, InResource, OutResource);
        Report(false, "could not write the pass's constants");
        return true;
    }

    ID3D12DescriptorHeap* heaps[] = { heap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);
    InCmdList->SetComputeRootDescriptorTable(0, heap.GetTableGPUStart());
    InCmdList->Dispatch((constants.Width + 7) / 8, (constants.Height + 7) / 8, 1);

    Report(true);
    return true;
}

F5Lut_Dx12::F5Lut_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    // dlssnr_lut.hlsl: t0 input, t1 the lattice, t2 the exposure, u0 output, b0 constants, s0 a linear-clamp sampler.
    D3D12_STATIC_SAMPLER_DESC sampler {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    if (!SetupRootSignature(InDevice, 3, 1, 1, 0, 0, 1, &sampler))
    {
        LOG_ERROR("Failed to setup root signature");
        return;
    }

    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(DlssNrLutConstants));
    const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    for (auto& buffer : _constantBuffers)
    {
        if (FAILED(InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&buffer))))
        {
            LOG_ERROR("[{0}] CreateCommittedResource failed", _name);
            return;
        }
    }

    // Precompiled only: the shader includes NR's .hlsli files, which a runtime compile from a string cannot reach.
    if (!CreateComputePipeline(InDevice, &_pipelineState, dlssnr_lut_cso, sizeof(dlssnr_lut_cso), nullptr))
    {
        LOG_ERROR("[{0}] Failed to create compute pipeline", _name);
        return;
    }

    _init = InitHeaps(InDevice, _frameHeaps, F5LUT_NUM_OF_HEAPS);
}

F5Lut_Dx12::~F5Lut_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    for (int i = 0; i < F5LUT_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
        SAFE_RELEASE(_constantBuffers[i]);
    }

    SAFE_RELEASE(_buffer);
    if (_lutTexture != nullptr)
        ReleaseLutTexture();
}
