#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#if defined(min)
#undef min
#endif

#if defined(CreateWindow)
#undef CreateWindow
#endif

#include <wrl.h>

using namespace Microsoft::WRL;

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <d3dx12.h>

// STL Headers
#include <algorithm>
#include <cassert>
#include <chrono>

// Helper functions
#include "Helpers.h"


// Swap Chain buffer frames
const uint8_t numFrames = 3;
uint32_t clientWidth = 1280;
uint32_t clientHeight = 720;

// WARP adapter
bool useWARP = false;
// True if DX12 objects are init
bool g_IsInitialized = false;

// Window Handler
HWND hWnd;
// Toggle fullscreen
RECT winRect;

// DirectX 12 Objects
ComPtr<ID3D12Device2> device;
ComPtr<ID3D12CommandQueue>commandQueue;
ComPtr<IDXGISwapChain4> swapChain;
ComPtr<ID3D12Resource> backBuffers[numFrames];
ComPtr<ID3D12GraphicsCommandList> commandList;
ComPtr<ID3D12CommandAllocator> commandAllocator[numFrames];
ComPtr<ID3D12DescriptorHeap> descriptorHeap;

UINT descriptorSize;
UINT currentBufferIndex;

ComPtr<ID3D12Fence> fence;
uint64_t fenceValue = 0;
uint64_t frameFenceValue[numFrames] = {};
HANDLE fenceEvent;

// V-sync
bool vSync = true;
bool tearingSupported = false;
bool fullscreen = false;

//typedef	struct D3D12_COMMAND_QUEUE_DESC
//{
//	D3D12_COMMAND_LIST_TYPE		Type;
//	INT							Priority;
//	D3D12_COMMAND_QUEUE_FLAGS	Flags;
//	UINT						NodeMask;
//}D3D12_COMMAND_QUEUE_DESC;

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (g_IsInitialized)
	{
		switch (message)
		{
			case WM_PAINT:
				//Update();
				//Render();
				break;
			case WM_SYSKEYDOWN:
			case WM_KEYDOWN:
			{
				bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
				switch (wParam)
				{
					case 'V':
						vSync = !vSync;
						break;
					case VK_ESCAPE:
						::PostQuitMessage(0);
						break;
					case VK_F11:
						//setFullscreen(fullscreen);
						break;
				}
			}
			break;
			case WM_SYSCHAR:
				break;
			case WM_SIZE:
			{
				RECT clientRect = {};
				::GetClientRect(hWnd, &clientRect);

				int width = clientRect.right - clientRect.left;
				int height = clientRect.bottom - clientRect.top;
				//Resize(width, height);
			}
				break;
			case WM_DESTROY:
				::PostQuitMessage(0);
				break;
			default:
				return ::DefWindowProcW(hwnd, message, wParam, lParam);
		}
	}
}


void parseCommandLineArguments() 
{
	int argc;
	wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

	for (size_t i = 0; i < argc; ++i)
	{
		if (::wcscmp(argv[i], L"-w") == 0 || ::wcscmp(argv[i], L"--width") == 0)
		{
			clientWidth = ::wcstol(argv[++i], nullptr, 10);
		}
		if (::wcscmp(argv[i], L"-h") == 0 || ::wcscmp(argv[i], L"--height") == 0)
		{
			clientHeight = ::wcstol(argv[++i], nullptr, 10);
		}
		if (::wcscmp(argv[i], L"-warp") == 0 || ::wcscmp(argv[i], L"--warp") == 0)
		{
			useWARP = true;
		}
	}
	::LocalFree(argv);
}

void enableDebugLayer()
{
#if defined(_DEBUG)
	ComPtr<ID3D12Debug> debugInterface;
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
	debugInterface->EnableDebugLayer();
#endif
}

void	registerWindowClass(HINSTANCE hInst, const wchar_t* windowClassName)
{
	WNDCLASSEXW windowClass = {};
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = &WndProc;
	windowClass.cbClsExtra = 0;
	windowClass.cbWndExtra = 0;
	windowClass.hInstance = hInst;
	windowClass.hIcon = ::LoadIcon(hInst, NULL);
	windowClass.hCursor = ::LoadCursor(hInst, NULL);
	windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	windowClass.lpszMenuName = NULL;
	windowClass.lpszClassName = windowClassName;
	windowClass.hIconSm = ::LoadIcon(hInst, NULL);

	static ATOM atom = ::RegisterClassExW(&windowClass);
	assert(atom > 0);
}

HWND createWindow(const wchar_t* windowClassName, HINSTANCE hInst, const wchar_t* windowTitle, uint32_t width, uint32_t height)
{
	int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

	RECT windowRect = { 0,0,static_cast<LONG>(width),static_cast<LONG>(height) };
	::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	int windowWidth = windowRect.right - windowRect.left;
	int windowHeight = windowRect.bottom - windowRect.top;

	int windowX = std::max<int>(0, (screenWidth - windowWidth)/2);
	int windowY	= std::max<int>(0,(screenHeight-windowHeight)/2);

	HWND hWnd = ::CreateWindowExW(
		NULL,
		windowClassName,
		windowTitle,
		WS_OVERLAPPEDWINDOW,
		windowX,
		windowY,
		windowWidth,
		windowHeight,
		NULL,
		NULL,
		hInst,
		nullptr
	);

	assert(hWnd && "Failed to create window");

	return hWnd;
}

ComPtr<IDXGIAdapter4> getAdapter(bool useWARP) 
{
	ComPtr<IDXGIFactory4> dxgiFactory;
	UINT createFactoryFlags = 0;
#if defined(_DEBUG)
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags,IID_PPV_ARGS(&dxgiFactory)));

	ComPtr<IDXGIAdapter1> dxgiAdapter1;
	ComPtr<IDXGIAdapter4> dxgiAdapter4;

	if (useWARP)
	{
		ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiFactory)));
		ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
	}
	else
	{
		SIZE_T maxVRAM = 0;
		for (UINT i = 0;dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1	dxgiAdapterDesc1;
			dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

			// Check if adapter has the ability to create a D3D12 device, without making it
			// Favor, adapter with largest VRAM capacity

			if((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 && SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) , dxgiAdapterDesc1.DedicatedVideoMemory > maxVRAM)
			{
				maxVRAM = dxgiAdapterDesc1.DedicatedVideoMemory;
				ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
			}
		}
	}
	return dxgiAdapter4;
};

ComPtr<ID3D12Device> createDevice(ComPtr<IDXGIAdapter4> adapter)
{
	ComPtr<ID3D12Device2> d3d12Device2;
	ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));

	HRESULT WINAPI D3D12CreateDevice
	(
		_In_opt_	IUnknown			*pAdapter,
					D3D_FEATURE_LEVEL	MinimumFeatureLevel,
		_In_		REFIID				riid,
		_Out_opt_	void				**pDevice
	);
	
	// Enable debug messages in debug mode
#if defined(_DEBUG)
	ComPtr<ID3D12InfoQueue> infoQueue;
	if (SUCCEEDED(d3d12Device2.As(&infoQueue)))
	{
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

		// Suppress Categories
		// D3D12_MESSAGE_CATEGORY Categories[]	=	{};

		// Suppress messages based on "severity level"
		D3D12_MESSAGE_SEVERITY Severities[] = {D3D12_MESSAGE_SEVERITY_INFO,};

		// Supress messages based on their IDs
		D3D12_MESSAGE_ID DenyIDs[] =
		{
			D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,		// Error when clearing a color value for a render target than what it originally had
			D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,								// Error for frame capture error
			D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,							// Errror for frame capture error
		};
		D3D12_INFO_QUEUE_FILTER NewFilter = {};
		//NewFilter.DenyList.NumCategories	=	_countof(Categories)
		//NewFilter.DenyList.pCategoryList	=	Categories
		NewFilter.DenyList.NumSeverities	= _countof(Severities);
		NewFilter.DenyList.pSeverityList	= Severities;
		NewFilter.DenyList.NumIDs			= _countof(DenyIDs);
		NewFilter.DenyList.pIDList			= DenyIDs;

		ThrowIfFailed(infoQueue->PushStorageFilter(&NewFilter));
	}
#endif
	return d3d12Device2;
}

ComPtr<ID3D12CommandQueue>	createCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
{
	ComPtr<ID3D12CommandQueue> d3d12CommandQueue;
	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Type		= type;
	desc.Priority	= D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags		= D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask	= 0;

	ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d12CommandQueue)));

	return d3d12CommandQueue;
}

bool CheckTearingSupport()
{
	BOOL allowTearing = FALSE;
	ComPtr<IDXGIAdapter4> factory4;
	if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
	{
		ComPtr<IDXGIFactory5> factory5;
		if (SUCCEEDED(factory4.As(&factory5)))
		{
			if (FAILED(factory5->CheckFeatureSupport(
				DXGI_FEATURE_PRESENT_ALLOW_TEARING,
				&allowTearing, sizeof(allowTearing)
			)))
			{
				allowTearing = FALSE;
			}
		}
	}
	return allowTearing == TRUE;
}

ComPtr<IDXGISwapChain4> createSwapChain(HWND hWnd, ComPtr<ID3D12CommandQueue> commandQueue, uint32_t width, uint32_t height, uint32_t bufferCount)
{
	ComPtr<IDXGISwapChain4>	dxgiSwapChain4;
	ComPtr<IDXGIFactory4>	dxgiFactory4;
	UINT	createFactoryFlags = 0;
#if defined(_DEBUG)
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

	DXGI_SWAP_CHAIN_DESC1	swapChainDesc = {};
	swapChainDesc.Width			= width;
	swapChainDesc.Height		= height;
	swapChainDesc.Format		= DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo		= FALSE;
	swapChainDesc.SampleDesc	= { 1,0 };
	swapChainDesc.BufferUsage	= DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount	= bufferCount;
	swapChainDesc.Scaling		= DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect	= DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode		= DXGI_ALPHA_MODE_UNSPECIFIED;
	swapChainDesc.Flags			= CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	ComPtr<IDXGISwapChain1> swapChain1;
	ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
		commandQueue.Get(),
		hWnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain1
	));

	ThrowIfFailed(dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));
	ThrowIfFailed(swapChain1.As(&dxgiSwapChain4));

	return dxgiSwapChain4;
}

int main()
{
	return 0;
}