// Copyright © 2014 Brook Hong. All Rights Reserved.
//

// k.vim#cmd msbuild /p:platform=win32 /p:Configuration=Release && .\Release\keycastow.exe
// msbuild keycastow.vcxproj /t:Clean
// rc keycastow.rc && cl -DUNICODE -D_UNICODE keycast.cpp keylog.cpp keycastow.res user32.lib shell32.lib gdi32.lib Comdlg32.lib comctl32.lib

#include <windows.h>
#include <windowsx.h>
#include <Commctrl.h>
#include <stdio.h>

#include <d2d1.h>
template<class Interface> inline void SafeRelease( Interface **ppInterfaceToRelease) {
    if (*ppInterfaceToRelease != NULL)
    {
        (*ppInterfaceToRelease)->Release();

        (*ppInterfaceToRelease) = NULL;
    }
}
ID2D1Factory *pD2DFactory = NULL;
ID2D1DCRenderTarget *pDCRT = NULL;
ID2D1SolidColorBrush *pBrush = NULL;
ID2D1SolidColorBrush *pPen = NULL;
FLOAT dpiX, dpiY;

#include "resource.h"
#include "timer.h"
CTimer showTimer;

#define MAXCHARS 4096
WCHAR textBuffer[MAXCHARS];
LPCWSTR textBufferEnd = textBuffer + MAXCHARS;

struct KeyLabel{
    RECT rect;
    WCHAR *text;
    DWORD length;
    DWORD time;
    KeyLabel() {
        text = textBuffer;
        length = 0;
    }
};

#define BR(bgr) (bgr>>16|(bgr&0x0000ff00)|(bgr&0x000000ff)<<16)
COLORREF clearColor = RGB(255,255,255);
HBRUSH clearBrush = (HBRUSH)GetStockObject(WHITE_BRUSH);

DWORD keyStrokeDelay = 500;
DWORD lingerTime = 1200;
DWORD fadeDuration = 600;
DWORD labelSpacing = 30;
COLORREF textColor = RGB(0,240, 33);
COLORREF bgColor = RGB(0x7f,0,0x8f);
COLORREF borderColor = bgColor;
DWORD borderSize = 1;
LOGFONT labelFont;
DWORD opacity = 198;
UINT tcModifiers = MOD_ALT;
UINT tcKey = 0x42;      // 0x42 is 'b'
DWORD cornerSize = 32;
DWORD renderType = 0;   // 0 -- Direct2D, 1 -- GDI

#define MAXLABELS 60
KeyLabel keyLabels[MAXLABELS];
DWORD labelCount = 0;
RECT desktopRect;

#include "keycast.h"
#include "keylog.h"

WCHAR *szWinName = L"KeyCastOW";
HWND hMainWnd;
HINSTANCE hInstance;
HDC hdcBuffer;

#define IDI_TRAY       100
#define WM_TRAYMSG     101
#define MENU_CONFIG    32
#define MENU_EXIT      33
#define MENU_RESTORE      34
void DrawAlphaBlend (HDC hdcwnd, int i)
{
    ULONG   ulBitmapWidth, ulBitmapHeight;      // window width/height
    RECT &rt = keyLabels[i].rect;

    // calculate window width/height
    ulBitmapWidth = rt.right - rt.left;
    ulBitmapHeight = rt.bottom - rt.top;

    // make sure we have at least some window size
    if ((!ulBitmapWidth) || (!ulBitmapHeight))
        return;

    BLENDFUNCTION bf;      // structure for alpha blending
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    int alpha = (int)(255.0*keyLabels[i].time/fadeDuration);
    alpha = (alpha > 255) ? 255: alpha;
    bf.SourceConstantAlpha = alpha;  // half of 0xff = 50% transparency
    bf.AlphaFormat = 0;             // ignore source alpha channel

    GdiAlphaBlend(hdcwnd, rt.left, rt.top,
                ulBitmapWidth, ulBitmapHeight,
                hdcBuffer, rt.left, rt.top, ulBitmapWidth, ulBitmapHeight, bf);
}
void eraseLabel(int i) {
    RECT &rt = keyLabels[i].rect;
    FillRect(hdcBuffer, &rt, clearBrush);
    InvalidateRect(hMainWnd, &rt, TRUE);
}
void updateLabel(int i) {
    // update change within hdcBuffer, then use InvalidateRect to trigger WM_PAINT
    // where DrawAlphaBlend is called to update change within paint DC
    eraseLabel(i);

    RECT box = {};
    DrawText(hdcBuffer, keyLabels[i].text, keyLabels[i].length, &box, DT_CALCRECT);
    keyLabels[i].rect.right = box.right+18+borderSize*2;

    ULONG   ulBitmapWidth, ulBitmapHeight;      // window width/height
    RECT &rt = keyLabels[i].rect;

    // calculate window width/height
    ulBitmapWidth = rt.right - rt.left;
    ulBitmapHeight = rt.bottom - rt.top;

    if( ulBitmapWidth && ulBitmapHeight ) {
        // make sure we have at least some window size
        if(renderType) {
            FillRect(hdcBuffer, &rt, clearBrush);
            RoundRect(hdcBuffer, rt.left+borderSize, rt.top+borderSize, rt.left+ulBitmapWidth-borderSize, rt.top+ulBitmapHeight-borderSize, cornerSize, cornerSize);
        } else {
            pDCRT->BindDC(hdcBuffer, &rt);
            pDCRT->SetTransform(D2D1::Matrix3x2F::Translation(rt.left+0.0f, rt.top+0.0f));
            RECT rc = {0, 0, ulBitmapWidth, ulBitmapHeight};
            pDCRT->SetTransform(D2D1::Matrix3x2F::Identity());
            D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(
                    D2D1::RectF(borderSize*dpiX, borderSize*dpiY, (ulBitmapWidth-borderSize)*dpiX, (ulBitmapHeight-borderSize)*dpiY),
                    cornerSize*dpiX,
                    cornerSize*dpiY);
            pDCRT->BeginDraw();
            pDCRT->Clear(D2D1::ColorF( BR(clearColor), 1.0f ));
            pDCRT->FillRoundedRectangle(roundedRect, pBrush);
            pDCRT->DrawRoundedRectangle(roundedRect, pPen, borderSize*1.f);
            pDCRT->EndDraw();
        }
        TextOut(hdcBuffer, rt.left+8+borderSize, rt.top+1+borderSize, keyLabels[i].text, keyLabels[i].length);
        InvalidateRect(hMainWnd, &rt, TRUE);
    }
}

static int newStrokeCount = 0;
static void startFade() {
    if(newStrokeCount > 0) {
        newStrokeCount -= 100;
    }
    DWORD i = 0;
    for(i = 0; i < labelCount; i++) {
        if(keyLabels[i].time > fadeDuration) {
            keyLabels[i].time -= 100;
        } else if(keyLabels[i].time > 0) {
            keyLabels[i].time -= 100;
            InvalidateRect(hMainWnd, &keyLabels[i].rect, TRUE);
        }
    }
}

bool outOfLine(LPCWSTR text) {
    RECT box = {};
    size_t newLen = wcslen(text);
    if(keyLabels[labelCount-1].text+keyLabels[labelCount-1].length+newLen >= textBufferEnd) {
        wcscpy_s(textBuffer, MAXCHARS, keyLabels[labelCount-1].text);
        keyLabels[labelCount-1].text = textBuffer;
    }
    LPWSTR tmp = keyLabels[labelCount-1].text + keyLabels[labelCount-1].length;
    wcscpy_s(tmp, (textBufferEnd-tmp), text);
    DrawText(hdcBuffer, keyLabels[labelCount-1].text, keyLabels[labelCount-1].length+newLen, &box, DT_CALCRECT);
    RECT r;
    GetWindowRect(hMainWnd,&r);
    return (r.left+box.right+18+borderSize*2 >= (DWORD)desktopRect.right);
}
void updateClearColor() {
    COLORREF cr = clearColor;
    HDC hdc = GetDC(NULL); // get the desktop device context
    RECT r;
    GetWindowRect(hMainWnd,&r);

    DWORD i;
    int x, y;
    for (i = labelCount-1; i > 0; i--) {
        x = r.left+keyLabels[i].rect.left;
        y = r.top+keyLabels[i].rect.top;
        if(keyLabels[i].time == 0 && x > 0 && x < desktopRect.right && y > 0 && y < desktopRect.bottom) {
            cr = GetPixel(hdc, x, y);
            break;
        }
    }
    ReleaseDC(NULL, hdc);

    if( cr != clearColor) {
        clearColor = cr;
        HBRUSH cb = CreateSolidBrush(clearColor);
        SetClassLongPtr(hMainWnd, GCLP_HBRBACKGROUND, (LONG)cb);
        DeleteObject(clearBrush);
        clearBrush = cb;
        SetLayeredWindowAttributes(hMainWnd, clearColor, (BYTE)opacity, LWA_COLORKEY | LWA_ALPHA);
        InvalidateRect(hMainWnd, NULL, TRUE);
    }
}
void showText(LPCWSTR text, BOOL forceNewStroke = FALSE) {
    SetWindowPos(hMainWnd,HWND_TOPMOST,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE);
    if(renderType == 0) {
        updateClearColor();
    }
    size_t newLen = wcslen(text);
    if(forceNewStroke || (newStrokeCount <= 0) || outOfLine(text)) {
        DWORD i;
        for (i = 1; i < labelCount; i++) {
            if(keyLabels[i].time > 0) {
                break;
            }
        }
        for (; i < labelCount; i++) {
            eraseLabel(i-1);
            keyLabels[i-1].text = keyLabels[i].text;
            keyLabels[i-1].length = keyLabels[i].length;
            keyLabels[i-1].time = keyLabels[i].time;
            keyLabels[i-1].rect.right = keyLabels[i].rect.right;
            updateLabel(i-1);
            eraseLabel(i);
        }
        keyLabels[labelCount-1].text = keyLabels[labelCount-2].text + keyLabels[labelCount-2].length;
        if(keyLabels[labelCount-1].text+newLen >= textBufferEnd) {
            keyLabels[labelCount-1].text = textBuffer;
        }
        wcscpy_s(keyLabels[labelCount-1].text, textBufferEnd-keyLabels[labelCount-1].text, text);
        keyLabels[labelCount-1].length = newLen;

        keyLabels[labelCount-1].time = lingerTime+fadeDuration;
        updateLabel(labelCount-1);

        newStrokeCount = keyStrokeDelay;
    } else {
        LPWSTR tmp = keyLabels[labelCount-1].text + keyLabels[labelCount-1].length;
        if(tmp+newLen >= textBufferEnd) {
            tmp = textBuffer;
            keyLabels[labelCount-1].text = tmp;
            keyLabels[labelCount-1].length = newLen;
        } else {
            keyLabels[labelCount-1].length += newLen;
        }
        wcscpy_s(tmp, (textBufferEnd-tmp), text);
        keyLabels[labelCount-1].time = lingerTime+fadeDuration;
        updateLabel(labelCount-1);

        newStrokeCount = keyStrokeDelay;
    }
}

BOOL ColorDialog ( HWND hWnd, COLORREF &clr ) {
    DWORD dwCustClrs[16] = {
        RGB(0,0,0),
        RGB(0,0,255),
        RGB(0,255,0),
        RGB(128,255,255),
        RGB(255,0,0),
        RGB(255,0,255),
        RGB(255,255,0),
        RGB(192,192,192),
        RGB(127,127,127),
        RGB(0,0,128),
        RGB(0,128,0),
        RGB(0,255,255),
        RGB(128,0,0),
        RGB(255,0,128),
        RGB(128,128,64),
        RGB(255,255,255)
    };
    CHOOSECOLOR dlgColor;
    dlgColor.lStructSize = sizeof(CHOOSECOLOR);
    dlgColor.hwndOwner = hWnd;
    dlgColor.hInstance = NULL;
    dlgColor.lpTemplateName = NULL;
    dlgColor.rgbResult =  clr;
    dlgColor.lpCustColors =  dwCustClrs;
    dlgColor.Flags = CC_ANYCOLOR|CC_RGBINIT;
    dlgColor.lCustData = 0;
    dlgColor.lpfnHook = NULL;

    if(ChooseColor(&dlgColor)) {
        clr = dlgColor.rgbResult;
    }
    return TRUE;
}
void updateMainWindow() {
    SetLayeredWindowAttributes(hMainWnd, clearColor, (BYTE)opacity, LWA_COLORKEY | LWA_ALPHA);

    if(pBrush) {
        SafeRelease(&pBrush);
    }
    if(pPen) {
        SafeRelease(&pPen);
    }
    pDCRT->CreateSolidColorBrush( D2D1::ColorF(BR(borderColor)), &pPen);
    pDCRT->CreateSolidColorBrush( D2D1::ColorF(BR(bgColor)), &pBrush);
    HPEN pen = CreatePen(PS_SOLID, borderSize, borderColor);
    HPEN hPenOld = (HPEN)SelectObject(hdcBuffer, pen);
    DeleteObject(hPenOld);
    HBRUSH brush = CreateSolidBrush(bgColor);
    HBRUSH hBrushOld = (HBRUSH)SelectObject(hdcBuffer, brush);
    DeleteObject(hBrushOld);

    HFONT hlabelFont = CreateFontIndirect(&labelFont);
    HFONT hFontOld = (HFONT)SelectObject(hdcBuffer, hlabelFont);
    DeleteObject(hFontOld);
    SetTextColor(hdcBuffer, textColor);
    SetBkMode (hdcBuffer, TRANSPARENT);

    RECT box = {};
    DrawText(hdcBuffer, L"A", 1, &box, DT_CALCRECT);
    labelCount = (desktopRect.bottom - desktopRect.top) / (box.bottom+4+labelSpacing);

    if(labelCount > MAXLABELS)
        labelCount = MAXLABELS;

    for(DWORD i = 0; i < labelCount; i ++) {
        keyLabels[i].time = 0;
        keyLabels[i].rect.left = 0;
        keyLabels[i].rect.right = 0;
        keyLabels[i].rect.top = (box.bottom+4)*i+labelSpacing*i;
        keyLabels[i].rect.bottom = (box.bottom+4)*(i+1)+labelSpacing*i+borderSize*2;
        if(keyLabels[i].time > lingerTime+fadeDuration) {
            keyLabels[i].time = lingerTime+fadeDuration;
        }
        if(keyLabels[i].time > 0) {
            updateLabel(i);
        }
    }
}
void initSettings() {
    keyStrokeDelay = 500;
    lingerTime = 1200;
    fadeDuration = 600;
    labelSpacing = 30;
    textColor = RGB(0,240, 33);
    bgColor = RGB(0x7f,0,0x8f);
    opacity = 198;
    borderColor = bgColor;
    borderSize = 1;
    cornerSize = 32;
    renderType = 0;
    tcModifiers = MOD_ALT;
    tcKey = 0x42;
    memset(&labelFont, 0, sizeof(labelFont));
    labelFont.lfCharSet = DEFAULT_CHARSET;
    labelFont.lfHeight = -37;
    labelFont.lfPitchAndFamily = DEFAULT_PITCH;
    labelFont.lfWeight  = FW_NORMAL;
    labelFont.lfOutPrecision = OUT_DEFAULT_PRECIS;
    labelFont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    labelFont.lfQuality = ANTIALIASED_QUALITY;
    wcscpy_s(labelFont.lfFaceName, sizeof(labelFont.lfFaceName), TEXT("Arial"));
}
BOOL saveSettings() {
    BOOL res = TRUE;

    HKEY hRootKey, hChildKey;
    if(RegOpenCurrentUser(KEY_WRITE, &hRootKey) != ERROR_SUCCESS)
        return FALSE;

    if(RegCreateKeyEx(hRootKey, L"Software\\KeyCastOW", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, NULL, &hChildKey, NULL) != ERROR_SUCCESS) {
        RegCloseKey(hRootKey);
        return FALSE;
    }

    if(RegSetKeyValue(hChildKey, NULL, L"keyStrokeDelay", REG_DWORD, (LPCVOID)&keyStrokeDelay, sizeof(keyStrokeDelay)) != ERROR_SUCCESS) {
        res = FALSE;
    }

    RegSetKeyValue(hChildKey, NULL, L"lingerTime", REG_DWORD, (LPCVOID)&lingerTime, sizeof(lingerTime));
    RegSetKeyValue(hChildKey, NULL, L"fadeDuration", REG_DWORD, (LPCVOID)&fadeDuration, sizeof(fadeDuration));
    RegSetKeyValue(hChildKey, NULL, L"labelSpacing", REG_DWORD, (LPCVOID)&labelSpacing, sizeof(labelSpacing));
    RegSetKeyValue(hChildKey, NULL, L"bgColor", REG_DWORD, (LPCVOID)&bgColor, sizeof(bgColor));
    RegSetKeyValue(hChildKey, NULL, L"textColor", REG_DWORD, (LPCVOID)&textColor, sizeof(textColor));
    RegSetKeyValue(hChildKey, NULL, L"labelFont", REG_BINARY, (LPCVOID)&labelFont, sizeof(labelFont));
    RegSetKeyValue(hChildKey, NULL, L"opacity", REG_DWORD, (LPCVOID)&opacity, sizeof(opacity));
    RegSetKeyValue(hChildKey, NULL, L"tcModifiers", REG_DWORD, (LPCVOID)&tcModifiers, sizeof(tcModifiers));
    RegSetKeyValue(hChildKey, NULL, L"tcKey", REG_DWORD, (LPCVOID)&tcKey, sizeof(tcKey));
    RegSetKeyValue(hChildKey, NULL, L"borderColor", REG_DWORD, (LPCVOID)&borderColor, sizeof(borderColor));
    RegSetKeyValue(hChildKey, NULL, L"borderSize", REG_DWORD, (LPCVOID)&borderSize, sizeof(borderSize));
    RegSetKeyValue(hChildKey, NULL, L"cornerSize", REG_DWORD, (LPCVOID)&cornerSize, sizeof(cornerSize));
    RegSetKeyValue(hChildKey, NULL, L"renderType", REG_DWORD, (LPCVOID)&renderType, sizeof(renderType));

    RegCloseKey(hRootKey);
    RegCloseKey(hChildKey);
    return res;
}
BOOL loadSettings() {
    BOOL res = TRUE;
    HKEY hRootKey, hChildKey;
    DWORD disposition; // For checking if key was created or only opened
    initSettings();
    if(RegOpenCurrentUser(KEY_WRITE | KEY_READ, &hRootKey) != ERROR_SUCCESS)
        return FALSE;
    if(RegCreateKeyEx(hRootKey, TEXT("SOFTWARE\\KeyCastOW"), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE,
                NULL, &hChildKey, &disposition) != ERROR_SUCCESS) {
        RegCloseKey(hRootKey);
        return FALSE;
    }

    DWORD size = sizeof(DWORD);
    if(disposition == REG_OPENED_EXISTING_KEY) {
        RegGetValue(hChildKey, NULL, L"keyStrokeDelay", RRF_RT_DWORD, NULL, &keyStrokeDelay, &size);
        RegGetValue(hChildKey, NULL, L"lingerTime", RRF_RT_DWORD, NULL, &lingerTime, &size);
        RegGetValue(hChildKey, NULL, L"fadeDuration", RRF_RT_DWORD, NULL, &fadeDuration, &size);
        RegGetValue(hChildKey, NULL, L"labelSpacing", RRF_RT_DWORD, NULL, &labelSpacing, &size);
        RegGetValue(hChildKey, NULL, L"bgColor", RRF_RT_DWORD, NULL, &bgColor, &size);
        RegGetValue(hChildKey, NULL, L"textColor", RRF_RT_DWORD, NULL, &textColor, &size);
        RegGetValue(hChildKey, NULL, L"opacity", RRF_RT_DWORD, NULL, &opacity, &size);
        RegGetValue(hChildKey, NULL, L"tcModifiers", RRF_RT_DWORD, NULL, &tcModifiers, &size);
        RegGetValue(hChildKey, NULL, L"tcKey", RRF_RT_DWORD, NULL, &tcKey, &size);
        RegGetValue(hChildKey, NULL, L"borderColor", RRF_RT_DWORD, NULL, &borderColor, &size);
        RegGetValue(hChildKey, NULL, L"borderSize", RRF_RT_DWORD, NULL, &borderSize, &size);
        RegGetValue(hChildKey, NULL, L"cornerSize", RRF_RT_DWORD, NULL, &cornerSize, &size);
        RegGetValue(hChildKey, NULL, L"renderType", RRF_RT_DWORD, NULL, &renderType, &size);

        size = sizeof(labelFont);
        RegGetValue(hChildKey, NULL, L"labelFont", RRF_RT_REG_BINARY, NULL, &labelFont, &size);
    } else {
        saveSettings();
    }

    RegCloseKey(hRootKey);
    RegCloseKey(hChildKey);
    return res;
}
BOOL CALLBACK SettingsWndProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WCHAR tmp[256];
    switch (msg)
    {
        case WM_INITDIALOG:
            {
                swprintf(tmp, 256, L"%d", keyStrokeDelay);
                SetDlgItemText(hwndDlg, IDC_KEYSTROKEDELAY, tmp);
                swprintf(tmp, 256, L"%d", lingerTime);
                SetDlgItemText(hwndDlg, IDC_LINGERTIME, tmp);
                swprintf(tmp, 256, L"%d", fadeDuration);
                SetDlgItemText(hwndDlg, IDC_FADEDURATION, tmp);
                swprintf(tmp, 256, L"%d", labelSpacing);
                SetDlgItemText(hwndDlg, IDC_LABELSPACING, tmp);
                swprintf(tmp, 256, L"%d", opacity);
                SetDlgItemText(hwndDlg, IDC_OPACITY, tmp);
                swprintf(tmp, 256, L"%d", borderSize);
                SetDlgItemText(hwndDlg, IDC_BORDERSIZE, tmp);
                swprintf(tmp, 256, L"%d", cornerSize);
                SetDlgItemText(hwndDlg, IDC_CORNERSIZE, tmp);
                ComboBox_AddString(GetDlgItem(hwndDlg, IDC_RENDER), L"Direct2D");
                ComboBox_AddString(GetDlgItem(hwndDlg, IDC_RENDER), L"GDI");
                ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_RENDER), renderType);
                CheckDlgButton(hwndDlg, IDC_MODCTRL, (tcModifiers & MOD_CONTROL) ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hwndDlg, IDC_MODALT, (tcModifiers & MOD_ALT) ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hwndDlg, IDC_MODSHIFT, (tcModifiers & MOD_SHIFT) ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hwndDlg, IDC_MODWIN, (tcModifiers & MOD_WIN) ? BST_CHECKED : BST_UNCHECKED);
                swprintf(tmp, 256, L"%c", MapVirtualKey(tcKey, MAPVK_VK_TO_CHAR));
                SetDlgItemText(hwndDlg, IDC_TCKEY, tmp);
                RECT r;
                GetWindowRect(hwndDlg, &r);
                SetWindowPos(hwndDlg, 0, desktopRect.right - r.right + r.left, desktopRect.bottom - r.bottom + r.top, 0, 0, SWP_NOSIZE);
            }
            return TRUE;
        case WM_NOTIFY:
            switch (((LPNMHDR)lParam)->code)
            {

                case NM_CLICK:          // Fall through to the next case.
                case NM_RETURN:
                    {
                        PNMLINK pNMLink = (PNMLINK)lParam;
                        LITEM   item    = pNMLink->item;
                        ShellExecute(NULL, L"open", item.szUrl, NULL, NULL, SW_SHOW);
                        break;
                    }
            }

            break;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_TEXTFONT:
                    {
                        CHOOSEFONT cf ;
                        cf.lStructSize    = sizeof (CHOOSEFONT) ;
                        cf.hwndOwner      = hwndDlg ;
                        cf.hDC            = NULL ;
                        cf.lpLogFont      = &labelFont ;
                        cf.iPointSize     = 0 ;
                        cf.Flags          = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_EFFECTS ;
                        cf.rgbColors      = 0 ;
                        cf.lCustData      = 0 ;
                        cf.lpfnHook       = NULL ;
                        cf.lpTemplateName = NULL ;
                        cf.hInstance      = NULL ;
                        cf.lpszStyle      = NULL ;
                        cf.nFontType      = 0 ;               // Returned from ChooseFont
                        cf.nSizeMin       = 0 ;
                        cf.nSizeMax       = 0 ;

                        if(ChooseFont (&cf)) {
                            updateMainWindow();
                            InvalidateRect(hMainWnd, NULL, TRUE);
                            saveSettings();
                        }
                    }
                    return TRUE;
                case IDC_TEXTCOLOR:
                    if( ColorDialog(hwndDlg, textColor) ) {
                        updateMainWindow();
                        InvalidateRect(hMainWnd, NULL, TRUE);
                        saveSettings();
                    }
                    return TRUE;
                case IDC_BGCOLOR:
                    if( ColorDialog(hwndDlg, bgColor) ) {
                        updateMainWindow();
                        InvalidateRect(hMainWnd, NULL, TRUE);
                        saveSettings();
                    }
                    return TRUE;
                case IDC_BORDERCOLOR:
                    if( ColorDialog(hwndDlg, borderColor) ) {
                        updateMainWindow();
                        InvalidateRect(hMainWnd, NULL, TRUE);
                        saveSettings();
                    }
                    return TRUE;
                case IDOK:
                    GetDlgItemText(hwndDlg, IDC_KEYSTROKEDELAY, tmp, 256);
                    keyStrokeDelay = _wtoi(tmp);
                    GetDlgItemText(hwndDlg, IDC_LINGERTIME, tmp, 256);
                    lingerTime = _wtoi(tmp);
                    GetDlgItemText(hwndDlg, IDC_FADEDURATION, tmp, 256);
                    fadeDuration = _wtoi(tmp);
                    GetDlgItemText(hwndDlg, IDC_LABELSPACING, tmp, 256);
                    labelSpacing = _wtoi(tmp);
                    GetDlgItemText(hwndDlg, IDC_OPACITY, tmp, 256);
                    opacity = _wtoi(tmp);
                    GetDlgItemText(hwndDlg, IDC_BORDERSIZE, tmp, 256);
                    borderSize = _wtoi(tmp);
                    GetDlgItemText(hwndDlg, IDC_CORNERSIZE, tmp, 256);
                    cornerSize = _wtoi(tmp);
                    renderType = ComboBox_GetCurSel(GetDlgItem(hwndDlg, IDC_RENDER));
                    tcModifiers = 0;
                    if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_MODCTRL)) {
                        tcModifiers |= MOD_CONTROL;
                    }
                    if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_MODALT)) {
                        tcModifiers |= MOD_ALT;
                    }
                    if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_MODSHIFT)) {
                        tcModifiers |= MOD_SHIFT;
                    }
                    if(BST_CHECKED == IsDlgButtonChecked(hwndDlg, IDC_MODWIN)) {
                        tcModifiers |= MOD_WIN;
                    }
                    GetDlgItemText(hwndDlg, IDC_TCKEY, tmp, 256);
                    if(tcModifiers != 0 && tmp[0] != '\0') {
                        tcKey = VkKeyScanEx(tmp[0], GetKeyboardLayout(0));
                        UnregisterHotKey(NULL, 1);
                        if (!RegisterHotKey( NULL, 1, tcModifiers | MOD_NOREPEAT, tcKey)) {
                            MessageBox(NULL, L"Unable to register hotkey, you probably need go to settings to redefine your hotkey for toggle capturing.", L"Warning", MB_OK|MB_ICONWARNING);
                        }
                    }
                    updateMainWindow();
                    InvalidateRect(hMainWnd, NULL, TRUE);
                    saveSettings();
                case IDCANCEL:
                    EndDialog(hwndDlg, wParam);
                    return TRUE;
            }
    }
    return FALSE;
}
LRESULT CALLBACK WindowFunc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static POINT s_last_mouse;
    static HMENU hPopMenu;
    static NOTIFYICONDATA nid;


    switch(message)
    {
        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc;
                DWORD i;
                hdc = BeginPaint(hWnd, &ps);
                for(i = 0; i < labelCount; i ++) {
                    DrawAlphaBlend(hdc, i);
                }
                EndPaint(hWnd, &ps);
            }
            break;

        // trayicon
        case WM_CREATE:
            {
                memset( &nid, 0, sizeof( nid ) );

                nid.cbSize              = sizeof( nid );
                nid.hWnd                = hWnd;
                nid.uID                 = IDI_TRAY;
                nid.uFlags              = NIF_ICON | NIF_MESSAGE | NIF_TIP;
                nid.uCallbackMessage    = WM_TRAYMSG;
                nid.hIcon = LoadIcon( hInstance, MAKEINTRESOURCE(IDI_ICON1));
                lstrcpy( nid.szTip, L"KeyCast On Windows by brook hong" );
                Shell_NotifyIcon( NIM_ADD, &nid );

                hPopMenu = CreatePopupMenu();
                AppendMenu( hPopMenu, MF_STRING, MENU_CONFIG,  L"&Settings..." );
                AppendMenu( hPopMenu, MF_STRING, MENU_RESTORE,  L"&Restore default settings" );
                AppendMenu( hPopMenu, MF_STRING, MENU_EXIT,    L"E&xit" );
                SetMenuDefaultItem( hPopMenu, MENU_CONFIG, FALSE );
            }
            break;
        case WM_TRAYMSG:
            {
                switch ( lParam )
                {
                    case WM_RBUTTONUP:
                        {
                            POINT pnt;
                            GetCursorPos( &pnt );
                            SetForegroundWindow( hWnd ); // needed to get keyboard focus
                            TrackPopupMenu( hPopMenu, TPM_LEFTALIGN, pnt.x, pnt.y, 0, hWnd, NULL );
                        }
                        break;
                    case WM_LBUTTONDBLCLK:
                        SendMessage( hWnd, WM_COMMAND, MENU_CONFIG, 0 );
                        return 0;
                }
            }
            break;
        case WM_COMMAND:
            {
                switch ( LOWORD( wParam ) )
                {
                    case MENU_CONFIG:
                        if (DialogBox(NULL,
                                    MAKEINTRESOURCE(IDD_DLGSETTINGS),
                                    hWnd,
                                    (DLGPROC)SettingsWndProc)==IDOK) {
                            // Complete the command; szItemName contains the
                            // name of the item to delete.
                        } else {
                            // Cancel the command.
                        }
                        break;
                    case MENU_RESTORE:
                        initSettings();
                        saveSettings();
                        updateMainWindow();
                        InvalidateRect(hMainWnd, NULL, TRUE);
                        break;
                    case MENU_EXIT:
                        Shell_NotifyIcon( NIM_DELETE, &nid );
                        ExitProcess(0);
                        break;
                    default:
                        break;
                }
            }
            break;

        // hold mouse to move
        case WM_LBUTTONDOWN:
            SetCapture(hWnd);
            GetCursorPos(&s_last_mouse);
            showTimer.Stop();
            break;
        case WM_MOUSEMOVE:
            if (GetCapture()==hWnd)
            {
                POINT p;
                GetCursorPos(&p);
                int dx= p.x - s_last_mouse.x;
                int dy= p.y - s_last_mouse.y;
                if (dx||dy)
                {
                    s_last_mouse=p;
                    RECT r;
                    GetWindowRect(hWnd,&r);
                    SetWindowPos(hWnd,HWND_TOPMOST,r.left+dx,r.top+dy,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
                }
            }
            break;
        case WM_LBUTTONUP:
            ReleaseCapture();
            showTimer.Start(100);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hThisInst, HINSTANCE hPrevInst,
        LPSTR lpszArgs, int nWinMode)
{
    WNDCLASSEX wcl;
    MSG        msg;

    hInstance = hThisInst;

    if (SUCCEEDED(CoInitialize(NULL))) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pD2DFactory);
        // Create a DC render target.
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    D2D1_ALPHA_MODE_PREMULTIPLIED),
                0,
                0,
                D2D1_RENDER_TARGET_USAGE_NONE,
                D2D1_FEATURE_LEVEL_DEFAULT
                );

        pD2DFactory->GetDesktopDpi(&dpiX, &dpiY);
        dpiX /= 96.f;
        dpiY /= 96.f;
        pD2DFactory->CreateDCRenderTarget(&props, &pDCRT);
    }
    wcl.cbSize = sizeof(WNDCLASSEX);
    wcl.hInstance = hThisInst;
    wcl.lpszClassName = szWinName;
    wcl.lpfnWndProc = WindowFunc;
    wcl.style = CS_DBLCLKS;
    wcl.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcl.hIconSm = LoadIcon(NULL, IDI_WINLOGO);
    wcl.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcl.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wcl.lpszMenuName = NULL;
    wcl.cbWndExtra = 0;
    wcl.cbClsExtra = 0;

    if(!RegisterClassEx(&wcl) )    {
        MessageBox(NULL, L"Could not register window class", L"Error", MB_OK);
        return 0;
    }

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LINK_CLASS|ICC_LISTVIEW_CLASSES|ICC_PAGESCROLLER_CLASS
        |ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES|ICC_TAB_CLASSES|ICC_TREEVIEW_CLASSES
        |ICC_UPDOWN_CLASS|ICC_USEREX_CLASSES|ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    hMainWnd = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            szWinName,
            szWinName,
            WS_POPUP,
            0, 0,            //X and Y position of window
            0, 0,            //Width and height of window
            NULL,
            NULL,
            hThisInst,
            NULL
            );
    if( !hMainWnd)    {
        MessageBox(NULL, L"Could not create window", L"Error", MB_OK);
        return 0;
    }
    loadSettings();
    if (!RegisterHotKey( NULL, 1, tcModifiers | MOD_NOREPEAT, tcKey)) {
        MessageBox(NULL, L"Unable to register hotkey, you probably need go to settings to redefine your hotkey for toggle capturing.", L"Warning", MB_OK|MB_ICONWARNING);
    }

    SystemParametersInfo(SPI_GETWORKAREA,NULL,&desktopRect,NULL);
    SetWindowPos(hMainWnd, HWND_TOPMOST, desktopRect.right*4/5, 0, desktopRect.right, desktopRect.bottom, 0);
    UpdateWindow(hMainWnd);

    HDC hdc = GetDC(hMainWnd);
    hdcBuffer = CreateCompatibleDC(hdc);
    HBITMAP hbitmap = CreateCompatibleBitmap(hdc, desktopRect.right, desktopRect.bottom);
    HBITMAP hBitmapOld = SelectBitmap(hdcBuffer, hbitmap);
    ReleaseDC(hMainWnd, hdc);
    DeleteObject(hBitmapOld);

    updateMainWindow();
    ShowWindow(hMainWnd, SW_SHOW);

    showTimer.OnTimedEvent = startFade;
    showTimer.Start(100);

    kbdhook = SetWindowsHookEx(WH_KEYBOARD_LL, LLKeyboardProc, hThisInst, NULL);

    while( GetMessage(&msg, NULL, 0, 0) )    {
        if (msg.message == WM_HOTKEY) {
            if(kbdhook) {
                showText(L"\u263b - KeyCastOW OFF", TRUE);
                UnhookWindowsHookEx(kbdhook);
                kbdhook = NULL;
            } else {
                showText(L"\u263b - KeyCastOW ON", TRUE);
                kbdhook = SetWindowsHookEx(WH_KEYBOARD_LL, LLKeyboardProc, hInstance, NULL);
            }
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    UnhookWindowsHookEx(kbdhook);
    UnregisterHotKey(NULL, 1);

    if(pD2DFactory) {
        SafeRelease(&pD2DFactory);
        SafeRelease(&pDCRT);
        SafeRelease(&pBrush);
        SafeRelease(&pPen);
        CoUninitialize();
    }
    return msg.wParam;
}
