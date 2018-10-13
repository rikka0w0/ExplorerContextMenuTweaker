#include "tmt.h"
#include "minihook\MinHook.h"

static HMODULE gLibModule = 0;

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


typedef BOOL (WINAPI *FPT_TrackPopupMenu)(HMENU, UINT, int, int, int, HWND, CONST RECT*);

FPT_TrackPopupMenu fpTrackPopupMenu;

// Detour function which overrides TrackPopupMenu.
BOOL WINAPI HookedTrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, CONST RECT* prcRect) {
	HWND hTarget = IdentifyTarget(hWnd);
	if (hTarget != NULL) {
		ClassicMenuShell(hMenu);
	}


	BOOL ret = fpTrackPopupMenu(hMenu, uFlags, x, y, nReserved, hWnd, prcRect);

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