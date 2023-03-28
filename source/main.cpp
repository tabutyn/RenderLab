#include "renderer.h"
#include <windows.h>
#include <memory>

LRESULT WindowProc(_In_ HWND hWnd, _In_ UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam) {
	Renderer* renderer = reinterpret_cast<Renderer*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

	switch (uMsg)
	{
	case WM_CREATE:
	{
		LPCREATESTRUCT pCreateStructure = reinterpret_cast<LPCREATESTRUCT>(lParam);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStructure->lpCreateParams));
	}
	return 0;
	case WM_CLOSE:
	{
		PostQuitMessage(0);
	}
	return 0;
	case WM_PAINT:
	{
		renderer->Update(renderer->GetDeltaTime());
		renderer->Render();
	}
	return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int nCmdShow)
{
	Renderer renderer = Renderer(600, 400, "RenderLab", hInstance);
	WNDCLASSEXA windowClass = { 0 };
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursorA(NULL, IDC_ARROW);
	windowClass.lpszClassName = "RenderLabWindowClass";
	RegisterClassExA(&windowClass);

	RECT windowRect = { 0, 0, renderer.GetWidth(), renderer.GetHeight()};
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	HWND hWnd = CreateWindowExA(
		0,
		windowClass.lpszClassName,
		renderer.GetTitle(),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,
		nullptr,
		hInstance,
		&renderer
	);
	renderer.SetWindowHandle(hWnd);

	ShowWindow(hWnd, SW_SHOWNORMAL);

	renderer.Init();
	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		// Process any messages in the queue.
		if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}
	}
	renderer.Destroy();
	return static_cast<char>(msg.wParam);
}
