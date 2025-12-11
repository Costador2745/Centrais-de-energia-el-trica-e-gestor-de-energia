#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>

#define SHM_NAME "/estado_centrais"

typedef struct {
    pid_t pid;
    int estado; // 0 - desligada, 1 - ligada

} estado_central;

estado_central *shared = NULL;
int N;

int pipe_energia_fd;
int pipe_alertas_fd;

pid_t escolher_eleito()
{
    pid_t eleito = -1;
    for (int i = 0; i < N; i++)
    {
        if (shared[i].estado == 1 && shared[i].pid != getpid())
        {
            if (eleito == -1 || shared[i].pid < eleito)
            {
                eleito = shared[i].pid;
            }
        }
    }
    return eleito;
}