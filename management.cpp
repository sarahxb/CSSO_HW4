#include "common.h"

HANDLE hMapShelves, hMapValability, hMapPrices;
HANDLE hMutexShelves, hMutexErrors, hMutexSold, hMutexDonations;
HANDLE hSemLogs;
HANDLE hEvtStartDay, hEvtDonDone;
DWORD* shelves, * valability, * prices;

void SetupFileSystem() {
    // Creaza folderele recursiv prin comanda de sistem
    char cmd[MAX_PATH];
    sprintf(cmd, "mkdir \"%s\\Reports\\Summary\"", ROOT_PATH);
    system(cmd);

    std::string pSold = GetPath("Reports\\Summary\\sold.txt");
    std::string pDon = GetPath("Reports\\Summary\\donations.txt");
    std::string pErr = GetPath("Reports\\Summary\\errors.txt");
    std::string pLog = GetPath("Reports\\logs.txt");

    OverwriteFile(pSold.c_str(), "0.00");
    OverwriteFile(pDon.c_str(), "0.00");
    DeleteFileA(pErr.c_str());
    DeleteFileA(pLog.c_str());
}

bool SetupMemoryAndSync() {
    hMapShelves = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, SHELVES_SIZE * sizeof(DWORD), MAP_SHELVES);
    hMapValability = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, SHELVES_SIZE * sizeof(DWORD), MAP_VALABILITY);
    hMapPrices = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, SHELVES_SIZE * sizeof(DWORD), MAP_PRICES);

    if (!hMapShelves || !hMapValability || !hMapPrices) return false;

    shelves = (DWORD*)MapViewOfFile(hMapShelves, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    valability = (DWORD*)MapViewOfFile(hMapValability, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    prices = (DWORD*)MapViewOfFile(hMapPrices, FILE_MAP_ALL_ACCESS, 0, 0, 0);

    for (int i = 0; i < SHELVES_SIZE; i++) {
        shelves[i] = 0xFFFFFFFF;
        valability[i] = 0xFFFFFFFF;
        prices[i] = 0xFFFFFFFF;
    }

    hMutexShelves = CreateMutexW(NULL, FALSE, MUTEX_SHELVES);
    hMutexErrors = CreateMutexW(NULL, FALSE, MUTEX_ERRORS);
    hMutexSold = CreateMutexW(NULL, FALSE, MUTEX_SOLD);
    hMutexDonations = CreateMutexW(NULL, FALSE, MUTEX_DONATIONS);
    hSemLogs = CreateSemaphoreW(NULL, 1, 1, SEM_LOGS);

    hEvtStartDay = CreateEventW(NULL, FALSE, FALSE, EVT_START_DAY);
    CreateEventW(NULL, FALSE, FALSE, EVT_DEP_DONE);
    CreateEventW(NULL, FALSE, FALSE, EVT_SELL_DONE);
    hEvtDonDone = CreateEventW(NULL, FALSE, FALSE, EVT_DON_DONE);

    return true;
}

void LogManagementError(const char* msg) {
    WaitForSingleObject(hMutexErrors, INFINITE);
    std::string path = GetPath("Reports\\Summary\\errors.txt");
    AppendToFile(path.c_str(), msg);
    ReleaseMutex(hMutexErrors);
}

void ValidateInvariants(const char* dateStr) {
    char msgBuffer[512];
    bool* presentOnShelves = (bool*)calloc(SHELVES_SIZE, sizeof(bool));

    // Check 1: Shelf -> Valid Product
    for (int i = 0; i < SHELVES_SIZE; i++) {
        if (shelves[i] != 0xFFFFFFFF) {
            DWORD pID = shelves[i];
            if (valability[pID] == 0xFFFFFFFF || prices[pID] == 0xFFFFFFFF) {
                sprintf(msgBuffer, "Data %s EROARE: Raftul %d are produs %d dar valabilitate/pret invalid.", dateStr, i, pID);
                LogManagementError(msgBuffer);
            }
            else {
                if (pID < SHELVES_SIZE) presentOnShelves[pID] = true;
            }
        }
    }

    // Check 2: Valid Product -> Shelf
    for (int id = 0; id < SHELVES_SIZE; id++) {
        if (valability[id] != 0xFFFFFFFF || prices[id] != 0xFFFFFFFF) {
            if (!presentOnShelves[id]) {
                sprintf(msgBuffer, "Data %s EROARE: Produsul %d exista in memorie (val: %d) dar nu este pe niciun raft!", dateStr, id, valability[id]);
                LogManagementError(msgBuffer);
            }
        }
    }
    free(presentOnShelves);
}

int main() {
    setbuf(stdout, NULL);
    SetConsoleTitleA("Management Process");
    printf("[MANAGEMENT] Initializare...\n");

    SetupFileSystem();
    if (!SetupMemoryAndSync()) {
        printf("Eroare la mapare memorie/creare sync!\n");
        return 1;
    }

    STARTUPINFOW si[3] = { 0 }; PROCESS_INFORMATION pi[3] = { 0 };
    for (int i = 0; i < 3; i++) si[i].cb = sizeof(STARTUPINFOW);

    // Lansare procese separat cu verificare eroare
    wchar_t cmd1[] = L"deposit.exe";
    if (!CreateProcessW(NULL, cmd1, NULL, NULL, FALSE, 0, NULL, NULL, &si[0], &pi[0])) {
        printf("[FATAL] Nu pot lansa deposit.exe (Error %d). Asigura-te ca exista!\n", GetLastError());
        return 1;
    }
    else printf("[DEBUG] deposit.exe lansat (PID: %d)\n", pi[0].dwProcessId);

    wchar_t cmd2[] = L"sell.exe";
    if (!CreateProcessW(NULL, cmd2, NULL, NULL, FALSE, 0, NULL, NULL, &si[1], &pi[1])) {
        printf("[FATAL] Nu pot lansa sell.exe (Error %d).\n", GetLastError());
        TerminateProcess(pi[0].hProcess, 0);
        return 1;
    }
    else printf("[DEBUG] sell.exe lansat (PID: %d)\n", pi[1].dwProcessId);

    wchar_t cmd3[] = L"donate.exe";
    if (!CreateProcessW(NULL, cmd3, NULL, NULL, FALSE, 0, NULL, NULL, &si[2], &pi[2])) {
        printf("[FATAL] Nu pot lansa donate.exe (Error %d).\n", GetLastError());
        TerminateProcess(pi[0].hProcess, 0); TerminateProcess(pi[1].hProcess, 0);
        return 1;
    }
    else printf("[DEBUG] donate.exe lansat (PID: %d)\n", pi[2].dwProcessId);


    std::vector<std::string> allDates = GetSimulationDates(".\\deposit", ".\\sold");
    printf("[MANAGEMENT] S-au gasit %llu zile de procesat.\n", allDates.size());

    // Loop zile
    for (int i = 0; i < (int)allDates.size(); i++) {
        printf("--- Procesare Ziua: %s ---\n", allDates[i].c_str());

        SetEvent(hEvtStartDay);

        // Asteapta Donate sa termine (Donate e ultimul in lant)
        DWORD res = WaitForSingleObject(hEvtDonDone, 60000); // 60s Timeout

        if (res == WAIT_TIMEOUT) {
            printf("[MANAGEMENT] TIMEOUT la data %s! Se opreste fortat.\n", allDates[i].c_str());
            LogManagementError("TIMEOUT: Procese blocate mai mult de 60 secunde.");
            break;
        }

        ValidateInvariants(allDates[i].c_str());
    }

    printf("[MANAGEMENT] Simulare completa. Inchidere procese...\n");
    for (int i = 0; i < 3; i++) {
        TerminateProcess(pi[i].hProcess, 0);
        CloseHandle(pi[i].hProcess);
        CloseHandle(pi[i].hThread);
    }

    // CLEANUP
    UnmapViewOfFile(shelves); UnmapViewOfFile(valability); UnmapViewOfFile(prices);
    CloseHandle(hMapShelves); CloseHandle(hMapValability); CloseHandle(hMapPrices);
    CloseHandle(hEvtStartDay); CloseHandle(hEvtDonDone);
    CloseHandle(hSemLogs);
    CloseHandle(hMutexShelves); CloseHandle(hMutexErrors); CloseHandle(hMutexSold); CloseHandle(hMutexDonations);

    // Final Report
    std::string errPath = GetPath("Reports\\Summary\\errors.txt");
    HANDLE hErr = CreateFileA(errPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hErr != INVALID_HANDLE_VALUE) {
        DWORD s = GetFileSize(hErr, NULL);
        CloseHandle(hErr);
        if (s > 0) {
            printf("\n=== ERORI DETECTATE (vezi errors.txt) ===\n");
            system(("type \"" + errPath + "\"").c_str());
        }
        else goto show_ok;
    }
    else {
    show_ok:
        printf("\n=== SIMULARE REUSITA ===\n");
        char sBuf[64] = { 0 }, dBuf[64] = { 0 };
        ReadAllTextWinAPI(GetPath("Reports\\Summary\\sold.txt").c_str(), sBuf, 64);
        ReadAllTextWinAPI(GetPath("Reports\\Summary\\donations.txt").c_str(), dBuf, 64);
        printf("Total Vandut: %s\nTotal Donat: %s\n", sBuf, dBuf);
    }

    return 0;
}