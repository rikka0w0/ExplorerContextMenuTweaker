/*
sent to: +0x18, dwItemData start with 0xaa0df00d before its submenu being populated

// Looks like tagMSAAMENUINFO (MSAAMENUINFO0
// https://docs.microsoft.com/en-us/windows/desktop/api/oleacc/ns-oleacc-tagmsaamenuinfo
struct open_with_submenu {
INT32 magicNumber;	// 0d f0 0d aa (0xaa0df00d)
INT32 length;		//
WCHAR* text;		// The menu text
};

other menu items: +0x20
*/

#include <stdlib.h>
#include <Windows.h>
#include <shlobj.h>
#include <commctrl.h>
#pragma comment(lib, "Comctl32.lib") 


#pragma region DataStructDeclaration
typedef struct tagMenuLevel *LPMenuLevel;

typedef struct tagMenuLevel {
	LPMenuLevel parent;
	HMENU hMenuOriginal;
	HMENU hMenuIconSource;
	WORD posInParent;
	WORD level;
} MenuLevel;

typedef struct tagHookRecord *LPHookRecord;

typedef struct tagHookRecord {
	HWND hWnd;
	LONG_PTR oldWndProc;
	LPHookRecord nextRecord;
} HookRecord;
#pragma endregion

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#pragma region GlobalVariables
// These are information necessary for generating a shell context menu
LPCITEMIDLIST g_pidl;
UINT g_QCMFlags;

LPHookRecord g_hookRecs;
#pragma endregion

#pragma region ShellHelper
// Callee need to DestroyMenu(hTopMenu);
HRESULT FillContextMenuFromPIDL(HWND hWnd, LPCITEMIDLIST pidl, HMENU hTopMenu, UINT uFlags)
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

				if (SUCCEEDED( pcm->QueryInterface(IID_IContextMenu2, (void**)&hContextMenu2) )) {
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
				
				ret =  MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
			}

			//pcm->Release();
		}
	}

	return ret;
}
#pragma endregion

void ClassicMenu(HMENU hMenu) {
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

void ClassicMenuEx(HMENU hMenu, HMENU hMenuIconSource) {
	// Reset the background
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

		MENUITEMINFO menuInfoSource;
		menuInfoSource.cbSize = sizeof(MENUITEMINFO);
		menuInfoSource.fMask = MIIM_DATA | MIIM_BITMAP | MIIM_FTYPE;
		GetMenuItemInfo(hMenuIconSource, i, true, &menuInfoSource);

		menuInfo.fType &= ~MFT_OWNERDRAW;
		menuInfo.fMask = MIIM_FTYPE;
		SetMenuItemInfo(hMenu, i, true, &menuInfo);

		if (menuInfo.hbmpItem == NULL && menuInfoSource.hbmpItem != NULL)
			SetMenuItemBitmaps(hMenu, i, MF_BYPOSITION, menuInfoSource.hbmpItem, NULL);
	}
}

BOOL CALLBACK EnumChildWindowHandler(HWND hwnd, LPARAM lParam)
{
	HWND* phWnd = (HWND*)lParam;
	char clsName[256];
	GetClassNameA(hwnd, clsName, sizeof(clsName));
	if (strcmp(clsName, "SHELLDLL_DefView") == 0) {
		*phWnd = hwnd;
		return FALSE;
	}

	return TRUE;
}

BOOL CALLBACK EnumChildWindowHandler2(HWND hwnd, LPARAM lParam)
{
	HWND* phWnd = (HWND*)lParam;
	char clsName[256];
	GetClassNameA(hwnd, clsName, sizeof(clsName));
	if (strcmp(clsName, "NamespaceTreeControl") == 0) {
		*phWnd = hwnd;
		return FALSE;
	}

	return TRUE;
}

void AddToHookRecord(HWND hWnd) {
	if (hWnd == NULL)
		return;

	LONG_PTR pWndProc = GetWindowLongPtr(hWnd, GWLP_WNDPROC);
	if (pWndProc == NULL)
		return;

	if (g_hookRecs != NULL) {
		LPHookRecord curRec = g_hookRecs;
		while (curRec != NULL) {
			if (curRec->hWnd == hWnd)
				return;
			curRec = curRec->nextRecord;
		}
	}
	
	LPHookRecord newRec = (LPHookRecord)malloc(sizeof(HookRecord));
	newRec->hWnd = hWnd;
	newRec->oldWndProc = pWndProc;
	newRec->nextRecord = NULL;

	// Add to the linkedlist
	if (g_hookRecs == NULL)
		g_hookRecs = newRec;
	else
		g_hookRecs->nextRecord = newRec;

	SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)&HookedWndProc);
}

LONG_PTR GetOldWndProc(HWND hWnd) {
	if (hWnd == NULL)
		return NULL;

	LPHookRecord curRec = g_hookRecs;
	while (curRec != NULL) {
		if (curRec->hWnd == hWnd) {
			return curRec->oldWndProc;
		}

		curRec = curRec->nextRecord;
	}

	return NULL;
}

int HookRecodeCount() {
	int count = 0;
	LPHookRecord curRec = g_hookRecs;
	while (curRec != NULL) {
		count++;
		curRec = curRec->nextRecord;
	}
	return count;
}

void UnhookWnd(HWND hWnd) {
	LPHookRecord prevRec = NULL;
	LPHookRecord curRec = g_hookRecs;
	while (curRec != NULL) {
		if (curRec->hWnd == hWnd) {
			// Unlink the current node, the next node pointed by nextRec
			LPHookRecord nextRec = curRec->nextRecord;
			if (prevRec == NULL)
				g_hookRecs = nextRec;
			else
				prevRec->nextRecord = nextRec;



			LONG_PTR OldWndProc = curRec->oldWndProc;
			if (OldWndProc != NULL) {
				SetWindowLongPtr(hWnd, GWLP_WNDPROC, OldWndProc);
			}

			// Release resources
			free(curRec);

			curRec = nextRec;
			return;
		}
		else {
			prevRec = curRec;
			curRec = curRec->nextRecord;
		}
	}
}

LRESULT CALLBACK mySubClassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
	//MessageBeep(0);
	
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void HookAllWnd() {
	HWND hForeWnd = GetForegroundWindow();
	if (hForeWnd == NULL)
		return;

	HWND hTopWnd = GetTopWindow(hForeWnd);
	char clsName[256];
	GetClassNameA(hTopWnd, clsName, sizeof(clsName));
	if (strcmp(clsName, "SHELLDLL_DefView") == 0) {
		AddToHookRecord(hTopWnd);
		return;
	}
	
	
	hForeWnd = GetAncestor(hForeWnd, GA_ROOTOWNER);
	HWND hShellDLLDV = NULL;
	EnumChildWindows(hForeWnd, EnumChildWindowHandler, (LPARAM)(&hShellDLLDV));
	if (hShellDLLDV != NULL) {
		AddToHookRecord(hShellDLLDV);
	}

	
	/*HWND hNSTC = NULL;
	EnumChildWindows(hForeWnd, EnumChildWindowHandler2, (LPARAM)(&hNSTC));
	if (hNSTC != NULL) {
		HWND hTarget = FindWindowExA(hNSTC, NULL, "SysTreeView32", NULL);
		if (hTarget != NULL) {
			GetClassNameA(hTarget, clsName, sizeof(clsName));
			MessageBoxA(0, clsName, "Class Name", 0);

			SetWindowSubclass(hTarget, &mySubClassProc, 2333, 0);
			//AddToHookRecord(hTarget);
		}
		else {
			MessageBoxA(0, "No SysTreeView32", "", 0);
		}
	}*/
}





LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static LPMenuLevel curMenuLevel;
	
	LONG_PTR OldWndProc = GetOldWndProc(hwnd);
	if (OldWndProc == NULL) {
		MessageBoxA(0, "fuck", "", 0);
	}

	if (uMsg == WM_INITMENUPOPUP) {
		HMENU hMenu = (HMENU)wParam;
		WORD pos = LOWORD(lParam);

		LRESULT ret = WNDPROC(OldWndProc)(hwnd, uMsg, wParam, lParam);

		if (pos == 0) {
			curMenuLevel = (LPMenuLevel)malloc(sizeof(MenuLevel));
			curMenuLevel->parent = NULL;													// Root
			curMenuLevel->hMenuOriginal = hMenu;
			curMenuLevel->hMenuIconSource = CreatePopupMenu();								// Create an empty menu
			FillContextMenuFromPIDL(0, g_pidl, curMenuLevel->hMenuIconSource, g_QCMFlags);	// And fill it
			curMenuLevel->posInParent = 0;													// Not applicable
			curMenuLevel->level = 0;														// Root

			ClassicMenuEx(hMenu, curMenuLevel->hMenuIconSource);
		}
		else {
			LPMenuLevel nextMenuLevel = (LPMenuLevel)malloc(sizeof(MenuLevel));
			nextMenuLevel->parent = curMenuLevel;
			nextMenuLevel->hMenuOriginal = hMenu;
			nextMenuLevel->hMenuIconSource = GetSubMenu(curMenuLevel->hMenuIconSource, pos);
			nextMenuLevel->posInParent = pos;
			nextMenuLevel->level = curMenuLevel->level + 1;
			curMenuLevel = nextMenuLevel;

			ClassicMenuEx(hMenu, curMenuLevel->hMenuIconSource);
		}

		return ret;
	}
	else if (uMsg == WM_UNINITMENUPOPUP) {
		if (curMenuLevel->parent == NULL) {
			// The root context menu has been closed

			// Release the hiden menu we created
			DestroyMenu(curMenuLevel->hMenuIconSource);
		}

		LPMenuLevel prevMenuLevel = curMenuLevel->parent;
		free(curMenuLevel);
		curMenuLevel = prevMenuLevel;

		return WNDPROC(OldWndProc)(hwnd, uMsg, wParam, lParam);
	}
	else if (uMsg == WM_MENUSELECT) {
		LRESULT ret = WNDPROC(OldWndProc)(hwnd, uMsg, wParam, lParam);

		HMENU hMenu = (HMENU)lParam;
		WORD menuFlags = HIWORD(wParam);
		if (menuFlags == 0xFFFF) {
			// Menu is closing
		} else if (menuFlags & MF_POPUP) {
			WORD pos = LOWORD(wParam);
			// Sub Menu

		} else {
			WORD menuID = LOWORD(wParam);
			// Command Item
		}

		return ret;
	}
	else if (uMsg == WM_EXITMENULOOP) {
		LRESULT ret = WNDPROC(OldWndProc)(hwnd, uMsg, wParam, lParam);
		
		UnhookWnd(hwnd);

		return ret;
	}
	else {
		return WNDPROC(OldWndProc)(hwnd, uMsg, wParam, lParam);
	}
}

void HookShell() {
	HookAllWnd();
}

HMODULE GetShellPayload() {
	return LoadLibraryA(
#ifdef _DEBUG
		"D:\\Administrator\\Desktop\\ExplorerContextMenuTweaker\\Debug\\x64\\ShellPayload.dll"
#else
		"ShellPayload.dll"
#endif
	);
}

void LoadHookDLL() {
	HMODULE hPayload = GetShellPayload();
	LPTHREAD_START_ROUTINE funcAddr = (LPTHREAD_START_ROUTINE)GetProcAddress(hPayload, "__PerformInjection");
	if (funcAddr == NULL) {
		MessageBoxA(0, "Unable to locate __PerformInjection() in ShellPayload.dll", "", 0);
		return;
	}
	HANDLE hThread = CreateThread(NULL, 0, funcAddr, NULL, 0, NULL);
	if (hThread == NULL) {
		MessageBoxA(0, "Failed to create thread __PerformInjection()", "", 0);
	}
	WaitForSingleObject(hThread, INFINITE);
}

DWORD g_PIDLArray_curId;
LPCITEMIDLIST* g_PIDLArray;
void StartBuildPIDLArray(DWORD length) {
	g_PIDLArray_curId = 1;
	g_PIDLArray = (LPCITEMIDLIST*)malloc(sizeof(LPCITEMIDLIST) * (length+1));
	g_PIDLArray[0] = (LPCITEMIDLIST)length;
}

void AddToPIDLArray(LPCITEMIDLIST pidl) {
	g_PIDLArray[g_PIDLArray_curId] = pidl;
	g_PIDLArray_curId++;
}

void SetCurrentPIDL() {
	HMODULE hPayload = GetShellPayload();
	LPTHREAD_START_ROUTINE funcAddr = (LPTHREAD_START_ROUTINE)GetProcAddress(hPayload, "__SetCurrentPIDL");
	if (funcAddr == NULL) {
		MessageBoxA(0, "Unable to locate __SetCurrentPIDL() in ShellPayload.dll", "", 0);
		return;
	}
	HANDLE hThread = CreateThread(NULL, 0, funcAddr, (LPVOID)g_PIDLArray, 0, NULL);
	if (hThread == NULL) {
		MessageBoxA(0, "Failed to create thread __SetCurrentPIDL()", "", 0);
	}
	WaitForSingleObject(hThread, INFINITE);
}

void SetContextMenuFlags(UINT flags) {
	HMODULE hPayload = GetShellPayload();
	LPTHREAD_START_ROUTINE funcAddr = (LPTHREAD_START_ROUTINE)GetProcAddress(hPayload, "__SetContextMenuFlags");
	if (funcAddr == NULL) {
		MessageBoxA(0, "Unable to locate __SetContextMenuFlags() in ShellPayload.dll", "", 0);
		return;
	}
	HANDLE hThread = CreateThread(NULL, 0, funcAddr, (LPVOID)flags, 0, NULL);
	if (hThread == NULL) {
		MessageBoxA(0, "Failed to create thread __SetContextMenuFlags()", "", 0);
	}
	WaitForSingleObject(hThread, INFINITE);
}