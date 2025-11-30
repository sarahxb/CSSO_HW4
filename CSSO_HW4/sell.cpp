#include "Common.h"

int main(int argc, char* argv[]) {
    // Deschidere fișiere mapate
    MappedFile mfShelves = OpenMappedFile(SHELVES_MAP_NAME);
    MappedFile mfValability = OpenMappedFile(VALABILITY_MAP_NAME);
    MappedFile mfPrices = OpenMappedFile(PRICES_MAP_NAME);

    if (!mfShelves.pData || !mfValability.pData || !mfPrices.pData) {
        std::cerr << "sell.exe: Eroare la deschiderea fisierelor mapate!" << std::endl;
        return 1;
    }

    DWORD* shelves = mfShelves.pData;
    DWORD* valability = mfValability.pData;
    DWORD* prices = mfPrices.pData;

    // Deschidere mutex-uri (request full access)
    HANDLE hLogsMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, LOGS_MUTEX_NAME);
    HANDLE hErrorsMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, ERRORS_MUTEX_NAME);
    HANDLE hSoldMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, SOLD_MUTEX_NAME);
    HANDLE hShelvesMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, SHELVES_MUTEX_NAME);

    if (!hLogsMutex || !hErrorsMutex || !hSoldMutex || !hShelvesMutex) {
        std::cerr << "sell.exe: Eroare la deschiderea mutex-urilor!" << std::endl;
        return 1;
    }

    // Inițializare barieră
    DayBarrier barrier;

    // Obținere lista fișiere sortate din directorul sold
    std::string soldDir = "C:\\Users\\sarah\\tema4\\sold";
    std::vector<std::string> files = GetSortedFiles(soldDir);

    // Obținem numărul de zile din deposit pentru sincronizare
    std::string depositDir = "C:\\Users\\sarah\\tema4\\deposit";
    std::vector<std::string> depositFiles = GetSortedFiles(depositDir);

    if (files.empty()) {
        std::ostringstream oss;
        oss << "sell.exe: Nu s-au gasit fisiere in directorul sold/";
        AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
        CloseMappedFile(mfShelves);
        CloseMappedFile(mfValability);
        CloseMappedFile(mfPrices);
        CloseHandle(hLogsMutex);
        CloseHandle(hErrorsMutex);
        CloseHandle(hSoldMutex);
        CloseHandle(hShelvesMutex);
        return 1;
    }

    int numDays = depositFiles.size();
    std::cout << "sell.exe: Proceseaza " << numDays << " zile (deposit: "
        << depositFiles.size() << ", sold: " << files.size() << ")" << std::endl;

    if (files.size() != depositFiles.size()) {
        std::ostringstream oss;
        oss << "sell.exe: ATENTIE - sold are " << files.size()
            << " zile, dar deposit are " << depositFiles.size() << " zile!";
        AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
    }

    // Procesare zi cu zi
    for (int dayIdx = 0; dayIdx < numDays; dayIdx++) {
        if (!barrier.AcquireDaySlot()) {
            std::ostringstream oss;
            oss << "sell.exe: Timeout la achiziționare slot semafor pentru ziua " << (dayIdx + 1);
            AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
            break;
        }

        if (!barrier.WaitForSellPhase()) {
            std::ostringstream oss;
            oss << "sell.exe: Timeout la asteptare DepositDone pentru ziua " << (dayIdx + 1);
            AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
            barrier.ReleaseDaySlot();
            break;
        }

        std::vector<DWORD> soldIds;

        std::string filename;
        if (dayIdx < (int)files.size()) filename = files[dayIdx];

        if (dayIdx >= (int)files.size() || filename.empty()) {
            std::cout << "sell.exe: Zi " << (dayIdx + 1) << " - nu exista fisier sold" << std::endl;
        }
        else {
            const std::string& filepath = soldDir + "\\" + filename;
            std::vector<std::string> lines = ReadLinesFromFile_Win(filepath);

            if (lines.empty()) {
                std::ostringstream oss;
                oss << "sell.exe: Nu pot deschide fisierul " << filepath;
                AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
            }
            else {
                std::cout << "sell.exe: Procesare " << filename << std::endl;

                int lineNum = 0;
                for (const auto& line : lines) {
                    lineNum++;
                    if (line.empty()) continue;

                    DWORD shelve_id;
                    try {
                        shelve_id = std::stoul(line);
                    }
                    catch (...) {
                        std::ostringstream oss;
                        oss << "sell.exe: Eroare parsare linia " << lineNum << " din " << filename << ": " << line;
                        AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
                        continue;
                    }

                    if (shelve_id >= MARKET_SIZE) {
                        std::ostringstream oss;
                        oss << "sell.exe: shelve_id invalid " << shelve_id << " (>= " << MARKET_SIZE << ")";
                        AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
                        continue;
                    }

                    WaitForSingleObject(hShelvesMutex, INFINITE);

                    if (shelves[shelve_id] == INVALID_VALUE) {
                        std::ostringstream oss;
                        oss << "S-a incercat vanzarea unui produs de pe un raft "
                            << shelve_id << " ce nu contine produs";
                        ReleaseMutex(hShelvesMutex);
                        AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
                        continue;
                    }

                    DWORD id_produs = shelves[shelve_id];

                    if (id_produs >= MARKET_SIZE) {
                        std::ostringstream oss;
                        oss << "sell.exe: id_produs invalid " << id_produs << " (>= " << MARKET_SIZE << ")";
                        ReleaseMutex(hShelvesMutex);
                        AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
                        continue;
                    }

                    DWORD val = valability[id_produs];
                    DWORD price = prices[id_produs];

                    if (val == 0) {
                        std::ostringstream oss;
                        oss << "S-a incercat vanzarea unui produs expirat "
                            << id_produs << " de pe raftul " << shelve_id;
                        ReleaseMutex(hShelvesMutex);
                        AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
                        continue;
                    }

                    if (val > 0 && val != INVALID_VALUE) {
                        double net = (val <= 2) ? (price * 0.75) : (double)price;

                        UpdateSoldFile(net, hSoldMutex);

                        valability[id_produs] = INVALID_VALUE;
                        prices[id_produs] = INVALID_VALUE;
                        shelves[shelve_id] = INVALID_VALUE;

                        ReleaseMutex(hShelvesMutex);

                        std::ostringstream oss;
                        oss << "S-a vandut produsul " << id_produs
                            << " de pe raftul " << shelve_id
                            << " cu " << val << " zile ramase; pret intreg "
                            << price << ", net " << net << ".";
                        AppendToFile(LOGS_FILE, oss.str(), hLogsMutex);

                        soldIds.push_back(id_produs);
                    }
                    else {
                        ReleaseMutex(hShelvesMutex);
                    }
                }
            }
        }

        // Write per-day sold ids into Summary directory BEFORE signalling SellDone
        {
            std::string outPath = std::string(SUMMARY_DIR) + "\\sold_" + ((filename.empty()) ? ("day_" + std::to_string(dayIdx + 1)) : filename) + ".txt";
            std::vector<std::string> lines;
            for (DWORD id : soldIds) lines.push_back(std::to_string(id));
            WaitForSingleObject(hSoldMutex, INFINITE);
            if (!WriteLinesToFile_Win(outPath, lines)) {
                std::ostringstream oss;
                oss << "sell.exe: Nu pot scrie fisierul de raport " << outPath;
                AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
            }
            ReleaseMutex(hSoldMutex);
        }

        std::cout << "[SELL] Day " << (dayIdx + 1) << ": signalling SellDone (ArriveSell)..." << std::endl;
        barrier.ArriveSell();
        std::cout << "[SELL] Day " << (dayIdx + 1) << ": ArriveSell signalled." << std::endl;

        barrier.Arrive();

        barrier.ReleaseDaySlot();
    } // for (dayIdx)

    // Cleanup (DUPĂ bucla for, nu înăuntru!)
    CloseMappedFile(mfShelves);
    CloseMappedFile(mfValability);
    CloseMappedFile(mfPrices);

    CloseHandle(hLogsMutex);
    CloseHandle(hErrorsMutex);
    CloseHandle(hSoldMutex);
    CloseHandle(hShelvesMutex);

    return 0;
}