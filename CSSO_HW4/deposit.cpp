#include "Common.h"
#include <set>

int main(int argc, char* argv[]) {
    // Deschidere fișiere mapate
    MappedFile mfShelves = OpenMappedFile(SHELVES_MAP_NAME);
    MappedFile mfValability = OpenMappedFile(VALABILITY_MAP_NAME);
    MappedFile mfPrices = OpenMappedFile(PRICES_MAP_NAME);

    if (!mfShelves.pData || !mfValability.pData || !mfPrices.pData) {
        std::cerr << "deposit.exe: Eroare la deschiderea fisierelor mapate!" << std::endl;
        return 1;
    }

    DWORD* shelves = mfShelves.pData;
    DWORD* valability = mfValability.pData;
    DWORD* prices = mfPrices.pData;

    // Deschidere mutex-uri (request full access to named mutexes)
    HANDLE hLogsMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, LOGS_MUTEX_NAME);
    HANDLE hErrorsMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, ERRORS_MUTEX_NAME);
    HANDLE hShelvesMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, SHELVES_MUTEX_NAME);

    if (!hLogsMutex || !hErrorsMutex || !hShelvesMutex) {
        std::cerr << "deposit.exe: Eroare la deschiderea mutex-urilor!" << std::endl;
        return 1;
    }

    // Inițializare barieră
    DayBarrier barrier;

    // Obținere lista fișiere sortate din directorul deposit
    std::string depositDir = "C:\\Users\\sarah\\tema4\\deposit";
    std::vector<std::string> files = GetSortedFiles(depositDir);

    if (files.empty()) {
        std::ostringstream oss;
        oss << "deposit.exe: Nu s-au gasit fisiere in directorul deposit/";
        AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
        CloseMappedFile(mfShelves);
        CloseMappedFile(mfValability);
        CloseMappedFile(mfPrices);
        CloseHandle(hLogsMutex);
        CloseHandle(hErrorsMutex);
        CloseHandle(hShelvesMutex);
        return 1;
    }

    std::cout << "deposit.exe: Proceseaza " << files.size() << " zile" << std::endl;

    for (size_t dayIdx = 0; dayIdx < files.size(); dayIdx++) {
        const auto& filename = files[dayIdx];
        std::cout << "deposit.exe: Zi " << (dayIdx + 1) << "/" << files.size() << " - " << filename << std::endl;

        // Achiziționare slot din semafor (previne racing ahead)
        if (!barrier.AcquireDaySlot()) {
            std::ostringstream oss;
            oss << "deposit.exe: Timeout la achiziționare slot semafor pentru " << filename;
            AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
            break;
        }

        // Așteptare pentru faza de deposit
        if (!barrier.WaitForDepositPhase()) {
            std::ostringstream oss;
            oss << "deposit.exe: Timeout la asteptare ManagerReady pentru ziua " << (dayIdx + 1);
            AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
            barrier.ReleaseDaySlot();
            break;
        }

        std::string filepath = depositDir + "\\" + filename;
        // Read file lines with WinAPI helper
        std::vector<std::string> lines = ReadLinesFromFile_Win(filepath);

        if (lines.empty()) {
            std::ostringstream oss;
            oss << "deposit.exe: Nu pot deschide sau fisier gol " << filepath;
            AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
            // participate in barrier anyway
        }
        else {
            int lineNum = 0;
            for (const auto& line : lines) {
                lineNum++;
                if (line.empty()) continue;

                // Parse: id_produs, expires_in_days, shelve_id, product_price
                std::istringstream iss(line);
                DWORD id_produs = 0, expires_in = 0, shelve_id = 0, product_price = 0;
                char comma;

                if (!(iss >> id_produs >> comma >> expires_in >> comma >> shelve_id >> comma >> product_price)) {
                    std::ostringstream oss;
                    oss << "deposit.exe: Eroare parsare linia " << lineNum << " din " << filename << ": " << line;
                    AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
                    continue;
                }

                if (id_produs >= MARKET_SIZE) {
                    std::ostringstream oss;
                    oss << "deposit.exe: id_produs invalid " << id_produs << " (>= " << MARKET_SIZE << ")";
                    AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
                    continue;
                }

                if (shelve_id >= MARKET_SIZE) {
                    std::ostringstream oss;
                    oss << "deposit.exe: shelve_id invalid " << shelve_id << " (>= " << MARKET_SIZE << ")";
                    AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
                    continue;
                }

                WaitForSingleObject(hShelvesMutex, INFINITE);

                if (shelves[shelve_id] != INVALID_VALUE) {
                    std::ostringstream oss;
                    oss << "S-a incercat adaugarea produsului " << id_produs
                        << " pe raftul " << shelve_id
                        << " care este deja ocupat de " << shelves[shelve_id];
                    ReleaseMutex(hShelvesMutex);
                    AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
                    continue;
                }

                shelves[shelve_id] = id_produs;
                valability[id_produs] = expires_in;
                prices[id_produs] = product_price;

                ReleaseMutex(hShelvesMutex);

                std::ostringstream oss;
                oss << "Am adaugat pe raftul " << shelve_id
                    << " produsul " << id_produs
                    << " cu valabilitate " << expires_in
                    << " zile si pret " << product_price << ".";
                AppendToFile(LOGS_FILE, oss.str(), hLogsMutex);
            }
        }

        std::cout << "deposit.exe: Finalizat zi " << (dayIdx + 1) << std::endl;

        // Semnalare că deposit a terminat ziua (MEREU!)
        barrier.ArriveDeposit();

        // Barieră: așteaptă ca toate procesele să termine ziua înainte de a continua
        barrier.Arrive();

        // Eliberare slot din semafor
        barrier.ReleaseDaySlot();
    }

    // Cleanup
    CloseMappedFile(mfShelves);
    CloseMappedFile(mfValability);
    CloseMappedFile(mfPrices);

    CloseHandle(hLogsMutex);
    CloseHandle(hErrorsMutex);
    CloseHandle(hShelvesMutex);

    return 0;
}