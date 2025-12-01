#include "common.h"

HANDLE hMutexShelves, hMutexDonations;
HANDLE hSemLogs;
HANDLE hEvtSellDone, hEvtDonDone;
DWORD* shelves, * valability, * prices;

void Log(const char* msg) {
    WaitForSingleObject(hSemLogs, INFINITE);
    std::string path = GetPath("Reports\\logs.txt");
    AppendToFile(path.c_str(), msg);
    ReleaseSemaphore(hSemLogs, 1, NULL);
}

void UpdateDonations(double amount) {
    WaitForSingleObject(hMutexDonations, INFINITE);
    std::string path = GetPath("Reports\\Summary\\donations.txt");
    char buf[64];
    if (ReadAllTextWinAPI(path.c_str(), buf, 64)) {
        double current = atof(buf);
        current += amount;
        sprintf(buf, "%.2f", current);
        OverwriteFile(path.c_str(), buf);
    }
    ReleaseMutex(hMutexDonations);
}

int main() {
    setbuf(stdout, NULL);
    SetConsoleTitleA("Donate Process");

    // *** FIX: _ALL_ACCESS ***
    hMutexShelves = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, MUTEX_SHELVES);
    hMutexDonations = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, MUTEX_DONATIONS);
    hSemLogs = OpenSemaphoreW(SEMAPHORE_ALL_ACCESS, FALSE, SEM_LOGS);

    hEvtSellDone = OpenEventW(EVENT_ALL_ACCESS, FALSE, EVT_SELL_DONE);
    hEvtDonDone = OpenEventW(EVENT_ALL_ACCESS, FALSE, EVT_DON_DONE);

    HANDLE hM1 = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAP_SHELVES);
    HANDLE hM2 = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAP_VALABILITY);
    HANDLE hM3 = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAP_PRICES);

    if (!hM1 || !hEvtSellDone || !hMutexShelves) return 1;

    shelves = (DWORD*)MapViewOfFile(hM1, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    valability = (DWORD*)MapViewOfFile(hM2, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    prices = (DWORD*)MapViewOfFile(hM3, FILE_MAP_ALL_ACCESS, 0, 0, 0);

    std::vector<std::string> allDates = GetSimulationDates(".\\deposit", ".\\sold");

    for (int i = 0; i < (int)allDates.size(); i++) {
        WaitForSingleObject(hEvtSellDone, INFINITE);

        WaitForSingleObject(hMutexShelves, INFINITE);
        for (int k = 0; k < SHELVES_SIZE; k++) {
            if (valability[k] != 0xFFFFFFFF) {
                if (valability[k] == 0) {
                    double val = (double)prices[k];
                    for (int r = 0; r < SHELVES_SIZE; r++) {
                        if (shelves[r] == (DWORD)k) { shelves[r] = 0xFFFFFFFF; break; }
                    }
                    valability[k] = 0xFFFFFFFF;
                    prices[k] = 0xFFFFFFFF;

                    ReleaseMutex(hMutexShelves);
                    UpdateDonations(val);
                    char log[128];
                    sprintf(log, "Produsul %d a fost donat", k);
                    Log(log);
                    WaitForSingleObject(hMutexShelves, INFINITE);
                }
                else if (valability[k] > 0) {
                    valability[k]--;
                }
            }
        }
        ReleaseMutex(hMutexShelves);

        SetEvent(hEvtDonDone);
    }
    return 0;
}