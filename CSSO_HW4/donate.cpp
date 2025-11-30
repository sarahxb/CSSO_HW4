#include "Common.h"

int main(int argc, char* argv[]) {
    // Deschidere fișiere mapate
    MappedFile mfShelves = OpenMappedFile(SHELVES_MAP_NAME);
    MappedFile mfValability = OpenMappedFile(VALABILITY_MAP_NAME);
    MappedFile mfPrices = OpenMappedFile(PRICES_MAP_NAME);

    if (!mfShelves.pData || !mfValability.pData || !mfPrices.pData) {
        std::cerr << "donate.exe: Eroare la deschiderea fisierelor mapate!" << std::endl;
        return 1;
    }

    DWORD* shelves = mfShelves.pData;
    DWORD* valability = mfValability.pData;
    DWORD* prices = mfPrices.pData;

    // Deschidere mutex-uri (request full access)
    HANDLE hLogsMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, LOGS_MUTEX_NAME);
    HANDLE hErrorsMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, ERRORS_MUTEX_NAME);
    HANDLE hDonationsMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, DONATIONS_MUTEX_NAME);
    HANDLE hShelvesMutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, SHELVES_MUTEX_NAME);

    if (!hLogsMutex || !hErrorsMutex || !hDonationsMutex || !hShelvesMutex) {
        std::cerr << "donate.exe: Eroare la deschiderea mutex-urilor!" << std::endl;
        return 1;
    }

    // Inițializare barieră
    DayBarrier barrier;

    // Obținere număr de zile (din deposit pentru sincronizare)
    std::string depositDir = "C:\\Users\\sarah\\tema4\\deposit";
    std::vector<std::string> files = GetSortedFiles(depositDir);

    if (files.empty()) {
        std::ostringstream oss;
        oss << "donate.exe: Nu s-au gasit fisiere in directorul deposit/ pentru sincronizare";
        AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
        CloseMappedFile(mfShelves);
        CloseMappedFile(mfValability);
        CloseMappedFile(mfPrices);
        CloseHandle(hLogsMutex);
        CloseHandle(hErrorsMutex);
        CloseHandle(hDonationsMutex);
        CloseHandle(hShelvesMutex);
        return 1;
    }

    std::cout << "donate.exe: Proceseaza " << files.size() << " zile" << std::endl;

    for (size_t dayIdx = 0; dayIdx < files.size(); dayIdx++) {
        std::string filename = files[dayIdx];
        std::cout << "donate.exe: Procesare zi " << (dayIdx + 1) << "/" << files.size() << std::endl;

        bool haveSlot = false;

        // Achiziționare slot din semafor
        if (!barrier.AcquireDaySlot()) {
            std::ostringstream oss;
            oss << "donate.exe: Timeout la achiziționare slot semafor pentru ziua " << (dayIdx + 1);
            AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
            barrier.ArriveDonate();
            barrier.Arrive();
            continue;
        }
        haveSlot = true;

        // Așteptare pentru faza de donate (după sell)
        if (!barrier.WaitForDonatePhase()) {
            std::ostringstream oss;
            oss << "donate.exe: Timeout la asteptare SellDone pentru ziua " << (dayIdx + 1);
            AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
            barrier.ArriveDonate();
            barrier.Arrive();
            if (haveSlot) barrier.ReleaseDaySlot();
            continue;
        }

        // Collect expired items and update shelves under the shelves mutex only
        struct ExpiredItem { DWORD id; DWORD price; };
        std::vector<ExpiredItem> expiredItems;
        std::vector<DWORD> donatedIds;

        WaitForSingleObject(hShelvesMutex, INFINITE);

        // Build reverse map: product_id -> shelf_id (O(N) pass once)
        std::vector<int> productToShelf(MARKET_SIZE, -1);
        for (int j = 0; j < MARKET_SIZE; j++) {
            DWORD prod = shelves[j];
            if (prod != INVALID_VALUE && prod < MARKET_SIZE) {
                productToShelf[prod] = j;
            }
        }

        // Single-pass scan: collect expired items, update shelves, decrement others
        for (int id_produs = 0; id_produs < MARKET_SIZE; id_produs++) {
            DWORD v = valability[id_produs];

            if (v == INVALID_VALUE) continue;

            if (v == 0) {
                // Product expired: collect for donation
                DWORD price = prices[id_produs];
                expiredItems.push_back({ (DWORD)id_produs, price });

                // Mark as removed in shared maps
                valability[id_produs] = INVALID_VALUE;
                prices[id_produs] = INVALID_VALUE;

                // Remove from shelf using reverse map (O(1) lookup)
                int shelfIdx = productToShelf[id_produs];
                if (shelfIdx >= 0 && shelfIdx < MARKET_SIZE) {
                    shelves[shelfIdx] = INVALID_VALUE;
                }
            }
            else if (v > 0 && v < INVALID_VALUE) {
                // Decrement valability for next day
                valability[id_produs] = v - 1;
            }
        }

        ReleaseMutex(hShelvesMutex);

        // Process donations outside the shelves mutex
        double totalDonation = 0.0;
        std::string logsBatch;
        logsBatch.reserve(expiredItems.size() * 64);

        for (const auto& it : expiredItems) {
            if (it.price != INVALID_VALUE) {
                totalDonation += (double)it.price;
            }
            donatedIds.push_back(it.id);

            // Build batch log lines
            std::ostringstream oss;
            oss << "Produsul " << it.id << " a fost donat";
            logsBatch += oss.str();
            logsBatch += "\r\n";
        }

        // Write logs in a single batch
        if (!logsBatch.empty()) {
            if (hLogsMutex) WaitForSingleObject(hLogsMutex, INFINITE);
            AppendTextToFileA_Win(LOGS_FILE, logsBatch);
            if (hLogsMutex) ReleaseMutex(hLogsMutex);
        }

        // Update donations summary (single call)
        if (totalDonation > 0.0) {
            UpdateDonationsFile(totalDonation, hDonationsMutex);
        }

        // Write per-day donated ids (no global mutex needed; filename is unique per day)
        {
            std::string outPath = std::string(SUMMARY_DIR) + "\\donated_" + filename + ".txt";
            std::vector<std::string> lines;
            lines.reserve(donatedIds.size());
            for (DWORD id : donatedIds) {
                lines.push_back(std::to_string(id));
            }
            if (!WriteLinesToFile_Win(outPath, lines)) {
                std::ostringstream oss;
                oss << "donate.exe: Nu pot scrie fisierul de raport " << outPath;
                AppendToFile(ERRORS_FILE, oss.str(), hErrorsMutex);
            }
        }

        std::cout << "donate.exe: Finalizat procesare zi " << (dayIdx + 1) << std::endl;

        barrier.ArriveDonate();
        barrier.Arrive();

        if (haveSlot) barrier.ReleaseDaySlot();
    }

    // Cleanup
    CloseMappedFile(mfShelves);
    CloseMappedFile(mfValability);
    CloseMappedFile(mfPrices);

    CloseHandle(hLogsMutex);
    CloseHandle(hErrorsMutex);
    CloseHandle(hDonationsMutex);
    CloseHandle(hShelvesMutex);

    return 0;
}