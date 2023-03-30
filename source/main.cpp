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

int main(int argc, char* argv[])
{
	Renderer renderer = Renderer(4096, 4096, "RenderLab");
	renderer.Init();
	MSG msg = {};
	while (true)
	{
		renderer.Update(renderer.GetDeltaTime());
		renderer.Render();
	}
	renderer.Destroy();
	return static_cast<char>(msg.wParam);
}
