#include <windows.h>
#include <shellapi.h>

#define STANDARD_DPI 96
#define DPI_SCALE(in) in * GetDPI() / STANDARD_DPI

HBITMAP bmp = NULL;

int GetDPI() {
	HDC hdc = GetDC(NULL);
	int DPI = GetDeviceCaps(hdc, LOGPIXELSX);
	ReleaseDC(NULL, hdc);
	return DPI;
}

HBITMAP IconToBitmap(HICON hIcon, INT size = 0)
{
	ICONINFO info = { 0 };
	if (hIcon == NULL
		|| !GetIconInfo(hIcon, &info)
		|| !info.fIcon)
	{
		//TO-DO: Free the hbmColor and hbmMask 
		return NULL;
	}

	INT nWidth = 0;
	INT nHeight = 0;
	if (size > 0)
	{
		nWidth = size;
		nHeight = size;
	}
	else
	{
		if (info.hbmColor != NULL)
		{
			BITMAP bmp = { 0 };
			GetObject(info.hbmColor, sizeof(bmp), &bmp);

			nWidth = bmp.bmWidth;
			nHeight = bmp.bmHeight;
		}
	}


	if (info.hbmColor != NULL)

	{

		DeleteObject(info.hbmColor);

		info.hbmColor = NULL;

	}



	if (info.hbmMask != NULL)

	{

		DeleteObject(info.hbmMask);

		info.hbmMask = NULL;

	}


	if (nWidth <= 0
		|| nHeight <= 0)
	{
		return NULL;
	}

	INT nPixelCount = nWidth * nHeight;

	HDC dc = GetDC(NULL);
	INT* pData = NULL;
	HDC dcMem = NULL;
	HBITMAP hBmpOld = NULL;
	bool* pOpaque = NULL;
	HBITMAP dib = NULL;
	BOOL bSuccess = FALSE;

	do
	{
		BITMAPINFOHEADER bi = { 0 };
		bi.biSize = sizeof(BITMAPINFOHEADER);
		bi.biWidth = nWidth;
		bi.biHeight = -nHeight;
		bi.biPlanes = 1;
		bi.biBitCount = 32;
		bi.biCompression = BI_RGB;
		dib = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, (VOID**)&pData, NULL, 0);
		if (dib == NULL) break;

		memset(pData, 0, nPixelCount * 4);

		dcMem = CreateCompatibleDC(dc);
		if (dcMem == NULL) break;

		hBmpOld = (HBITMAP)SelectObject(dcMem, dib);
		::DrawIconEx(dcMem, 0, 0, hIcon, nWidth, nHeight, 0, NULL, DI_MASK);

		pOpaque = (bool*)malloc(sizeof(bool)*nPixelCount);
		if (pOpaque == NULL) break;
		for (INT i = 0; i < nPixelCount; ++i)
		{
			pOpaque[i] = !pData[i];
		}

		memset(pData, 0, nPixelCount * 4);
		::DrawIconEx(dcMem, 0, 0, hIcon, nWidth, nHeight, 0, NULL, DI_NORMAL);

		BOOL bPixelHasAlpha = FALSE;
		UINT* pPixel = (UINT*)pData;
		for (INT i = 0; i<nPixelCount; ++i, ++pPixel)
		{
			if ((*pPixel & 0xff000000) != 0)
			{
				bPixelHasAlpha = TRUE;
				break;
			}
		}

		if (!bPixelHasAlpha)
		{
			pPixel = (UINT*)pData;
			for (INT i = 0; i <nPixelCount; ++i, ++pPixel)
			{
				if (pOpaque[i])
				{
					*pPixel |= 0xFF000000;
				}
				else
				{
					*pPixel &= 0x00FFFFFF;
				}
			}
		}

		bSuccess = TRUE;

	} while (FALSE);


	if (pOpaque != NULL)
	{
		free(pOpaque);
		pOpaque = NULL;
	}

	if (dcMem != NULL)
	{
		SelectObject(dcMem, hBmpOld);
		DeleteDC(dcMem);
	}

	ReleaseDC(NULL, dc);

	if (!bSuccess)
	{
		if (dib != NULL)
		{
			DeleteObject(dib);
			dib = NULL;
		}
	}

	return dib;
}

HBITMAP Icon_Get() {
	if (bmp != NULL)
		return bmp;

	HICON hIcon = NULL;
	ExtractIconEx(L"shell32.dll", 21, NULL, &hIcon, 1);
	bmp = IconToBitmap(hIcon, DPI_SCALE(16));
	DestroyIcon(hIcon);

	return bmp;
}

void Icon_Free() {
	DeleteObject(bmp);
	bmp = NULL;
}


