#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <semaphore.h>

#define SHM_NAME "/estado_centrais"
#define PIPE_ENERGIA "/tmp/pipe_energia"
#define PIPE_ALERTAS "/tmp/pipe_alertas"
#define SEM_NAME "/sem_centrais"

sem_t *sem;
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

    sem_wait(sem);
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
    sem_post(sem);
    return eleito;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Não foi possivel criar.\n");
        return -1;
    }
    N = atoi(argv[1]);
    if (N <= 0)
    {
        printf("Numero de centrais incorretro.\n");
    }
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);
    sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);

    if (sem == SEM_FAILED)
    {
        perror("Erro a criar semaforos.\n");
        return -1;
    }

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if(shm_fd == -1)
    {
        printf("Erro a abrir SHM.\n");
        return -1;
    }

    size_t shm_size = N * sizeof(estado_central);
    if (ftruncate(shm_fd, shm_size) == -1)
    {
        printf("Erro a alocar memoria.\n");
        return -1;
    }

    shared = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED)
    {
        printf("Erro a criar mapa.\n");
        return -1;
    }

    //iniciar os estados da Struct estado_central
    for (int i = 0; i < N; i++)
    {
        shared[i].pid = -1;
        shared[i].estado = 1;
    }
    //


    pipe_energia_fd = open(PIPE_ENERGIA, O_WRONLY);
    if (pipe_energia_fd == -1 )
    {
        printf("Erro a abrir pipe de energia.\n");
        return -1;
    }
    pipe_alertas_fd = open(PIPE_ALERTAS, O_WRONLY);
    if (pipe_alertas_fd == -1)
    {
        printf("Erro a abrir pipe de alertas.\n");
        return -1;
    }

    for (int i = 0; i < N ; i++)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            printf("Erro a criar o fork.\n");
            return -1;
        }
        else if (pid == 0)
        {
            sem_wait(sem);
            shared[i].pid = getpid();
            shared[i].estado = 1;
            sem_post(sem);
            srand(time(NULL) ^ getpid());

            while (1)
            {
                int ativo;
                sem_wait(sem);
                ativo = shared[i].estado;
                sem_post(sem);
                if (ativo)
                {
                    int energia = rand() % 100 + 1;
                    char buffer[64];
                    snprintf(buffer, sizeof(buffer),"%d", energia);
                    write(pipe_energia_fd, buffer, strlen(buffer) + 1);
                }
                sleep(2);

                if (rand() % 10 == 0)
                {
                    sem_wait(sem);
                    shared[i].estado = 0;
                    sem_post(sem);

                    pid_t eleito = escolher_eleito();
                    if (eleito == -1)
                    {
                        char alerta[128];
                        snprintf(alerta, sizeof(alerta),"Aviso: A central %d entrou em manutenção.", getpid());
                        write(pipe_alertas_fd, alerta, strlen(alerta)+ 1);
                    }
                    sleep(30);

                    sem_wait(sem);
                    shared[i].estado = 1;
                    sem_post(sem);

                    eleito = escolher_eleito();
                    if (eleito != -1)
                    {
                        char alerta[128];
                        snprintf(alerta, sizeof(alerta),"Aviso: A central %d terminou a manutenção.", getpid());
                        write(pipe_alertas_fd, alerta, strlen(alerta)+ 1);
                    }
                }
            }
            exit(0);
        }
    }
    
    for (int i = 0; i < N; i++)
    {
        wait(NULL);
    }
    munmap(shared, shm_size);
    shm_unlink(SHM_NAME);
    sem_close(sem);
    sem_unlink(SEM_NAME);
    close(pipe_alertas_fd);
    close(pipe_energia_fd);
    return 0;
}
