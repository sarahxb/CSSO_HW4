#ifndef COMMON_H
#define COMMON_H

#include <windows.h>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <algorithm>

// Constante
#define MARKET_SIZE 10000
#define INVALID_VALUE 0xFFFFFFFF
#define DAY_BARRIER_TIMEOUT 60000
#define PROCESS_TIMEOUT 60000

// Nume pentru obiecte de sincronizare
#define SHELVES_MAP_NAME L"MarketShelves"
#define VALABILITY_MAP_NAME L"MarketValability"
#define PRICES_MAP_NAME L"ProductPrices"

#define LOGS_MUTEX_NAME L"LogsMutex"
#define ERRORS_MUTEX_NAME L"ErrorsMutex"
#define SOLD_MUTEX_NAME L"SoldMutex"
#define DONATIONS_MUTEX_NAME L"DonationsMutex"
#define SHELVES_MUTEX_NAME L"ShelvesMutex"

#define BARRIER_EVENT_DEPOSIT L"BarrierDepositDone"
#define BARRIER_EVENT_SELL L"BarrierSellDone"
#define BARRIER_EVENT_DONATE L"BarrierDonateDone"
#define BARRIER_EVENT_MANAGER L"BarrierManagerReady"
#define BARRIER_EVENT_RELEASE L"BarrierRelease"

#define BARRIER_SEMAPHORE L"BarrierSemaphore"
#define BARRIER_MUTEX_NAME L"BarrierMutex"
#define BARRIER_COUNTER_MAP_NAME L"BarrierCounter"

// Căi fișiere
#define REPORTS_DIR "C:\\Facultate\\CSSO\\H4\\Reports"
#define SUMMARY_DIR "C:\\Facultate\\CSSO\\H4\\Reports\\Summary"
#define LOGS_FILE "C:\\Facultate\\CSSO\\H4\\Reports\\logs.txt"
#define ERRORS_FILE "C:\\Facultate\\CSSO\\H4\\Reports\\Summary\\errors.txt"
#define SOLD_FILE "C:\\Facultate\\CSSO\\H4\\Reports\\Summary\\sold.txt"
#define DONATIONS_FILE "C:\\Facultate\\CSSO\\H4\\Reports\\Summary\\donations.txt"

// Structuri pentru fișiere mapate
struct MappedFile {
    HANDLE hMapFile;
    DWORD* pData;
};

// Funcții helper (Windows API only)
bool CreateDirectoryRecursive(const std::string& path) {
    size_t pos = 0;
    do {
        pos = path.find_first_of("\\/", pos + 1);
        std::string subpath = path.substr(0, pos);
        if (!subpath.empty()) {
            CreateDirectoryA(subpath.c_str(), NULL);
        }
    } while (pos != std::string::npos);
    return true;
}

MappedFile OpenMappedFile(const wchar_t* name, SIZE_T size = MARKET_SIZE * sizeof(DWORD)) {
    MappedFile mf = { NULL, NULL };
    mf.hMapFile = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (mf.hMapFile != NULL) {
        mf.pData = (DWORD*)MapViewOfFile(mf.hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, size);
    }
    return mf;
}

void CloseMappedFile(MappedFile& mf) {
    if (mf.pData) UnmapViewOfFile(mf.pData);
    if (mf.hMapFile) CloseHandle(mf.hMapFile);
}

// Read entire file into std::string using WinAPI. Returns empty string on failure or empty file.
static std::string ReadFileToStringA_Win(const std::string& filename) {
    HANDLE h = CreateFileA(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart == 0) {
        CloseHandle(h);
        return std::string();
    }
    std::string buf;
    buf.resize((size_t)size.QuadPart);
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(h, &buf[0], (DWORD)size.QuadPart, &bytesRead, NULL);
    CloseHandle(h);
    if (!ok || bytesRead == 0) return std::string();
    if ((size_t)bytesRead < buf.size()) buf.resize(bytesRead);
    return buf;
}

// Split text into lines (handles CRLF and LF)
static std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t i = 0, n = text.size();
    std::string cur;
    while (i < n) {
        char c = text[i++];
        if (c == '\r') {
            if (i < n && text[i] == '\n') ++i;
            lines.push_back(cur);
            cur.clear();
        }
        else if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        }
        else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

// Write raw text to file using WinAPI. creationDisposition: CREATE_ALWAYS / CREATE_NEW / OPEN_ALWAYS
static bool WriteTextToFileA_Win(const std::string& filename, const std::string& text, DWORD creationDisposition = CREATE_ALWAYS) {
    HANDLE h = CreateFileA(filename.c_str(), GENERIC_WRITE, 0, NULL, creationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, text.c_str(), (DWORD)text.size(), &written, NULL);
    CloseHandle(h);
    return ok && written == (DWORD)text.size();
}

// Append text (with newline) to file using WinAPI. Creates file if missing.
static void AppendTextToFileA_Win(const std::string& filename, const std::string& text) {
    HANDLE h = CreateFileA(filename.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    std::string toWrite = text + "\r\n";
    DWORD written = 0;
    WriteFile(h, toWrite.c_str(), (DWORD)toWrite.size(), &written, NULL);
    CloseHandle(h);
}

// Helpers used by processes (wrap with mutex externally)
void AppendToFile(const std::string& filename, const std::string& text, HANDLE hMutex) {
    if (hMutex) WaitForSingleObject(hMutex, INFINITE);
    AppendTextToFileA_Win(filename, text);
    if (hMutex) ReleaseMutex(hMutex);
}

double ReadDoubleFromFile(const std::string& filename, HANDLE hMutex) {
    double value = 0.0;
    if (hMutex) WaitForSingleObject(hMutex, INFINITE);
    std::string content = ReadFileToStringA_Win(filename);
    if (!content.empty()) {
        // parse until non-numeric (safe)
        try {
            value = std::stod(content);
        }
        catch (...) { value = 0.0; }
    }
    if (hMutex) ReleaseMutex(hMutex);
    return value;
}

void WriteDoubleToFile(const std::string& filename, double value, HANDLE hMutex) {
    if (hMutex) WaitForSingleObject(hMutex, INFINITE);
    std::ostringstream oss;
    oss << value;
    WriteTextToFileA_Win(filename, oss.str(), CREATE_ALWAYS);
    if (hMutex) ReleaseMutex(hMutex);
}

void UpdateSoldFile(double netPrice, HANDLE hMutex) {
    double currentValue = ReadDoubleFromFile(SOLD_FILE, hMutex);
    WriteDoubleToFile(SOLD_FILE, currentValue + netPrice, hMutex);
}

void UpdateDonationsFile(double price, HANDLE hMutex) {
    double currentValue = ReadDoubleFromFile(DONATIONS_FILE, hMutex);
    WriteDoubleToFile(DONATIONS_FILE, currentValue + price, hMutex);
}

// Read file lines using WinAPI. Returns empty vector if file missing or empty.
std::vector<std::string> ReadLinesFromFile_Win(const std::string& filename) {
    std::string txt = ReadFileToStringA_Win(filename);
    if (txt.empty()) return std::vector<std::string>();
    return SplitLines(txt);
}

// Write lines to file (overwrite) using WinAPI
bool WriteLinesToFile_Win(const std::string& filename, const std::vector<std::string>& lines) {
    std::string joined;
    for (size_t i = 0; i < lines.size(); ++i) {
        joined += lines[i];
        if (i + 1 < lines.size()) joined += "\r\n";
    }
    return WriteTextToFileA_Win(filename, joined, CREATE_ALWAYS);
}

// Funcție helper pentru extragere numere din string
std::vector<int> ExtractNumbersFromFilename(const std::string& s) {
    std::vector<int> nums;
    std::string current = "";
    for (size_t i = 0; i < s.length(); i++) {
        if (isdigit((unsigned char)s[i])) {
            current += s[i];
        }
        else {
            if (!current.empty()) {
                nums.push_back(atoi(current.c_str()));
                current = "";
            }
        }
    }
    if (!current.empty()) {
        nums.push_back(atoi(current.c_str()));
    }
    return nums;
}

// Comparator pentru sortare numerică
bool CompareFilesNumerically(const std::string& a, const std::string& b) {
    std::vector<int> numsA = ExtractNumbersFromFilename(a);
    std::vector<int> numsB = ExtractNumbersFromFilename(b);

    // Compară număr cu număr
    size_t minSize = (numsA.size() < numsB.size()) ? numsA.size() : numsB.size();
    for (size_t i = 0; i < minSize; i++) {
        if (numsA[i] != numsB[i]) {
            return numsA[i] < numsB[i];
        }
    }

    // Dacă toate numerele sunt egale, compară dimensiunea
    return numsA.size() < numsB.size();
}

std::vector<std::string> GetSortedFiles(const std::string& directory) {
    std::vector<std::string> files;
    WIN32_FIND_DATAA findData;
    std::string searchPath = directory + "\\*";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.push_back(findData.cFileName);
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }

    // Sortare numerică pentru fișiere de forma "2024.11.9.txt"
    std::sort(files.begin(), files.end(), CompareFilesNumerically);

    return files;
}

// Barieră în 2 faze: arrive + release (unchanged, uses WinAPI handles)
class DayBarrier {
private:
    HANDLE hDepositDone;
    HANDLE hSellDone;
    HANDLE hDonateDone;
    HANDLE hManagerReady;
    HANDLE hSemaphore;

    // Barieră: eveniment de release + contor partajat
    HANDLE hBarrierRelease;
    MappedFile mfBarrierCounter;
    static const int NUM_PROCESSES = 3; // deposit, sell, donate

public:
    DayBarrier() {
        hDepositDone = OpenEventW(EVENT_ALL_ACCESS, FALSE, BARRIER_EVENT_DEPOSIT);
        hSellDone = OpenEventW(EVENT_ALL_ACCESS, FALSE, BARRIER_EVENT_SELL);
        hDonateDone = OpenEventW(EVENT_ALL_ACCESS, FALSE, BARRIER_EVENT_DONATE);
        hManagerReady = OpenEventW(EVENT_ALL_ACCESS, FALSE, BARRIER_EVENT_MANAGER);
        hSemaphore = OpenSemaphoreW(SEMAPHORE_ALL_ACCESS, FALSE, BARRIER_SEMAPHORE);

        // Barieră: eveniment pentru a aștepta ca toate procesele să ajungă
        hBarrierRelease = OpenEventW(EVENT_ALL_ACCESS, FALSE, BARRIER_EVENT_RELEASE);
        // Barieră: contor partajat (un singur LONG)
        mfBarrierCounter = OpenMappedFile(BARRIER_COUNTER_MAP_NAME, sizeof(LONG));

        // Debug: verify handles
        if (!hDepositDone || !hSellDone || !hDonateDone || !hManagerReady || !hSemaphore ||
            !hBarrierRelease || !mfBarrierCounter.pData) {
            std::cerr << "EROARE DayBarrier: Nu pot deschide obiecte sincronizare!" << std::endl;
        }
    }

    ~DayBarrier() {
        if (hDepositDone) CloseHandle(hDepositDone);
        if (hSellDone) CloseHandle(hSellDone);
        if (hDonateDone) CloseHandle(hDonateDone);
        if (hManagerReady) CloseHandle(hManagerReady);
        if (hSemaphore) CloseHandle(hSemaphore);
        if (hBarrierRelease) CloseHandle(hBarrierRelease);
        CloseMappedFile(mfBarrierCounter);
    }

    bool AcquireDaySlot() {
        if (!hSemaphore) return false;
        DWORD result = WaitForSingleObject(hSemaphore, DAY_BARRIER_TIMEOUT);
        return (result == WAIT_OBJECT_0);
    }

    void ReleaseDaySlot() {
        if (hSemaphore) {
            ReleaseSemaphore(hSemaphore, 1, NULL);
        }
    }

    void Arrive() {
        if (!mfBarrierCounter.pData || !hBarrierRelease) return;
        
        LONG* counter = (LONG*)mfBarrierCounter.pData;
        LONG currentCount = InterlockedIncrement(counter);
        
        if (currentCount == NUM_PROCESSES) {
            // Last to arrive: release all waiters
            SetEvent(hBarrierRelease);
        }
        else {
            // Wait for signal from last arriver
            DWORD res = WaitForSingleObject(hBarrierRelease, DAY_BARRIER_TIMEOUT);
            if (res != WAIT_OBJECT_0) {
                std::cerr << "Barrier timeout!" << std::endl;
            }
        }
        
        // All processes exit here; management resets the event externally
    }

    void ArriveDeposit() { if (hDepositDone) SetEvent(hDepositDone); }
    void ArriveSell() { if (hSellDone) SetEvent(hSellDone); }
    void ArriveDonate() { if (hDonateDone) SetEvent(hDonateDone); }

    bool WaitForDepositPhase() {
        if (hManagerReady) {
            DWORD result = WaitForSingleObject(hManagerReady, DAY_BARRIER_TIMEOUT);
            return result == WAIT_OBJECT_0;
        }
        return true;
    }

    bool WaitForSellPhase() {
        if (hDepositDone) {
            DWORD result = WaitForSingleObject(hDepositDone, DAY_BARRIER_TIMEOUT);
            return result == WAIT_OBJECT_0;
        }
        return true;
    }

    bool WaitForDonatePhase() {
        if (hSellDone) {
            DWORD result = WaitForSingleObject(hSellDone, DAY_BARRIER_TIMEOUT);
            return result == WAIT_OBJECT_0;
        }
        return true;
    }

    void WaitForAllDone() {
        if (hDepositDone && hSellDone && hDonateDone) {
            HANDLE events[] = { hDepositDone, hSellDone, hDonateDone };
            WaitForMultipleObjects(3, events, TRUE, INFINITE);
        }
    }

    void ResetForNextDay() {
        if (hDepositDone) ResetEvent(hDepositDone);
        if (hSellDone) ResetEvent(hSellDone);
        if (hDonateDone) ResetEvent(hDonateDone);
        if (hManagerReady) SetEvent(hManagerReady);
    }
};

#endif