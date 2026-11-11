#include <windows.h>
#include <iostream>
#include <random>
#include <chrono>
#include <thread>

constexpr const char* SHM_NAME = "Local\\ShmCounterLab";
constexpr const char* SEM_NAME = "Local\\SemCounterLab";
constexpr int TARGET_COUNT = 1000;

struct SharedData {
    int counter;
    bool is_initialized;
};

// Clasa RAII pentru eliberarea automata a semaforului pe Windows
class WinSemaphoreLock {
public:
    explicit WinSemaphoreLock(HANDLE sem) : sem_(sem) {
        WaitForSingleObject(sem_, INFINITE);
    }
    ~WinSemaphoreLock() {
        ReleaseSemaphore(sem_, 1, nullptr);
    }
    WinSemaphoreLock(const WinSemaphoreLock&) = delete;
    WinSemaphoreLock& operator=(const WinSemaphoreLock&) = delete;

private:
    HANDLE sem_;
};

int main() {
    DWORD pid = GetCurrentProcessId();

    // 1. Initializare generator de numere aleatorii
    std::mt19937 rng(static_cast<unsigned int>(
        std::chrono::system_clock::now().time_since_epoch().count() ^ (pid << 16)
    ));
    std::uniform_int_distribution<int> coin_dist(1, 2);

    // 2. Creare / deschidere semafor numit (initializat cu 1, maxim 1)
    HANDLE hSem = CreateSemaphoreA(nullptr, 1, 1, SEM_NAME);
    if (hSem == nullptr) {
        std::cerr << "Eroare CreateSemaphore: " << GetLastError() << '\n';
        return 1;
    }

    // 3. Creare / deschidere memorie partajata (File Mapping in RAM)
    HANDLE hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(SharedData),
        SHM_NAME
    );
    if (hMapFile == nullptr) {
        std::cerr << "Eroare CreateFileMapping: " << GetLastError() << '\n';
        CloseHandle(hSem);
        return 1;
    }

    // 4. Maparea memoriei in spatiul de adrese al procesului
    auto* data = static_cast<SharedData*>(MapViewOfFile(
        hMapFile,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        sizeof(SharedData)
    ));
    if (data == nullptr) {
        std::cerr << "Eroare MapViewOfFile: " << GetLastError() << '\n';
        CloseHandle(hMapFile);
        CloseHandle(hSem);
        return 1;
    }

    // 5. Initializarea memoriei in sectiune critica
    {
        WinSemaphoreLock lock(hSem);
        if (!data->is_initialized) {
            data->counter = 0;
            data->is_initialized = true;
            std::cout << "[PID " << pid << "] Memoria partajata a fost initializata (contor = 0).\n";
        }
    }

    std::cout << "[PID " << pid << "] Incepe procesul de numarare pana la " << TARGET_COUNT << "...\n";

    // 6. Bucla principala de incrementare
    while (true) {
        bool done = false;

        {
            WinSemaphoreLock lock(hSem);

            if (data->counter >= TARGET_COUNT) {
                done = true;
            } else {
                int coin = coin_dist(rng);
                while (coin == 2 && data->counter < TARGET_COUNT) {
                    data->counter++;
                    std::cout << "[PID " << pid << "] Moneda=2 -> Scris in memorie: " 
                              << data->counter << '\n';
                    coin = coin_dist(rng);
                }
            }
        } // Semaforul se elibereaza automat aici

        if (done) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    std::cout << "[PID " << pid << "] Finalizat. Valoare finala a contorului: " << data->counter << '\n';

    // Curatare resurse locale
    UnmapViewOfFile(data);
    CloseHandle(hMapFile);
    CloseHandle(hSem);

    // Mentine fereastra deschisa pentru captura de ecran
    system("pause");

    return 0;
}
