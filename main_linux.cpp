#include <iostream>
#include <random>
#include <chrono>
#include <thread>
#include <system_error>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>

constexpr const char* SHM_NAME = "/shm_counter_lab";
constexpr const char* SEM_NAME = "/sem_counter_lab";
constexpr int TARGET_COUNT = 1000;

struct SharedData {
    int counter;
    bool is_initialized;
};

// Clasa RAII pentru eliberarea automata a semaforului (lock_guard custom)
class SemaphoreLock {
public:
    explicit SemaphoreLock(sem_t* sem) : sem_(sem) {
        sem_wait(sem_);
    }
    ~SemaphoreLock() {
        sem_post(sem_);
    }
    SemaphoreLock(const SemaphoreLock&) = delete;
    SemaphoreLock& operator=(const SemaphoreLock&) = delete;

private:
    sem_t* sem_;
};

int main() {
    const pid_t pid = getpid();

    // 1. Initializare generator numere aleatorii modern din <random>
    std::mt19937 rng(static_cast<unsigned int>(
        std::chrono::system_clock::now().time_since_epoch().count() ^ (pid << 16)
    ));
    std::uniform_int_distribution<int> coin_dist(1, 2); // Simuleaza datul cu banul (1 sau 2)

    // 2. Creare / deschidere semafor binar (initializat cu 1)
    sem_t* sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        std::cerr << "Eroare sem_open: " << std::generic_category().message(errno) << '\n';
        return 1;
    }

    // 3. Creare / deschidere obiect memorie partajata
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        std::cerr << "Eroare shm_open: " << std::generic_category().message(errno) << '\n';
        sem_close(sem);
        return 1;
    }

    // Alocare dimensiune pentru structura noastra
    if (ftruncate(shm_fd, sizeof(SharedData)) == -1) {
        std::cerr << "Eroare ftruncate: " << std::generic_category().message(errno) << '\n';
        close(shm_fd);
        sem_close(sem);
        return 1;
    }

    // 4. Maparea memoriei partajate in spatiul virtual de adrese
    auto* data = static_cast<SharedData*>(mmap(
        nullptr,
        sizeof(SharedData),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        shm_fd,
        0
    ));

    if (data == MAP_FAILED) {
        std::cerr << "Eroare mmap: " << std::generic_category().message(errno) << '\n';
        close(shm_fd);
        sem_close(sem);
        return 1;
    }

    // 5. Initializarea memoriei in mod atomic (sectiune critica)
    {
        SemaphoreLock lock(sem);
        if (!data->is_initialized) {
            data->counter = 0;
            data->is_initialized = true;
            std::cout << "[PID " << pid << "] Memoria partajata a fost initializata (contor = 0).\n";
        }
    }

    std::cout << "[PID " << pid << "] Incepe procesul de numarare pana la " << TARGET_COUNT << "...\n";

    // 6. Bucla principala de incrementare concurenta
    while (true) {
        bool done = false;

        {
            // Preluam semaforul la intrarea in scope; se elibereaza automat la iesire
            SemaphoreLock lock(sem);

            // Citim memoria
            if (data->counter >= TARGET_COUNT) {
                done = true;
            } else {
                // Dam cu banul (1 sau 2)
                int coin = coin_dist(rng);

                // Cat timp cade 2, scriem in memorie urmatorul numar
                while (coin == 2 && data->counter < TARGET_COUNT) {
                    data->counter++;
                    std::cout << "[PID " << pid << "] Moneda=2 -> Scris in memorie: " 
                              << data->counter << '\n';
                    coin = coin_dist(rng);
                }
            }
        } // sem_post() se apeleaza automat aici prin destructorul clasei SemaphoreLock

        if (done) {
            break;
        }

        // Pauza minuscula pentru a permite celuilalt proces sa preia semaforul
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    std::cout << "[PID " << pid << "] Finalizat. Valoare finala a contorului: " 
              << data->counter << '\n';

    // Curatare resurse locale procesului
    munmap(data, sizeof(SharedData));
    close(shm_fd);
    sem_close(sem);

    return 0;
}
