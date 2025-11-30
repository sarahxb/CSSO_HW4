#include "Common.h"
#include <set>

// Ensure every deposit day has a corresponding sold file (placeholder if missing)
static void EnsureSoldPlaceholders(const std::string& depositDir, const std::string& soldDir) {
    std::vector<std::string> depositFiles = GetSortedFiles(depositDir);
    for (const auto& name : depositFiles) {
        std::string soldPath = soldDir + "\\" + name;
        // Use Windows API to check existence and create placeholder if missing
        if (GetFileAttributesA(soldPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            HANDLE h = CreateFileA(soldPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            std::cerr << "Management: Created placeholder " << soldPath << std::endl;
        }
    }
}

bool ValidateDay(DWORD* shelves, DWORD* valability, DWORD* prices,
    const std::set<DWORD>& soldToday, const std::set<DWORD>& donatedToday,
    HANDLE hErrorsMutex, int dayNumber) {
    bool valid = true;

    // Verificare 1: Niciun raft nu pointează către un produs invalid
    for (int i = 0; i < MARKET_SIZE; i++) {
        if (shelves[i] != INVALID_VALUE) {
            DWORD productId = shelves[i];
            // Bounds check before indexing
            if (productId >= MARKET_SIZE ||
                valability[productId] == INVALID_VALUE ||
                prices[productId] == INVALID_VALUE) {
                std::ostringstream oss;
                oss << "EROARE ZI " << dayNumber << ": Raftul " << i << " pointeaza catre produsul " << productId
                    << " care are date invalide (valability="
                    << ((productId < MARKET_SIZE) ? std::to_string(valability[productId]) : std::string("OOB"))
                    << ", price="
                    << ((productId < MARKET_SIZE) ? std::to_string(prices[productId]) : std::string("OOB"))
                    << ")";
                AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
                valid = false;
            }
        }
    }

    // Verificare 2: Produse setate dar nu pe rafturi (dacă nu au fost vândute/donate)
    for (int i = 0; i < MARKET_SIZE; i++) {
        // treat product as "set" only if both valability and price are set (not INVALID)
        if (valability[i] != INVALID_VALUE && prices[i] != INVALID_VALUE /* include valability==0 handled by donate */) {
            // Produsul este setat, verificăm dacă e pe vreun raft
            bool foundOnShelf = false;
            for (int j = 0; j < MARKET_SIZE; j++) {
                if (shelves[j] == (DWORD)i) {
                    foundOnShelf = true;
                    break;
                }
            }

            if (!foundOnShelf && soldToday.find(i) == soldToday.end() &&
                donatedToday.find(i) == donatedToday.end()) {
                std::ostringstream oss;
                oss << "EROARE ZI " << dayNumber << ": Produsul " << i << " este setat in valability/prices "
                    << "dar nu se afla pe niciun raft si nu a fost vandut/donat astazi";
                AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
                valid = false;
            }
        }
    }

    return valid;
}

int main(int argc, char* argv[]) {
    std::cout << "Management: Pornire..." << std::endl;

    // Creare directoare
    CreateDirectoryRecursive(REPORTS_DIR);
    CreateDirectoryRecursive(SUMMARY_DIR);

    // Creare fișiere mapate
    HANDLE hMapShelves = CreateFileMappingW(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
        MARKET_SIZE * sizeof(DWORD), SHELVES_MAP_NAME);
    DWORD errShelves = GetLastError();
    bool createdShelves = (hMapShelves != NULL && errShelves != ERROR_ALREADY_EXISTS);

    HANDLE hMapValability = CreateFileMappingW(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
        MARKET_SIZE * sizeof(DWORD), VALABILITY_MAP_NAME);
    DWORD errValability = GetLastError();
    bool createdValability = (hMapValability != NULL && errValability != ERROR_ALREADY_EXISTS);

    HANDLE hMapPrices = CreateFileMappingW(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
        MARKET_SIZE * sizeof(DWORD), PRICES_MAP_NAME);
    DWORD errPrices = GetLastError();
    bool createdPrices = (hMapPrices != NULL && errPrices != ERROR_ALREADY_EXISTS);

    if (!hMapShelves || !hMapValability || !hMapPrices) {
        std::cerr << "Eroare la crearea fisierelor mapate!" << std::endl;
        return 1;
    }

    DWORD* shelves = (DWORD*)MapViewOfFile(hMapShelves, FILE_MAP_ALL_ACCESS, 0, 0, MARKET_SIZE * sizeof(DWORD));
    DWORD* valability = (DWORD*)MapViewOfFile(hMapValability, FILE_MAP_ALL_ACCESS, 0, 0, MARKET_SIZE * sizeof(DWORD));
    DWORD* prices = (DWORD*)MapViewOfFile(hMapPrices, FILE_MAP_ALL_ACCESS, 0, 0, MARKET_SIZE * sizeof(DWORD));

    if (!shelves || !valability || !prices) {
        std::cerr << "Eroare la maparea vizuală a fișierelor mapate!" << std::endl;
        if (shelves) UnmapViewOfFile(shelves);
        if (valability) UnmapViewOfFile(valability);
        if (prices) UnmapViewOfFile(prices);
        CloseHandle(hMapShelves);
        CloseHandle(hMapValability);
        CloseHandle(hMapPrices);
        return 1;
    }

    // Initialize buffers only if mapping was created now (support reattach)
    if (createdShelves) {
        for (int i = 0; i < MARKET_SIZE; i++) shelves[i] = INVALID_VALUE;
    }
    if (createdValability) {
        for (int i = 0; i < MARKET_SIZE; i++) valability[i] = INVALID_VALUE;
    }
    if (createdPrices) {
        for (int i = 0; i < MARKET_SIZE; i++) prices[i] = INVALID_VALUE;
    }

    // Create sold.txt and donations.txt only if they don't exist (preserve totals on reattach)
    if (GetFileAttributesA(SOLD_FILE) == INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileA(SOLD_FILE, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            const char* zero = "0";
            DWORD written = 0;
            WriteFile(h, zero, (DWORD)strlen(zero), &written, NULL);
            CloseHandle(h);
        }
    }
    if (GetFileAttributesA(DONATIONS_FILE) == INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileA(DONATIONS_FILE, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            const char* zero = "0";
            DWORD written = 0;
            WriteFile(h, zero, (DWORD)strlen(zero), &written, NULL);
            CloseHandle(h);
        }
    }

    // Ștergere logs.txt și errors.txt dacă există (WinAPI)
    DeleteFileA(LOGS_FILE);
    DeleteFileA(ERRORS_FILE);

    // Creare mutex-uri
    HANDLE hLogsMutex = CreateMutexW(NULL, FALSE, LOGS_MUTEX_NAME);
    HANDLE hErrorsMutex = CreateMutexW(NULL, FALSE, ERRORS_MUTEX_NAME);
    HANDLE hSoldMutex = CreateMutexW(NULL, FALSE, SOLD_MUTEX_NAME);
    HANDLE hDonationsMutex = CreateMutexW(NULL, FALSE, DONATIONS_MUTEX_NAME);
    HANDLE hShelvesMutex = CreateMutexW(NULL, FALSE, SHELVES_MUTEX_NAME);

    // Creare events pentru fazele (manual-reset pentru a debloca toate procesele)
    HANDLE hDepositDone = CreateEventW(NULL, TRUE, FALSE, BARRIER_EVENT_DEPOSIT);
    HANDLE hSellDone = CreateEventW(NULL, TRUE, FALSE, BARRIER_EVENT_SELL);
    HANDLE hDonateDone = CreateEventW(NULL, TRUE, FALSE, BARRIER_EVENT_DONATE);
    // ManagerReady: signaled to allow first day to start
    HANDLE hManagerReady = CreateEventW(NULL, TRUE, TRUE, BARRIER_EVENT_MANAGER);

    // Creare semafor pentru evitare racing ahead
    HANDLE hSemaphore = CreateSemaphoreW(NULL, 3, 3, BARRIER_SEMAPHORE);

    // --- Barrier primitives children expect: create and initialize ---
    HANDLE hBarrierMutex = CreateMutexW(NULL, FALSE, BARRIER_MUTEX_NAME);
    HANDLE hBarrierRelease = CreateEventW(NULL, TRUE, FALSE, BARRIER_EVENT_RELEASE);
    HANDLE hMapBarrierCounter = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(LONG), BARRIER_COUNTER_MAP_NAME);
    DWORD errBarrierCounter = GetLastError();
    bool createdBarrierCounter = (hMapBarrierCounter != NULL && errBarrierCounter != ERROR_ALREADY_EXISTS);
    LONG* pBarrierCounter = NULL;
    if (hMapBarrierCounter) {
        pBarrierCounter = (LONG*)MapViewOfFile(hMapBarrierCounter, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LONG));
        if (pBarrierCounter) {
            if (createdBarrierCounter) {
                InterlockedExchange(pBarrierCounter, 0);
            }
        }
    }
    // --- end barrier creation ---------------------------------------

    // Determinare numar de zile si validare directoare (do this before starting children)
    std::string depositDir = "C:\\Users\\sarah\\tema4\\deposit";
    std::string soldDir = "C:\\Users\\sarah\\tema4\\sold";

    WIN32_FIND_DATAA findData;
    HANDLE hFindDeposit = FindFirstFileA((depositDir + "\\*").c_str(), &findData);
    HANDLE hFindSold = FindFirstFileA((soldDir + "\\*").c_str(), &findData);

    if (hFindDeposit == INVALID_HANDLE_VALUE) {
        std::cerr << "EROARE: Directorul 'deposit' nu exista sau nu poate fi accesat!" << std::endl;
        if (hFindSold != INVALID_HANDLE_VALUE) FindClose(hFindSold);
        return 1;
    }
    FindClose(hFindDeposit);

    if (hFindSold == INVALID_HANDLE_VALUE) {
        std::cerr << "EROARE: Directorul 'sold' nu exista sau nu poate fi accesat!" << std::endl;
        return 1;
    }
    FindClose(hFindSold);

    std::vector<std::string> files = GetSortedFiles(depositDir);
    int numDays = files.size();

    if (numDays == 0) {
        std::cerr << "EROARE: Nu s-au gasit fisiere in directorul deposit/" << std::endl;
        return 1;
    }

    // Make sold/ match deposit/ by creating placeholders for missing sold files
    EnsureSoldPlaceholders(depositDir, soldDir);

    std::cout << "Management: Lansare procese..." << std::endl;

    // Lansare procese (after directories validated and placeholders created)
    STARTUPINFOA si1 = { sizeof(si1) }, si2 = { sizeof(si2) }, si3 = { sizeof(si3) };
    PROCESS_INFORMATION pi1, pi2, pi3;

    // Căi complete către executabile (în același director cu management.exe)
    std::string exePath = "C:\\Users\\sarah\\source\\repos\\CSSO_HW4\\x64\\Debug\\";
    std::string depositExe = exePath + "Deposit.exe";
    std::string sellExe = exePath + "Sell.exe";
    std::string donateExe = exePath + "Donate.exe";

    if (!CreateProcessA(depositExe.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si1, &pi1)) {
        std::cerr << "Eroare la lansarea deposit.exe: " << GetLastError() << std::endl;
        return 1;
    }

    if (!CreateProcessA(sellExe.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si2, &pi2)) {
        std::cerr << "Eroare la lansarea sell.exe: " << GetLastError() << std::endl;
        TerminateProcess(pi1.hProcess, 1);
        return 1;
    }

    if (!CreateProcessA(donateExe.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si3, &pi3)) {
        std::cerr << "Eroare la lansarea donate.exe: " << GetLastError() << std::endl;
        TerminateProcess(pi1.hProcess, 1);
        TerminateProcess(pi2.hProcess, 1);
        return 1;
    }

    HANDLE processes[] = { pi1.hProcess, pi2.hProcess, pi3.hProcess };

    std::cout << "Management: Procesare " << numDays << " zile..." << std::endl;

    // Procesare zi cu zi
    for (int day = 0; day < numDays; day++) {
        std::cout << "Management: Zi " << (day + 1) << "/" << numDays << std::endl;

        // Pornire zi: semnalare manager ready
        SetEvent(hManagerReady);

        // Așteptare ca toate procesele să termine ziua (s-au semnalat Done)
        HANDLE events[] = { hDepositDone, hSellDone, hDonateDone };
        DWORD waitResult = WaitForMultipleObjects(3, events, TRUE, DAY_BARRIER_TIMEOUT);

        if (waitResult == WAIT_TIMEOUT) {
            std::ostringstream oss;
            oss << "TIMEOUT: Procesele nu au terminat ziua " << (day + 1) << " in timp util";

            // Debugging: verifică care event nu s-a semnalat
            DWORD depositState = WaitForSingleObject(hDepositDone, 0);
            DWORD sellState = WaitForSingleObject(hSellDone, 0);
            DWORD donateState = WaitForSingleObject(hDonateDone, 0);

            oss << " (Deposit: " << (depositState == WAIT_OBJECT_0 ? "OK" : "BLOCAT")
                << ", Sell: " << (sellState == WAIT_OBJECT_0 ? "OK" : "BLOCAT")
                << ", Donate: " << (donateState == WAIT_OBJECT_0 ? "OK" : "BLOCAT") << ")";

            AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
            std::cerr << oss.str() << std::endl;

            TerminateProcess(pi1.hProcess, 1);
            TerminateProcess(pi2.hProcess, 1);
            TerminateProcess(pi3.hProcess, 1);
            break;
        }

        // IMPORTANT: wait for the barrier release event so all children passed Arrive()
        if (hBarrierRelease) {
            DWORD barrierResult = WaitForSingleObject(hBarrierRelease, DAY_BARRIER_TIMEOUT);
            if (barrierResult != WAIT_OBJECT_0) {
                std::ostringstream oss;
                oss << "TIMEOUT: Procesele nu au trecut de bariera pentru ziua " << (day + 1);
                AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
                std::cerr << oss.str() << std::endl;

                TerminateProcess(pi1.hProcess, 1);
                TerminateProcess(pi2.hProcess, 1);
                TerminateProcess(pi3.hProcess, 1);
                break;
            }
        }

        // Validare zi
        std::set<DWORD> soldToday, donatedToday;

        // Read per-day sold/donated reports (if present) into sets using WinAPI helpers
        std::string dayFile = files[day]; // deposit filename yyyy.mm.dd
        std::string soldReport = std::string(SUMMARY_DIR) + "\\sold_" + dayFile + ".txt";
        std::string donatedReport = std::string(SUMMARY_DIR) + "\\donated_" + dayFile + ".txt";

        // Read sold
        WaitForSingleObject(hSoldMutex, INFINITE);
        {
            std::vector<std::string> lines = ReadLinesFromFile_Win(soldReport);
            for (const auto& line : lines) {
                if (line.empty()) continue;
                try {
                    DWORD id = (DWORD)std::stoul(line);
                    soldToday.insert(id);
                }
                catch (...) {}
            }
        }
        ReleaseMutex(hSoldMutex);

        // Read donated
        WaitForSingleObject(hDonationsMutex, INFINITE);
        {
            std::vector<std::string> lines = ReadLinesFromFile_Win(donatedReport);
            for (const auto& line : lines) {
                if (line.empty()) continue;
                try {
                    DWORD id = (DWORD)std::stoul(line);
                    donatedToday.insert(id);
                }
                catch (...) {}
            }
        }
        ReleaseMutex(hDonationsMutex);

        // Acquire shelves mutex while validating to avoid races with children
        WaitForSingleObject(hShelvesMutex, INFINITE);
        ValidateDay(shelves, valability, prices, soldToday, donatedToday, hErrorsMutex, day + 1);
        ReleaseMutex(hShelvesMutex);

        // After all day processing, reset barrier for next iteration
        if (hBarrierRelease) ResetEvent(hBarrierRelease);
        if (pBarrierCounter) InterlockedExchange(pBarrierCounter, 0);

        // Reset events pentru următoarea zi
        ResetEvent(hDepositDone);
        ResetEvent(hSellDone);
        ResetEvent(hDonateDone);
        ResetEvent(hManagerReady);

        Sleep(50); // Pauză mică pentru sincronizare
    }

    // Așteptare finală cu timeout (60s per spec)
    std::cout << "Management: Asteptare terminare procese..." << std::endl;
    DWORD waitResultProcesses = WaitForMultipleObjects(3, processes, TRUE, DAY_BARRIER_TIMEOUT);

    if (waitResultProcesses == WAIT_TIMEOUT) {
        std::cerr << "Management: Timeout la terminare, forțare oprire..." << std::endl;
        TerminateProcess(pi1.hProcess, 1);
        TerminateProcess(pi2.hProcess, 1);
        TerminateProcess(pi3.hProcess, 1);
    }

    std::cout << "Management: Afisare rezultate..." << std::endl;

    // Afișare rezultate: check existence of errors file using Windows API
    if (GetFileAttributesA(ERRORS_FILE) != INVALID_FILE_ATTRIBUTES) {
        std::cout << "\n=== S-au detectat ERORI ===" << std::endl;
        std::vector<std::string> lines = ReadLinesFromFile_Win(ERRORS_FILE);
        for (const auto& line : lines) std::cout << line << std::endl;
    }
    else {
        double soldValue = ReadDoubleFromFile(SOLD_FILE, hSoldMutex);
        double donationsValue = ReadDoubleFromFile(DONATIONS_FILE, hDonationsMutex);

        std::cout << "\n=== REZULTATE FINALE ===" << std::endl;
        std::cout << "Sold net: " << soldValue << " lei" << std::endl;
        std::cout << "Donatii: " << donationsValue << " lei" << std::endl;
    }

    // Cleanup
    CloseHandle(pi1.hProcess);
    CloseHandle(pi1.hThread);
    CloseHandle(pi2.hProcess);
    CloseHandle(pi2.hThread);
    CloseHandle(pi3.hProcess);
    CloseHandle(pi3.hThread);

    UnmapViewOfFile(shelves);
    UnmapViewOfFile(valability);
    UnmapViewOfFile(prices);

    CloseHandle(hMapShelves);
    CloseHandle(hMapValability);
    CloseHandle(hMapPrices);

    CloseHandle(hLogsMutex);
    CloseHandle(hErrorsMutex);
    CloseHandle(hSoldMutex);
    CloseHandle(hDonationsMutex);
    CloseHandle(hShelvesMutex);

    CloseHandle(hDepositDone);
    CloseHandle(hSellDone);
    CloseHandle(hDonateDone);
    CloseHandle(hManagerReady);
    CloseHandle(hSemaphore);

    // Cleanup barrier objects
    if (pBarrierCounter) UnmapViewOfFile(pBarrierCounter);
    if (hMapBarrierCounter) CloseHandle(hMapBarrierCounter);
    if (hBarrierRelease) CloseHandle(hBarrierRelease);
    if (hBarrierMutex) CloseHandle(hBarrierMutex);

    std::cout << "\nManagement: Finalizat!" << std::endl;

    return 0;
}