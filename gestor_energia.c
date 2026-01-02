#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#define tamanho 256 //tamanho do buffer da leitura
#define PIPE_ENERGIA "/tmp/pipe_energia" //pipe para dados de energia
#define PIPE_ALERTAS "/tmp/pipe_alertas" //pipe para mensagens de alerta

int fd_energia, fd_alertas;
//São globais para poderem ser usados na função terminar()

void terminar(int sig) //Função quando o programa recebe SIGINT
{
    close(fd_energia);
    close(fd_alertas);
    unlink("/tmp/pipe_energia");
    unlink("/tmp/pipe_alertas");
    write(1, "CTRL+C recebido. Terminar...\n", 29);
    _exit(0);
}

int main()
{
    signal(SIGINT, terminar); //termina quando o utilizador digita Ctrl+C

    if (mkfifo(PIPE_ENERGIA, 0666) == -1)
    {
        if (errno != EEXIST)
        {
            perror("Não foi possível criar o pipe de energia.\n");
            return -1;
        }
    }
    if (mkfifo(PIPE_ALERTAS, 0666) == -1)
    {
        if (errno != EEXIST)
        {
            perror("Não foi possivel criar o pipe de alertas.\n");
            return -1;
        }
    }
    //Cria as named pipes

    fd_energia = open(PIPE_ENERGIA, O_RDONLY | O_NONBLOCK);
    if(fd_energia == -1)
    {
        perror("Não foi possivel abrir a pipe energia.\n");
        return -1;
    }
    fd_alertas = open(PIPE_ALERTAS, O_RDONLY | O_NONBLOCK);
    if (fd_alertas == -1)
    {
        perror("Não foi possivel abrir a pipe alertas.\n");
        return -1;
    }
    //Abre os pipes para leitura
    //O_NONBLOCK: o programa não fica bloqueado se não houver dados

    char buffer[tamanho];
    int energia_total = 0;
    int potencia_instantanea = 0;
    while (1) //O gestor está sempre ativo
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd_energia, &read_fds);
        FD_SET(fd_alertas, &read_fds);
        //Define quais os pipes que o programa vai observar

        int max_fd = (fd_energia > fd_alertas) ? fd_energia : fd_alertas; //O select() precisa do maior descritor + 1 nos argumentos da sua função select().

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL); //O programa fica à espera até haver dados em qualquer um dos pipes.

        if (activity < 0)
        {
            perror("Erro na função select.\n");
            return -1;
        }
        if (FD_ISSET(fd_energia, &read_fds)) //Verifica se chegaram dados no pipe de energia.
        {   
            ssize_t n = read(fd_energia, buffer, sizeof(buffer) - 1); //Lê os dados recebidos. ssize_t é um tipo inteiro com sinal, usado para guardar o número de bytes lidos
            if (n > 0)
            {
                buffer[n] = '\0'; // Garante que o buffer termina em '\0', tornando-o uma string válida.
                int valor = atoi(buffer);
                energia_total += valor;
                potencia_instantanea = valor;
                printf("[DADOS] Energia Total Produzida: %d MWh |[DADOS] Potência Instantânea Produzida: %d kW\n", energia_total, potencia_instantanea);
                //Extrai: energia → valor a somar; potencia → valor instantâneo

            }
            else if (n == 0)
            {
                close(fd_energia);                                                             // Se read() devolver 0, significa que o processo escritor
                fd_energia = open(PIPE_ENERGIA, O_RDONLY | O_NONBLOCK);                        // fechou a extremidade de escrita do pipe 
                if(fd_energia == -1)                                                           // Fecha-se o descritor atual e reabre-se o pipe para continuar  
                {                                                                              // a receber dados quando um novo escritor se ligar.
                    perror("Não foi possivel abrir a pipe energia.\n");
                    return -1;
                }
            }
        }
        if (FD_ISSET(fd_alertas, &read_fds)) //Verifica se chegou um alerta.
        {
            ssize_t i = read(fd_alertas, buffer, sizeof(buffer) - 1);
            if (i > 0)
            {
                buffer[i] = '\0';
                printf("[ALERTA] %s\n", buffer);
            }
            else if (i == 0)
            {
                close(fd_alertas);                                                           // Se read() devolver 0, significa que o processo que envia alertas
                fd_alertas = open(PIPE_ALERTAS, O_RDONLY | O_NONBLOCK);                      // terminou ou fechou o pipe.
                if (fd_alertas == -1)                                                        // O descritor é fechado e o pipe é reaberto para permitir
                {                                                                            // novas ligações de escritores no futuro.
                    perror("Não foi possivel abrir a pipe alertas.\n");
                    return -1;
                }
            }
        }
    }
    return 0;
}