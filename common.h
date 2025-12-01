#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>
#include <set>

// === CONFIGURARE ===
#define SHELVES_SIZE 10000
#define MAX_BUFFER 8192 

// NOTA: Schimba aici daca vrei sa testezi in alt folder, 
// dar la predare trebuie sa fie "C:\\Facultate\\CSSO\\H4"
#define ROOT_PATH "C:\\Facultate\\CSSO\\H4"

// === NUME OBIECTE SINCRONIZARE (V2 - Nume noi pentru a evita blocaje vechi) ===
const wchar_t* MUTEX_SHELVES = L"Local\\MutexMarketShelves_V2";
const wchar_t* MUTEX_ERRORS = L"Local\\MutexErrors_V2";
const wchar_t* MUTEX_SOLD = L"Local\\MutexSold_V2";
const wchar_t* MUTEX_DONATIONS = L"Local\\MutexDonations_V2";

// Semaforul care cauza problemele (acum are nume nou si curat)
const wchar_t* SEM_LOGS = L"Local\\SemaphoreLogs_V2";

const wchar_t* MAP_SHELVES = L"Local\\MapShelves_V2";
const wchar_t* MAP_VALABILITY = L"Local\\MapValability_V2";
const wchar_t* MAP_PRICES = L"Local\\MapPrices_V2";

const wchar_t* EVT_START_DAY = L"Local\\EvtStartDay_V2";
const wchar_t* EVT_DEP_DONE = L"Local\\EvtDepositDone_V2";
const wchar_t* EVT_SELL_DONE = L"Local\\EvtSellDone_V2";
const wchar_t* EVT_DON_DONE = L"Local\\EvtDonateDone_V2";

// Helper pentru cai absolute
static std::string GetPath(const char* subpath) {
    char path[MAX_PATH];
    sprintf(path, "%s\\%s", ROOT_PATH, subpath);
    return std::string(path);
}

// === FUNCȚII I/O ===

static void AppendToFile(const char* filePath, const char* message) {
    HANDLE hFile = CreateFileA(filePath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten;
        char buffer[MAX_BUFFER];
        sprintf(buffer, "%s\r\n", message);
        WriteFile(hFile, buffer, (DWORD)strlen(buffer), &bytesWritten, NULL);
        CloseHandle(hFile);
    }
}

static void OverwriteFile(const char* filePath, const char* message) {
    HANDLE hFile = CreateFileA(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten;
        WriteFile(hFile, message, (DWORD)strlen(message), &bytesWritten, NULL);
        CloseHandle(hFile);
    }
}

static bool ReadAllTextWinAPI(const char* filePath, char* outBuffer, DWORD bufferSize) {
    HANDLE hFile = CreateFileA(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD bytesRead = 0;
    memset(outBuffer, 0, bufferSize);
    BOOL res = ReadFile(hFile, outBuffer, bufferSize - 1, &bytesRead, NULL);
    CloseHandle(hFile);
    return res && (bytesRead > 0);
}

static std::vector<std::string> GetSimulationDates(const char* dirDeposit, const char* dirSold) {
    std::set<std::string> uniqueDates;
    char searchPath[MAX_PATH];
    WIN32_FIND_DATAA findData;

    sprintf(searchPath, "%s\\*.*", dirDeposit);
    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) uniqueDates.insert(findData.cFileName);
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }

    sprintf(searchPath, "%s\\*.*", dirSold);
    hFind = FindFirstFileA(searchPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) uniqueDates.insert(findData.cFileName);
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }

    std::vector<std::string> sortedFiles(uniqueDates.begin(), uniqueDates.end());
    std::sort(sortedFiles.begin(), sortedFiles.end(), [](const std::string& a, const std::string& b) {
        int y1, m1, d1, y2, m2, d2;
        sscanf(a.c_str(), "%d.%d.%d", &y1, &m1, &d1);
        sscanf(b.c_str(), "%d.%d.%d", &y2, &m2, &d2);
        if (y1 != y2) return y1 < y2;
        if (m1 != m2) return m1 < m2;
        return d1 < d2;
        });

    return sortedFiles;
}