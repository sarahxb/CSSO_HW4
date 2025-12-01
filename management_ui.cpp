#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include "common.h"

#define ID_DEP_EDIT    101
#define ID_DEP_BROWSE  102
#define ID_SOLD_EDIT   103
#define ID_SOLD_BROWSE 104
#define ID_RUN_BTN     105
#define ID_LOGS_EDIT   201
#define ID_ERR_EDIT    202
#define ID_DON_STATIC  301
#define ID_SOLD_STATIC 302
#define TIMER_ID       1001

HWND hDepEdit, hSoldEdit, hRunBtn, hLogsEdit, hErrEdit, hDonStatic, hSoldStatic;
PROCESS_INFORMATION g_procInfo = { 0 };
std::map<std::string, LARGE_INTEGER> g_lastPos;

// --- Helpers: WinAPI folder browser ---
std::string BrowseForFolder(HWND owner) {
    BROWSEINFOA bi = {};
    bi.hwndOwner = owner;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return "";
    char path[MAX_PATH];
    SHGetPathFromIDListA(pidl, path);
    CoTaskMemFree(pidl);
    return std::string(path);
}

// --- WinAPI file read all ---
std::string ReadAllTextWinAPI(const char* path) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return "";
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart == 0) { CloseHandle(h); return ""; }
    std::string out;
    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    ReadFile(h, &out[0], (DWORD)out.size(), &read, NULL);
    CloseHandle(h);
    if (read == 0) return "";
    return out;
}

// --- Tail file using WinAPI ---
void TailFileToEditWinAPI(const char* path, HWND hEdit) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size)) { CloseHandle(h); return; }
    LARGE_INTEGER last = {};
    auto it = g_lastPos.find(path);
    if (it != g_lastPos.end()) last = it->second;
    if (size.QuadPart > last.QuadPart) {
        LARGE_INTEGER toRead;
        toRead.QuadPart = size.QuadPart - last.QuadPart;
        std::vector<char> buf((size_t)toRead.QuadPart + 1);
        LARGE_INTEGER move;
        move.QuadPart = last.QuadPart;
        SetFilePointerEx(h, move, NULL, FILE_BEGIN);
        DWORD read = 0;
        ReadFile(h, buf.data(), (DWORD)toRead.QuadPart, &read, NULL);
        buf[read] = 0;
        // append to edit control
        int len = GetWindowTextLengthA(hEdit);
        SendMessageA(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessageA(hEdit, EM_REPLACESEL, FALSE, (LPARAM)buf.data());
        SendMessageA(hEdit, EM_SCROLLCARET, 0, 0);
    }
    g_lastPos[path].QuadPart = size.QuadPart;
    CloseHandle(h);
}

// --- Recursively remove directory (WinAPI) ---
bool RemoveDirectoryRecursiveWinAPI(const std::string& dir) {
    std::string search = dir + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        // either empty or doesn't exist
        return RemoveDirectoryA(dir.c_str()) ? true : (GetLastError() == ERROR_FILE_NOT_FOUND);
    }
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        std::string full = dir + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            RemoveDirectoryRecursiveWinAPI(full);
        }
        else {
            SetFileAttributesA(full.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileA(full.c_str());
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    SetFileAttributesA(dir.c_str(), FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryA(dir.c_str()) ? true : false;
}

// --- Create directory (including parents) ---
bool CreateDirectoryRecursiveWinAPI(const std::string& dir) {
    char tmp[MAX_PATH];
    strncpy_s(tmp, dir.c_str(), MAX_PATH);
    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '\\') {
            *p = 0;
            CreateDirectoryA(tmp, NULL);
            *p = '\\';
        }
    }
    return CreateDirectoryA(tmp, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// --- Copy directory contents recursively using WinAPI ---
bool CopyDirectoryContentsWinAPI(const std::string& src, const std::string& dst) {
    // remove dst first
    RemoveDirectoryRecursiveWinAPI(dst);
    CreateDirectoryA(dst.c_str(), NULL);

    std::string search = src + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        // empty source or not found -> still OK (empty dest)
        return true;
    }
    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        std::string fullSrc = src + "\\" + name;
        std::string fullDst = dst + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CreateDirectoryRecursiveWinAPI(fullDst);
            CopyDirectoryContentsWinAPI(fullSrc, fullDst);
        }
        else {
            // ensure destination folder exists
            CreateDirectoryRecursiveWinAPI(dst);
            // copy file
            CopyFileA(fullSrc.c_str(), fullDst.c_str(), FALSE);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return true;
}

// --- Update summary UI values (donations, sold) using WinAPI reads ---
void UpdateSummaryValues(HWND hwnd) {
    std::string dpath = std::string(ROOT_PATH) + "\\Reports\\Summary\\donations.txt";
    std::string donations = ReadAllTextWinAPI(dpath.c_str());
    while (!donations.empty() && (donations.back() == '\r' || donations.back() == '\n')) donations.pop_back();
    if (donations.empty()) donations = "0.00";
    std::string dlabel = "Donations: " + donations;
    SetWindowTextA(hDonStatic, dlabel.c_str());

    std::string soldPath = std::string(ROOT_PATH) + "\\Reports\\Summary\\sold.txt";
    std::string sold = ReadAllTextWinAPI(soldPath.c_str());
    while (!sold.empty() && (sold.back() == '\r' || sold.back() == '\n')) sold.pop_back();
    if (sold.empty()) sold = "0";
    std::string slabel = "Sold (net): " + sold;
    SetWindowTextA(hSoldStatic, slabel.c_str());
}

// --- Start management.exe ---
void StartManagementProcess() {
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(NULL, (LPSTR)"management.exe", NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (ok) {
        if (g_procInfo.hProcess) {
            CloseHandle(g_procInfo.hProcess);
            CloseHandle(g_procInfo.hThread);
        }
        g_procInfo = pi;
    }
    else {
        MessageBoxA(NULL, "Failed to launch management.exe", "Error", MB_OK | MB_ICONERROR);
    }
}

// --- Window proc ---
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateWindowA("STATIC", "Deposit folder:", WS_VISIBLE | WS_CHILD, 10, 10, 100, 20, hwnd, NULL, NULL, NULL);
        hDepEdit = CreateWindowA("EDIT", ".\\deposit", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 120, 8, 360, 22, hwnd, (HMENU)ID_DEP_EDIT, NULL, NULL);
        CreateWindowA("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE, 490, 8, 80, 22, hwnd, (HMENU)ID_DEP_BROWSE, NULL, NULL);

        CreateWindowA("STATIC", "Sold folder:", WS_VISIBLE | WS_CHILD, 10, 40, 100, 20, hwnd, NULL, NULL, NULL);
        hSoldEdit = CreateWindowA("EDIT", ".\\sold", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 120, 38, 360, 22, hwnd, (HMENU)ID_SOLD_EDIT, NULL, NULL);
        CreateWindowA("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE, 490, 38, 80, 22, hwnd, (HMENU)ID_SOLD_BROWSE, NULL, NULL);

        hRunBtn = CreateWindowA("BUTTON", "Run Management", WS_CHILD | WS_VISIBLE, 10, 70, 200, 28, hwnd, (HMENU)ID_RUN_BTN, NULL, NULL);

        hDonStatic = CreateWindowA("STATIC", "Donations: 0.00", WS_CHILD | WS_VISIBLE, 230, 70, 200, 28, hwnd, (HMENU)ID_DON_STATIC, NULL, NULL);
        hSoldStatic = CreateWindowA("STATIC", "Sold (net): 0", WS_CHILD | WS_VISIBLE, 430, 70, 200, 28, hwnd, (HMENU)ID_SOLD_STATIC, NULL, NULL);

        CreateWindowA("STATIC", "Logs:", WS_CHILD | WS_VISIBLE, 10, 110, 50, 20, hwnd, NULL, NULL, NULL);
        hLogsEdit = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 10, 130, 560, 180, hwnd, (HMENU)ID_LOGS_EDIT, NULL, NULL);

        CreateWindowA("STATIC", "Errors:", WS_CHILD | WS_VISIBLE, 10, 320, 50, 20, hwnd, NULL, NULL, NULL);
        hErrEdit = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 10, 340, 560, 180, hwnd, (HMENU)ID_ERR_EDIT, NULL, NULL);

        // initialize tracked positions
        g_lastPos[std::string(ROOT_PATH) + "\\Reports\\logs.txt"] = {};
        g_lastPos[std::string(ROOT_PATH) + "\\Reports\\Summary\\errors.txt"] = {};
        SetTimer(hwnd, TIMER_ID, 500, NULL);
        break;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_DEP_BROWSE) {
            std::string p = BrowseForFolder(hwnd);
            if (!p.empty()) SetWindowTextA(hDepEdit, p.c_str());
        }
        else if (id == ID_SOLD_BROWSE) {
            std::string p = BrowseForFolder(hwnd);
            if (!p.empty()) SetWindowTextA(hSoldEdit, p.c_str());
        }
        else if (id == ID_RUN_BTN) {
            char buf[1024];
            GetWindowTextA(hDepEdit, buf, sizeof(buf));
            std::string depPath = buf;
            GetWindowTextA(hSoldEdit, buf, sizeof(buf));
            std::string soldPath = buf;

            // copy into local .\deposit and .\sold so backend finds them
            if (!CopyDirectoryContentsWinAPI(depPath, ".\\deposit") || !CopyDirectoryContentsWinAPI(soldPath, ".\\sold")) {
                MessageBoxA(hwnd, "Failed to prepare deposit/sold folders (check permissions).", "Error", MB_OK | MB_ICONERROR);
                break;
            }

            // clear logs/errors UI and reset tracked positions
            SetWindowTextA(hLogsEdit, "");
            SetWindowTextA(hErrEdit, "");
            g_lastPos[std::string(ROOT_PATH) + "\\Reports\\logs.txt"] = {};
            g_lastPos[std::string(ROOT_PATH) + "\\Reports\\Summary\\errors.txt"] = {};

            StartManagementProcess();
            EnableWindow(hRunBtn, FALSE);
        }
        break;
    }

    case WM_TIMER:
        TailFileToEditWinAPI((std::string(ROOT_PATH) + "\\Reports\\logs.txt").c_str(), hLogsEdit);
        TailFileToEditWinAPI((std::string(ROOT_PATH) + "\\Reports\\Summary\\errors.txt").c_str(), hErrEdit);
        UpdateSummaryValues(hwnd);

        if (g_procInfo.hProcess) {
            DWORD status = WaitForSingleObject(g_procInfo.hProcess, 0);
            if (status == WAIT_OBJECT_0) {
                CloseHandle(g_procInfo.hProcess);
                CloseHandle(g_procInfo.hThread);
                g_procInfo.hProcess = NULL;
                g_procInfo.hThread = NULL;
                EnableWindow(hRunBtn, TRUE);
                MessageBoxA(hwnd, "management.exe finished.", "Info", MB_OK);
            }
        }
        break;

    case WM_DESTROY:
        if (g_procInfo.hProcess) {
            TerminateProcess(g_procInfo.hProcess, 0);
            CloseHandle(g_procInfo.hProcess);
            CloseHandle(g_procInfo.hThread);
        }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "ManagementUI";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "Management UI", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT, 600, 580, NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}