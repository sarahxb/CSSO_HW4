#include "common.h"

HANDLE hMutexShelves, hMutexErrors;
HANDLE hSemLogs;
HANDLE hEvtStartDay, hEvtDepDone;
DWORD* shelves, * valability, * prices;

const char* DIR_DEPOSIT_INPUT = ".\\deposit";

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

int main() {
    setbuf(stdout, NULL);
    SetConsoleTitleA("Deposit Process");

    // *** FIX: Folosim _ALL_ACCESS pentru a putea face Release/Set ***
    hMutexShelves = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, MUTEX_SHELVES);
    hMutexErrors = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, MUTEX_ERRORS);
    hSemLogs = OpenSemaphoreW(SEMAPHORE_ALL_ACCESS, FALSE, SEM_LOGS);

    hEvtStartDay = OpenEventW(EVENT_ALL_ACCESS, FALSE, EVT_START_DAY);
    hEvtDepDone = OpenEventW(EVENT_ALL_ACCESS, FALSE, EVT_DEP_DONE);

    HANDLE hM1 = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAP_SHELVES);
    HANDLE hM2 = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAP_VALABILITY);
    HANDLE hM3 = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAP_PRICES);

    if (!hM1 || !hEvtStartDay || !hMutexShelves || !hSemLogs) {
        // E important sa returnam eroare daca nu avem handle-uri
        return 1;
    }

    shelves = (DWORD*)MapViewOfFile(hM1, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    valability = (DWORD*)MapViewOfFile(hM2, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    prices = (DWORD*)MapViewOfFile(hM3, FILE_MAP_ALL_ACCESS, 0, 0, 0);

    std::vector<std::string> allDates = GetSimulationDates(DIR_DEPOSIT_INPUT, ".\\sold");
    char* fileBuffer = (char*)malloc(65536);

    for (int i = 0; i < (int)allDates.size(); i++) {
        WaitForSingleObject(hEvtStartDay, INFINITE);

        char path[MAX_PATH];
        sprintf(path, "%s\\%s", DIR_DEPOSIT_INPUT, allDates[i].c_str());

        if (ReadAllTextWinAPI(path, fileBuffer, 65536)) {
            char* nextToken = NULL;
            char* line = strtok_s(fileBuffer, "\r\n", &nextToken);

            while (line) {
                int id, exp, shelf, price;
                if (sscanf(line, "%d,%d,%d,%d", &id, &exp, &shelf, &price) == 4) {
                    if (shelf >= 0 && shelf < SHELVES_SIZE) {
                        WaitForSingleObject(hMutexShelves, INFINITE);
                        if (shelves[shelf] != 0xFFFFFFFF) {
                            DWORD existingId = shelves[shelf];
                            ReleaseMutex(hMutexShelves);
                            char err[256];
                            sprintf(err, "S-a incercat adaugarea produsului %d pe raftul %d care este deja ocupat de %d", id, shelf, existingId);
                            LogError(err);
                        }
                        else {
                            shelves[shelf] = id;
                            valability[id] = exp;
                            prices[id] = price;
                            ReleaseMutex(hMutexShelves);
                            char log[256];
                            sprintf(log, "Am adaugat pe raftul %d produsul %d cu valabilitate %d zile si pret %d.", shelf, id, exp, price);
                            Log(log);
                        }
                    }
                }
                line = strtok_s(NULL, "\r\n", &nextToken);
            }
        }
        SetEvent(hEvtDepDone);
    }
    free(fileBuffer);
    return 0;
}