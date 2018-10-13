#include "tmt.h"
#include "minihook\MinHook.h"
#include <shlobj.h>
#include <shlwapi.h>

static HMODULE gLibModule = 0;

DWORD g_last_pidl_len;
LPCITEMIDLIST* g_last_pidl;
UINT g_last_QCMFlags;

typedef struct tagMenuLevel *LPMenuLevel;

typedef struct tagMenuLevel {
	LPMenuLevel parent;

	HMENU hMenuOriginal;
	HMENU hMenuIconSource;

	// Pos in parent menu
	WORD posInOriginal;
	WORD posInIconSource;

	WORD level;

} MenuLevel;

typedef struct tagRootMenuInfo {
	HMENU hMenu;
	HMENU hIconSource;
	IContextMenu* pContextMenu;
} RootMenuInfo;

#pragma region ShellHelper
// Callee need to DestroyMenu(hTopMenu);
HRESULT FillContextMenuFromPIDL(LPCITEMIDLIST* pidl, DWORD pidl_len, HMENU hTopMenu, UINT uFlags, IContextMenu** pContextMenu)
{
	HRESULT ret = MAKE_HRESULT(SEVERITY_ERROR, 0, 0);

	IContextMenu *pcm;
	HRESULT hr;

	if (pidl_len == 0) {
		IShellFolder *psf;
		LPCITEMIDLIST pidlChild;
		hr = SHBindToParent(pidl[0], IID_IShellFolder, (void**)&psf, &pidlChild);
		if (FAILED(hr))
			return hr;

		IShellView *psw;
		hr = psf->CreateViewObject(NULL, IID_IShellView, (void**)&psw);
		if (FAILED(hr)) {
			psf->Release();
			return hr;
		}

		hr = psw->GetItemObject(SVGIO_BACKGROUND, IID_IContextMenu, (void**)&pcm);

		psw->Release();
		psf->Release();
	}
	else {
		IShellFolder *psfDesktop;
		hr = SHGetDesktopFolder(&psfDesktop);
		if (FAILED(hr)) {
			psfDesktop->Release();
			return hr;
		}

		hr = psfDesktop->GetUIObjectOf(NULL, pidl_len, pidl, IID_IContextMenu, NULL, (void**)&pcm);

		psfDesktop->Release();
	}

	if (FAILED(hr))
		return hr;

	*pContextMenu = pcm;
	hr = pcm->QueryContextMenu(hTopMenu, 0, 1, 0x7FFF, uFlags);

	if (FAILED(hr)) {
		pcm->Release();
		return hr;
	}
		
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
	}

	ret = MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);	

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

void ClassicMenuShell2(HMENU hMenu, HMENU hIconSource) {
	MENUINFO info;
	info.cbSize = sizeof(MENUINFO);
	info.fMask = MIM_BACKGROUND;
	GetMenuInfo(hMenu, &info);
	info.hbrBack = nullptr;
	SetMenuInfo(hMenu, &info);

	for (int i = 0; i < GetMenuItemCount(hMenu); i++) {
		UINT id = GetMenuItemID(hMenu, i);

		MENUITEMINFO menuInfo = { 0 };
		menuInfo.cbSize = sizeof(MENUITEMINFO);
		menuInfo.fMask = MIIM_DATA | MIIM_BITMAP | MIIM_FTYPE;
		GetMenuItemInfo(hMenu, i, true, &menuInfo);
		ULONG_PTR itemData = menuInfo.dwItemData;
		HBITMAP hBmpOrignal = menuInfo.hbmpItem;

		menuInfo.fType &= ~MFT_OWNERDRAW;
		menuInfo.fMask = MIIM_FTYPE;
		SetMenuItemInfo(hMenu, i, true, &menuInfo);

		menuInfo.fMask = MIIM_BITMAP;
		GetMenuItemInfo(hIconSource, i, true, &menuInfo);
		HBITMAP hIconSrc = menuInfo.hbmpItem;

		if (hBmpOrignal == NULL) {
			if (hIconSrc != NULL) {
				SetMenuItemBitmaps(hMenu, i, MF_BYPOSITION, menuInfo.hbmpItem, NULL);
			}
			else {
				HBITMAP hBmp = *((HBITMAP*)(itemData + 0x20));
				if (!SetMenuItemBitmaps(hMenu, i, MF_BYPOSITION, hBmp, NULL)) {
					SetMenuItemBitmaps(hMenu, i, MF_BYPOSITION, *((HBITMAP*)(itemData + 0x18)), NULL);
				}
			}
		}			
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

BOOL FindIndexInResMenu(HMENU hMenu, WORD pos, HMENU hResMenu, WORD* rpos) {
	char text[256];
	GetMenuStringA(hMenu, pos, text, sizeof(text), MF_BYPOSITION);

	int cItem = GetMenuItemCount(hResMenu);
	for (int i = 0; i < cItem; i++) {
		char text2[256];
		GetMenuStringA(hResMenu, i, text2, sizeof(text2), MF_BYPOSITION);
		if (strcmp(text, text2) == 0) {
			*rpos = i;
			return TRUE;
		}
	}

	return FALSE;
}

LPMenuLevel PushMenuLevel(LPMenuLevel curMenuLevel, HMENU hMenu, WORD pos, HMENU hResource, WORD posInRes) {
	LPMenuLevel nextMenuLevel = (LPMenuLevel)malloc(sizeof(MenuLevel));
	nextMenuLevel->parent = curMenuLevel;
	nextMenuLevel->hMenuOriginal = hMenu;
	nextMenuLevel->hMenuIconSource = hResource;
	nextMenuLevel->posInOriginal = pos;
	nextMenuLevel->posInIconSource = posInRes;
	if (curMenuLevel == NULL)
		nextMenuLevel->level = 0;	// Root level
	else
		nextMenuLevel->level = curMenuLevel->level + 1;

	return nextMenuLevel;
}

#pragma region TrackPopupMenuHook
typedef BOOL (WINAPI *FPT_TrackPopupMenu)(HMENU, UINT, int, int, int, HWND, CONST RECT*);
FPT_TrackPopupMenu fpTrackPopupMenu;
LONG_PTR g_oldWndProc = NULL;

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static RootMenuInfo g_rootMenuInfo;
	static LPMenuLevel g_curMenuLevel;

	LRESULT ret = WNDPROC(g_oldWndProc)(hwnd, uMsg, wParam, lParam);
	
	if (uMsg == WM_INITMENU) {
		g_curMenuLevel = NULL;
	} else if (uMsg == WM_INITMENUPOPUP) {
		HMENU hMenu = (HMENU)wParam;
		WORD pos = LOWORD(lParam);

		if (g_last_pidl == NULL) {
			// If we dont have information to generate an icon source....
			ClassicMenuShell(hMenu);
			return ret;
		}

		if (g_curMenuLevel == NULL) {
			if (pos == 0) {
				g_rootMenuInfo.hMenu = hMenu;
				g_rootMenuInfo.hIconSource = NULL;
				g_rootMenuInfo.pContextMenu = NULL;

				ClassicMenuShell(hMenu);

				// Icon resource is not ready, then generate one
				g_rootMenuInfo.hIconSource = CreatePopupMenu();
				FillContextMenuFromPIDL(g_last_pidl, g_last_pidl_len, g_rootMenuInfo.hIconSource, g_last_QCMFlags, &(g_rootMenuInfo.pContextMenu));
				int menuItemCount = GetMenuItemCount(g_rootMenuInfo.hMenu);
				int menuItemCount2 = GetMenuItemCount(g_rootMenuInfo.hIconSource);

				g_curMenuLevel = PushMenuLevel(NULL, hMenu, 0, g_rootMenuInfo.hIconSource, 0);
			}
		}
		else {
			WORD rpos;
			if (FindIndexInResMenu(g_curMenuLevel->hMenuOriginal, pos, g_curMenuLevel->hMenuIconSource, &rpos)) {
				HMENU subMenuRes = GetSubMenu(g_curMenuLevel->hMenuIconSource, rpos);
				g_curMenuLevel = PushMenuLevel(g_curMenuLevel, hMenu, pos, subMenuRes, rpos);

				ClassicMenuShell2(hMenu, subMenuRes);
			}
			else {
				ClassicMenuShell(hMenu);
			}
		}
	}
	else if (uMsg == WM_UNINITMENUPOPUP) {
		if (g_curMenuLevel != NULL) {
			LPMenuLevel prevMenuLevel = g_curMenuLevel->parent;
			free(g_curMenuLevel);
			g_curMenuLevel = prevMenuLevel;
		}

		if (g_curMenuLevel == NULL) {
			// Menu closing

			if (g_rootMenuInfo.hIconSource != NULL) {
				// Release icon resource menu
				DestroyMenu(g_rootMenuInfo.hIconSource);
				// Release IContextMenu COM interface
				g_rootMenuInfo.pContextMenu->Release();

				g_rootMenuInfo.hMenu = NULL;
				g_rootMenuInfo.hIconSource = NULL;
				g_rootMenuInfo.pContextMenu = NULL;
			}
			
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
	//free(g_last_pidl);
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
	LPCITEMIDLIST* pidlList = (LPCITEMIDLIST*)param;
	DWORD len = (DWORD)pidlList[0];
	g_last_pidl_len = len;
	g_last_pidl = pidlList+1;
	return 0;
}

extern "C" _declspec(dllexport) DWORD  __cdecl  __SetContextMenuFlags(LPVOID param) {
	UINT flags = (UINT) param;

	g_last_QCMFlags = flags;
	return 0;
}

#pragma endregion