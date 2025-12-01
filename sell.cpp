#include "common.h"

HANDLE hMutexShelves, hMutexErrors, hMutexSold;
HANDLE hSemLogs;
HANDLE hEvtDepDone, hEvtSellDone;
DWORD* shelves, * valability, * prices;

const char* DIR_SOLD_INPUT = ".\\sold";

void Log(const char* msg) {
    WaitForSingleObject(hSemLogs, INFINITE);
    std::string path = GetPath("Reports\\logs.txt");
    AppendToFile(path.c_str(), msg);
    ReleaseSemaphore(hSemLogs, 1, NULL);
}

void LogError(const char* msg) {
    WaitForSingleObject(hMutexErrors, INFINITE);
    std::string path = GetPath("Reports\\Summary\\errors.txt");
    AppendToFile(path.c_str(), msg);
    ReleaseMutex(hMutexErrors);
}

void UpdateSold(double amount) {
    WaitForSingleObject(hMutexSold, INFINITE);
    std::string path = GetPath("Reports\\Summary\\sold.txt");
    char buf[64];
    if (ReadAllTextWinAPI(path.c_str(), buf, 64)) {
        double current = atof(buf);
        current += amount;
        sprintf(buf, "%.2f", current);
        OverwriteFile(path.c_str(), buf);
    }
    ReleaseMutex(hMutexSold);
}

int main() {
    setbuf(stdout, NULL);
    SetConsoleTitleA("Sell Process");

    // *** FIX: _ALL_ACCESS ***
    hMutexShelves = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, MUTEX_SHELVES);
    hMutexErrors = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, MUTEX_ERRORS);
    hMutexSold = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, MUTEX_SOLD);
    hSemLogs = OpenSemaphoreW(SEMAPHORE_ALL_ACCESS, FALSE, SEM_LOGS);

    hEvtDepDone = OpenEventW(EVENT_ALL_ACCESS, FALSE, EVT_DEP_DONE);
    hEvtSellDone = OpenEventW(EVENT_ALL_ACCESS, FALSE, EVT_SELL_DONE);

    HANDLE hM1 = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAP_SHELVES);
    HANDLE hM2 = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAP_VALABILITY);
    HANDLE hM3 = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAP_PRICES);

    if (!hM1 || !hEvtDepDone || !hMutexShelves) return 1;

    shelves = (DWORD*)MapViewOfFile(hM1, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    valability = (DWORD*)MapViewOfFile(hM2, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    prices = (DWORD*)MapViewOfFile(hM3, FILE_MAP_ALL_ACCESS, 0, 0, 0);

    std::vector<std::string> allDates = GetSimulationDates(".\\deposit", DIR_SOLD_INPUT);
    char* fileBuffer = (char*)malloc(65536);

    for (int i = 0; i < (int)allDates.size(); i++) {
        WaitForSingleObject(hEvtDepDone, INFINITE);

        char path[MAX_PATH];
        sprintf(path, "%s\\%s", DIR_SOLD_INPUT, allDates[i].c_str());

        if (ReadAllTextWinAPI(path, fileBuffer, 65536)) {
            char* nextToken = NULL;
            char* line = strtok_s(fileBuffer, "\r\n", &nextToken);

            while (line) {
                int shelf = atoi(line);
                WaitForSingleObject(hMutexShelves, INFINITE);

                if (shelf < 0 || shelf >= SHELVES_SIZE || shelves[shelf] == 0xFFFFFFFF) {
                    ReleaseMutex(hMutexShelves);
                    char err[256];
                    sprintf(err, "S-a incercat vanzarea unui produs de pe un raft %d ce nu contine produs", shelf);
                    LogError(err);
                }
                else {
                    DWORD id = shelves[shelf];
                    if (valability[id] == 0) {
                        ReleaseMutex(hMutexShelves);
                        char err[256];
                        sprintf(err, "S-a incercat vanzarea unui produs expirat %d de pe raftul %d", id, shelf);
                        LogError(err);
                    }
                    else {
                        double price = (double)prices[id];
                        double net = price;
                        if (valability[id] <= 2) net = price * 0.75;

                        DWORD daysLeft = valability[id];
                        shelves[shelf] = 0xFFFFFFFF;
                        valability[id] = 0xFFFFFFFF;
                        prices[id] = 0xFFFFFFFF;
                        ReleaseMutex(hMutexShelves);

                        UpdateSold(net);
                        char log[256];
                        sprintf(log, "S-a vandut produsul %d de pe raftul %d cu %d zile ramase; pret intreg %.0f, net %.2f.", id, shelf, daysLeft, price, net);
                        Log(log);
                    }
                }
                line = strtok_s(NULL, "\r\n", &nextToken);
            }
        }
        SetEvent(hEvtSellDone);
    }
    free(fileBuffer);
    return 0;
}