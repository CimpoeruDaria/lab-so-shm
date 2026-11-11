#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>

constexpr const char* SHM_NAME = "/shm_counter_lab";
constexpr const char* SEM_NAME = "/sem_counter_lab";

int main() {
    int shm_res = shm_unlink(SHM_NAME);
    int sem_res = sem_unlink(SEM_NAME);

    if (shm_res == 0 && sem_res == 0) {
        std::cout << "[CLEANUP] Resursele POSIX au fost curatate cu succes.\n";
    } else {
        std::cout << "[CLEANUP] Resursele nu au fost gasite sau au fost deja sterse.\n";
    }

    return 0;
}
