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

#define SHM_NAME "/estado_centrais" //memoria partilhada com o estado das centrais
#define PIPE_ENERGIA "/tmp/pipe_energia"
#define PIPE_ALERTAS "/tmp/pipe_alertas"
#define SEM_NAME "/sem_centrais" //semaforo para sincronizar acesso a memoria

sem_t *sem; //semaforo POSIX*
typedef struct {
    pid_t pid;
    int estado; // 0 - desligada, 1 - ligada

} estado_central;

estado_central *shared = NULL; //shared: ponteiro para a memoria partilhada
int N; //numero de centrais
int pipe_energia_fd;
int pipe_alertas_fd;

pid_t escolher_eleito()
{
    pid_t eleito = -1;
    sem_wait(sem); // bloqueia acesso à SHM

    for (int i = 0; i < N; i++)
    {
        if (shared[i].estado == 1 && shared[i].pid != -1) // so centrais ativas
        {
            if (eleito == -1 || shared[i].pid < eleito) // escolhe menor PID
                eleito = shared[i].pid;
        }
    }
    sem_post(sem); // liberta acesso à SHM
    return eleito;
}

int main(int argc, char *argv[])
{
    if (argc != 2) //O programa espera 1 argumento: numero de centrais
    {
        printf("Não foi possivel criar.\n");
        return -1;
    }
    N = atoi(argv[1]);
    if (N <= 0)
    {
        printf("Numero de centrais incorreto.\n");
        return -1;
    }
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);
    //remove restos de execucoes anteriores

    sem = sem_open(SEM_NAME, O_CREAT, 0666, 1); //semaforo binario (mutex) -> valor inicial = 1 -> garante acesso exclusivo a memoria partilhada

    if (sem == SEM_FAILED)
    {
        perror("Erro a criar semaforos.\n");
        return -1;
    }

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666); //Cria uma memoria partilhada
    if(shm_fd == -1)
    {
        printf("Erro a abrir SHM.\n");
        return -1;
    }
    //Cria uma memoria partilhada

    size_t shm_size = N * sizeof(estado_central); //Define tamanho para N estrutura estado_central
    if (ftruncate(shm_fd, shm_size) == -1) //ftruncate garante que a SHM tem espaço suficiente para todos os processos
    {
        printf("Erro a alocar memoria.\n");
        return -1;
    }

    shared = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); //Mapeia a memoria no espaco do processo
    if (shared == MAP_FAILED)
    {
        printf("Erro a criar mapa.\n");
        return -1;
    }
    close(shm_fd); // descritor ja nao e necessario

    for (int i = 0; i < N; i++) //iniciar os estados da Struct estado_central
    {
        shared[i].pid = -1;
        shared[i].estado = 1;
    }
    //todas as centrais comecam ligadas, sem o pid definido

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
    //Abrem-se as pipes
    //open() bloqueiam ate existir um leitor

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
            srand(time(NULL) ^ getpid()); //garante números diferentes em cada processo

            int estados_anteriores[N]; //array para guardar estados anteriores das centrais
            sem_wait(sem);
            for (int j = 0; j < N; j++)
            {
                estados_anteriores[j] = shared[j].estado; // copia estados iniciais
            }
            sem_post(sem);

            while (1)
            {
                int ativo;
                sem_wait(sem);
                ativo = shared[i].estado;
                sem_post(sem);
                if (ativo) // so produz energia se estiver ativa
                {
                    int energia = rand() % 100 + 1;
                    char buffer[64];
                    snprintf(buffer, sizeof(buffer),"%d", energia);
                    write(pipe_energia_fd, buffer, strlen(buffer) + 1);
                }
                //So produz energia quando esta ligada
                //Envia valores entre 1 e 100, pelo pipe de energia
                sleep(2);

                int estados_atuais[N];
                pid_t pids_atuais[N];

                sem_wait(sem);
                for (int j = 0; j < N; j++)
                {
                    estados_atuais[j] = shared[j].estado; // copia estado atual
                    pids_atuais[j] = shared[j].pid; // copia PID atual
                }
                sem_post(sem);

                for (int j = 0; j < N; j++)
                {
                    if (estados_atuais[j] != estados_anteriores[j] && pids_atuais[j] != -1) // deteta mudanca
                    {
                        pid_t eleito = escolher_eleito();

                        if (eleito == getpid()) // so o eleito envia alerta
                        {
                            char alerta[128];
                            if (estados_atuais[j] == 0)
                                snprintf(alerta, sizeof(alerta), "Aviso: A central %d entrou em manutenção.", pids_atuais[j]);
                            else
                                snprintf(alerta, sizeof(alerta), "Aviso: A central %d terminou a manutenção.", pids_atuais[j]);

                            write(pipe_alertas_fd, alerta, strlen(alerta) + 1);
                        }
                    estados_anteriores[j] = estados_atuais[j]; // atualiza estado antigo
                    }
                }
                if (rand() % 10 == 0) //10% de probabilidade de entrar em manutencao
                {
                    sem_wait(sem);
                    shared[i].estado = 0;
                    sem_post(sem);

                    sleep(30);

                    sem_wait(sem);
                    shared[i].estado = 1;
                    sem_post(sem);
                }
            }
            exit(0);
        }
    }
    
    for (int i = 0; i < N; i++)
    {
        wait(NULL); //processo pai espra pelos filhos
    }
    munmap(shared, shm_size);
    shm_unlink(SHM_NAME);
    sem_close(sem);
    sem_unlink(SEM_NAME);
    close(pipe_alertas_fd);
    close(pipe_energia_fd);
    //limpeza final
    return 0;
}
