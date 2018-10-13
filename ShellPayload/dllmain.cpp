#include "tmt.h"
#include "minihook\MinHook.h"
#include <shlobj.h>

static HMODULE gLibModule = 0;

LPCITEMIDLIST g_last_pidl;
UINT g_last_QCMFlags;

#pragma region ShellHelper
// Callee need to DestroyMenu(hTopMenu);
HRESULT FillContextMenuFromPIDL(LPCITEMIDLIST pidl, HMENU hTopMenu, UINT uFlags)
{
	HRESULT ret = MAKE_HRESULT(SEVERITY_ERROR, 0, 0);

	IContextMenu *pcm;
	HRESULT hr;

	IShellFolder *psf;
	LPCITEMIDLIST pidlChild;
	if (SUCCEEDED(hr = SHBindToParent(pidl, IID_IShellFolder, (void**)&psf, &pidlChild))) {
		hr = psf->GetUIObjectOf(NULL, 1, &pidlChild, IID_IContextMenu, NULL, (void**)&pcm);
		// 0 was hWnd
		psf->Release();

		if (SUCCEEDED(hr)) {
			hr = pcm->QueryContextMenu(hTopMenu, 0, 1, 0x7FFF, uFlags);

			if (SUCCEEDED(hr)) {
				IContextMenu3* hContextMenu3 = NULL;
				IContextMenu2* hContextMenu2 = NULL;

				/*
				if (SUCCEEDED(pcm->QueryInterface(IID_IContextMenu3, (void**)&hContextMenu3))) {
				LRESULT lres;

				hContextMenu3->HandleMenuMsg2(WM_INITMENUPOPUP, (WPARAM)hTopMenu, 0, &lres);

				for (int pos = 0; pos < GetMenuItemCount(hTopMenu); pos++) {
				HMENU subMenu = GetSubMenu(hTopMenu, pos);
				if (subMenu != NULL) {
				LPARAM lparam = (LPARAM)(MAKELONG(pos, FALSE));
				hContextMenu3->HandleMenuMsg2(WM_INITMENUPOPUP, (WPARAM)subMenu, lparam, &lres);
				}
				}
				//hContextMenu3->Release();
				}*/

				if (SUCCEEDED(pcm->QueryInterface(IID_IContextMenu2, (void**)&hContextMenu2))) {
					hContextMenu2->HandleMenuMsg(WM_INITMENUPOPUP, (WPARAM)hTopMenu, FALSE);

					for (int pos = 0; pos < GetMenuItemCount(hTopMenu); pos++) {
						HMENU subMenu = GetSubMenu(hTopMenu, pos);
						if (subMenu != NULL) {
							LPARAM lparam = (LPARAM)(MAKELONG(pos, FALSE));
							hContextMenu2->HandleMenuMsg(WM_INITMENUPOPUP, (WPARAM)subMenu, lparam);
						}
					}
					//hContextMenu2->Release();
				}

				ret = MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
			}

			//pcm->Release();
		}
	}

	return ret;
}
#pragma endregion

void ClassicMenuShell(HMENU hMenu) {
	MENUINFO info;
	info.cbSize = sizeof(MENUINFO);
	info.fMask = MIM_BACKGROUND;
	GetMenuInfo(hMenu, &info);
	info.hbrBack = nullptr;
	SetMenuInfo(hMenu, &info);

	for (int i = 0; i < GetMenuItemCount(hMenu); i++) {
		UINT id = GetMenuItemID(hMenu, i);

		MENUITEMINFO menuInfo;
		menuInfo.cbSize = sizeof(MENUITEMINFO);
		menuInfo.fMask = MIIM_DATA | MIIM_BITMAP | MIIM_FTYPE;
		GetMenuItemInfo(hMenu, i, true, &menuInfo);
		ULONG_PTR itemData = menuInfo.dwItemData;
		HBITMAP hBmpHnd = menuInfo.hbmpItem;

		menuInfo.fType &= ~MFT_OWNERDRAW;
		menuInfo.fMask = MIIM_FTYPE;
		SetMenuItemInfo(hMenu, i, true, &menuInfo);

		HBITMAP hBmp = *((HBITMAP*)(itemData + 0x20));
		if (hBmpHnd == NULL && hBmp != NULL)
			SetMenuItemBitmaps(hMenu, i, MF_BYPOSITION, hBmp, NULL);
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		gLibModule = hModule;
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

HWND IdentifyTarget(HWND hWnd) {
	if (IsWindow(hWnd)) {
		char clsName[256];
		GetClassNameA(hWnd, clsName, sizeof(clsName));
		if (strcmp(clsName, "SHELLDLL_DefView") == 0) {
			return hWnd;
		} else if (strcmp(clsName, "SysTreeView32") == 0) {
			HWND hWndParent = GetParent(hWnd);
			char clsName[256];
			GetClassNameA(hWndParent, clsName, sizeof(clsName));
			if (strcmp(clsName, "NamespaceTreeControl") == 0) {
				return hWnd;
			}
		}
	}

	return NULL;
}

#pragma region TrackPopupMenuHook
typedef BOOL (WINAPI *FPT_TrackPopupMenu)(HMENU, UINT, int, int, int, HWND, CONST RECT*);
FPT_TrackPopupMenu fpTrackPopupMenu;
LONG_PTR g_oldWndProc = NULL;

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static HMENU root;
	LRESULT ret = WNDPROC(g_oldWndProc)(hwnd, uMsg, wParam, lParam);
	if (uMsg == WM_INITMENUPOPUP) {
		HMENU hMenu = (HMENU)wParam;
		WORD pos = LOWORD(lParam);

		if (pos == 0) {
			ClassicMenuShell(hMenu);
			root = hMenu;
		}
		else {
			//HMENU iconSource = CreatePopupMenu();								// Create an empty menu
			//FillContextMenuFromPIDL(g_last_pidl, iconSource, g_last_QCMFlags);	// And fill it
			//int num = GetMenuItemCount(iconSource);
			//num = GetMenuItemCount(root);
			//DestroyMenu(iconSource);
		}
	}

	return ret;
}

// Detour function which overrides TrackPopupMenu.
BOOL WINAPI HookedTrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, CONST RECT* prcRect) {
	HWND hTarget = IdentifyTarget(hWnd);
	if (hTarget != NULL) {
		g_oldWndProc = GetWindowLongPtr(hWnd, GWLP_WNDPROC);
		SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)&HookedWndProc);
	}

	BOOL ret = fpTrackPopupMenu(hMenu, uFlags, x, y, nReserved, hWnd, prcRect);
	g_last_pidl = NULL;

	if (g_oldWndProc != NULL) {
		SetWindowLongPtr(hWnd, GWLP_WNDPROC, g_oldWndProc);
		g_oldWndProc = NULL;
	}

	return ret;
}

extern "C" _declspec(dllexport) DWORD  __cdecl  __PerformInjection(LPVOID param) {
	if (fpTrackPopupMenu != NULL) {
		return 0;
	}
	
	if (MH_Initialize() != MH_OK)
		MessageBoxW(0, L"Unable to initialize disassembler!", L"Fatal Error", MB_ICONERROR);
	
	BOOL flag = MH_CreateHook(&TrackPopupMenu, &HookedTrackPopupMenu,
		reinterpret_cast<LPVOID*>(&fpTrackPopupMenu));
	if (flag != MH_OK)
		MessageBoxW(0, L"Unable to create hooks!", L"Fatal Error", MB_ICONERROR);

	// Enable the hook
	flag = MH_EnableHook(&TrackPopupMenu);
	if (flag != MH_OK)
		MessageBoxW(0, L"Failed to enable hooks!", L"Fatal Error", MB_ICONERROR);
	return 0;
}

extern "C" _declspec(dllexport) DWORD  __cdecl  __SetCurrentPIDL(LPVOID param) {
	LPCITEMIDLIST pidl = (LPCITEMIDLIST)param;
	
	g_last_pidl = pidl;
	return 0;
}

extern "C" _declspec(dllexport) DWORD  __cdecl  __SetContextMenuFlags(LPVOID param) {
	UINT flags = (UINT) param;

	g_last_QCMFlags = flags;
	return 0;
}

#pragma endregion