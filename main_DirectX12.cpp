#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <stdexcept>
#include <string>
#include <chrono>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// ──────────────────────────────────────────────
// 인라인 셰이더
// ──────────────────────────────────────────────

static const char* g_shaderSrc = R"HLSL(
cbuffer ConstantBuffer : register(b0)
{
    float4x4 rotation;
};

struct VSInput  { float2 pos : POSITION; float3 color : COLOR; };
struct PSInput  { float4 pos : SV_POSITION; float3 color : COLOR; };

PSInput VSMain(VSInput v)
{
    PSInput o;
    o.pos   = mul(rotation, float4(v.pos, 0.0f, 1.0f));
    o.color = v.color;
    return o;
}

float4 PSMain(PSInput i) : SV_TARGET
{
    return float4(i.color, 1.0f);
}
)HLSL";

// ──────────────────────────────────────────────
// 상수
// ──────────────────────────────────────────────

static constexpr UINT  FRAME_COUNT = 2;
static constexpr UINT  WIDTH = 800;
static constexpr UINT  HEIGHT = 600;
static constexpr UINT  CB_SIZE = (sizeof(XMFLOAT4X4) + 255) & ~255;

// ──────────────────────────────────────────────
// 자료구조
// ──────────────────────────────────────────────

struct Vertex { XMFLOAT2 pos; XMFLOAT3 color; };
struct SceneConstant { XMFLOAT4X4 rotation; };

// ──────────────────────────────────────────────
// DX12 전역
// ──────────────────────────────────────────────

static ComPtr<ID3D12Device>              g_device;
static ComPtr<ID3D12CommandQueue>        g_cmdQueue;
static ComPtr<IDXGISwapChain3>           g_swapChain;
static ComPtr<ID3D12DescriptorHeap>      g_rtvHeap;
static ComPtr<ID3D12Resource>            g_renderTargets[FRAME_COUNT];
static ComPtr<ID3D12CommandAllocator>    g_cmdAlloc[FRAME_COUNT];
static ComPtr<ID3D12GraphicsCommandList> g_cmdList;
static ComPtr<ID3D12RootSignature>       g_rootSig;
static ComPtr<ID3D12PipelineState>       g_pso;
static ComPtr<ID3D12Resource>            g_vertexBuffer;
static ComPtr<ID3D12Resource>            g_constantBuffer[FRAME_COUNT];
static ComPtr<ID3D12DescriptorHeap>      g_cbvHeap;
static ComPtr<ID3D12Fence>               g_fence;

static UINT   g_rtvDescSize = 0;
static UINT   g_cbvDescSize = 0;
static UINT64 g_fenceValue[FRAME_COUNT] = {};
static UINT64 g_fenceCounter = 0;
static HANDLE g_fenceEvent = nullptr;
static UINT   g_frameIndex = 0;
static UINT8* g_cbMapped[FRAME_COUNT] = {};
static D3D12_VERTEX_BUFFER_VIEW g_vbView = {};

// ──────────────────────────────────────────────
// 헬퍼
// ──────────────────────────────────────────────

static void Check(HRESULT hr, const char* msg = "")
{
    if (FAILED(hr))
        throw std::runtime_error(std::string(msg) + " hr=" + std::to_string(hr));
}

static void WaitForFrame(UINT frame)
{
    if (g_fence->GetCompletedValue() < g_fenceValue[frame]) {
        Check(g_fence->SetEventOnCompletion(g_fenceValue[frame], g_fenceEvent));
        WaitForSingleObject(g_fenceEvent, INFINITE);
    }
}

static void WaitForGpu()
{
    Check(g_cmdQueue->Signal(g_fence.Get(), ++g_fenceCounter));
    Check(g_fence->SetEventOnCompletion(g_fenceCounter, g_fenceEvent));
    WaitForSingleObject(g_fenceEvent, INFINITE);
}

// ──────────────────────────────────────────────
// 초기화
// ──────────────────────────────────────────────

static void InitDX12(HWND hwnd)
{
#ifdef _DEBUG
    ComPtr<ID3D12Debug> dbg;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer();
#endif

    ComPtr<IDXGIFactory6> factory;
    Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
    Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)));

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(g_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&g_cmdQueue)));

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.BufferCount = FRAME_COUNT; sd.Width = WIDTH; sd.Height = HEIGHT;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.SampleDesc = { 1,0 }; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    ComPtr<IDXGISwapChain1> sc1;
    Check(factory->CreateSwapChainForHwnd(g_cmdQueue.Get(), hwnd, &sd, nullptr, nullptr, &sc1));
    Check(sc1.As(&g_swapChain));
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    // RTV Heap
    D3D12_DESCRIPTOR_HEAP_DESC rhd = {};
    rhd.NumDescriptors = FRAME_COUNT; rhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    Check(g_device->CreateDescriptorHeap(&rhd, IID_PPV_ARGS(&g_rtvHeap)));
    g_rtvDescSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvH = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < FRAME_COUNT; i++) {
        Check(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])));
        g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr, rtvH);
        rtvH.ptr += g_rtvDescSize;
        Check(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_cmdAlloc[i])));
    }

    // CBV Heap
    D3D12_DESCRIPTOR_HEAP_DESC chd = {};
    chd.NumDescriptors = FRAME_COUNT; chd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    chd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(g_device->CreateDescriptorHeap(&chd, IID_PPV_ARGS(&g_cbvHeap)));
    g_cbvDescSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Constant Buffers
    D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC   bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bufDesc.Width = CB_SIZE;
    bufDesc.Height = 1; bufDesc.DepthOrArraySize = 1; bufDesc.MipLevels = 1;
    bufDesc.SampleDesc = { 1,0 }; bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_CPU_DESCRIPTOR_HANDLE cbvH = g_cbvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < FRAME_COUNT; i++) {
        Check(g_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
            &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_constantBuffer[i])));
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvd = {};
        cbvd.BufferLocation = g_constantBuffer[i]->GetGPUVirtualAddress();
        cbvd.SizeInBytes = CB_SIZE;
        g_device->CreateConstantBufferView(&cbvd, cbvH);
        cbvH.ptr += g_cbvDescSize;
        Check(g_constantBuffer[i]->Map(0, nullptr, reinterpret_cast<void**>(&g_cbMapped[i])));
    }

    // Root Signature
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV; range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER rp = {};
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp.DescriptorTable = { 1, &range };
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 1; rsd.pParameters = &rp;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> rsBlob, rsErr;
    Check(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr));
    Check(g_device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&g_rootSig)));

    // 셰이더 컴파일 (메모리에서)
    UINT flags = 0;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> vs, ps, err;
    if (FAILED(D3DCompile(g_shaderSrc, strlen(g_shaderSrc), nullptr, nullptr, nullptr,
        "VSMain", "vs_5_0", flags, 0, &vs, &err))) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        throw std::runtime_error("VS compile failed");
    }
    if (FAILED(D3DCompile(g_shaderSrc, strlen(g_shaderSrc), nullptr, nullptr, nullptr,
        "PSMain", "ps_5_0", flags, 0, &ps, &err))) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        throw std::runtime_error("PS compile failed");
    }

    // PSO
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psd = {};
    psd.pRootSignature = g_rootSig.Get();
    psd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psd.InputLayout = { layout, _countof(layout) };
    psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psd.NumRenderTargets = 1;
    psd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psd.SampleDesc = { 1, 0 };
    psd.SampleMask = UINT_MAX;
    psd.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE };
    psd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psd.DepthStencilState.DepthEnable = FALSE;
    Check(g_device->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&g_pso)));

    // Command List
    Check(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        g_cmdAlloc[0].Get(), g_pso.Get(), IID_PPV_ARGS(&g_cmdList)));
    Check(g_cmdList->Close());

    // Vertex Buffer
    Vertex verts[] = {
        { {  0.0f,  0.6f }, { 1,0,0 } },
        { { -0.6f, -0.4f }, { 0,1,0 } },
        { {  0.6f, -0.4f }, { 0,0,1 } },
    };
    bufDesc.Width = sizeof(verts);
    Check(g_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
        &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_vertexBuffer)));
    void* p; Check(g_vertexBuffer->Map(0, nullptr, &p));
    memcpy(p, verts, sizeof(verts));
    g_vertexBuffer->Unmap(0, nullptr);
    g_vbView = { g_vertexBuffer->GetGPUVirtualAddress(), sizeof(verts), sizeof(Vertex) };

    // Fence
    Check(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)));
    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

// ──────────────────────────────────────────────
// 렌더
// ──────────────────────────────────────────────

static void Render(float t)
{
    // CB 업데이트
    SceneConstant sc;
    XMStoreFloat4x4(&sc.rotation, XMMatrixTranspose(XMMatrixRotationZ(t * XMConvertToRadians(45.f))));
    memcpy(g_cbMapped[g_frameIndex], &sc, sizeof(sc));

    Check(g_cmdAlloc[g_frameIndex]->Reset());
    Check(g_cmdList->Reset(g_cmdAlloc[g_frameIndex].Get(), g_pso.Get()));

    D3D12_VIEWPORT vp = { 0,0,(float)WIDTH,(float)HEIGHT,0,1 };
    D3D12_RECT     sr = { 0,0,WIDTH,HEIGHT };
    g_cmdList->RSSetViewports(1, &vp);
    g_cmdList->RSSetScissorRects(1, &sr);

    // Barrier: Present → RTV
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = g_renderTargets[g_frameIndex].Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_cmdList->ResourceBarrier(1, &b);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)g_frameIndex * g_rtvDescSize;
    const float clear[] = { 0.08f,0.08f,0.08f,1.f };
    g_cmdList->ClearRenderTargetView(rtv, clear, 0, nullptr);
    g_cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    g_cmdList->SetGraphicsRootSignature(g_rootSig.Get());
    ID3D12DescriptorHeap* heaps[] = { g_cbvHeap.Get() };
    g_cmdList->SetDescriptorHeaps(1, heaps);
    D3D12_GPU_DESCRIPTOR_HANDLE cbvGpu = g_cbvHeap->GetGPUDescriptorHandleForHeapStart();
    cbvGpu.ptr += (UINT64)g_frameIndex * g_cbvDescSize;
    g_cmdList->SetGraphicsRootDescriptorTable(0, cbvGpu);
    g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_cmdList->IASetVertexBuffers(0, 1, &g_vbView);
    g_cmdList->DrawInstanced(3, 1, 0, 0);

    // Barrier: RTV → Present
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_cmdList->ResourceBarrier(1, &b);
    Check(g_cmdList->Close());

    ID3D12CommandList* lists[] = { g_cmdList.Get() };
    g_cmdQueue->ExecuteCommandLists(1, lists);
    Check(g_swapChain->Present(1, 0));

    g_fenceValue[g_frameIndex] = ++g_fenceCounter;
    Check(g_cmdQueue->Signal(g_fence.Get(), g_fenceCounter));
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
    WaitForFrame(g_frameIndex);
}

// ──────────────────────────────────────────────
// Win32
// ──────────────────────────────────────────────

static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) { DestroyWindow(hw); return 0; }
    return DefWindowProc(hw, msg, wp, lp);
}

int main()
{
    HINSTANCE hInst = GetModuleHandle(nullptr);
    WNDCLASSEX wc = { sizeof(wc) };
    wc.hInstance = hInst; wc.lpfnWndProc = WndProc; wc.lpszClassName = L"DX12";
    RegisterClassEx(&wc);
    RECT rc = { 0,0,WIDTH,HEIGHT };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowEx(0, L"DX12", L"Rainbow Triangle - DX12",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    try { InitDX12(hwnd); }
    catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Init Error", MB_OK | MB_ICONERROR); return -1;
    }

    auto t0 = std::chrono::steady_clock::now();
    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        else {
            float sec = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
            try { Render(sec); }
            catch (const std::exception& e) {
                MessageBoxA(nullptr, e.what(), "Render Error", MB_OK | MB_ICONERROR); break;
            }
        }
    }
    WaitForGpu();
    CloseHandle(g_fenceEvent);
    return 0;
}