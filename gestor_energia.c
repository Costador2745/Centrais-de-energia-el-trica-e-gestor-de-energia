#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

#define tamanho 256 // tamanho do buffer da leitura
#define PIPE_ENERGIA "/tmp/pipe_energia"  // pipe para dados de energia
#define PIPE_ALERTAS "/tmp/pipe_alertas"  // pipe para mensagens de alerta

int fd_energia, fd_alertas; // globais para serem usados na função terminar()

// função chamada quando o programa recebe SIGINT (Ctrl+C)
void terminar(int sig)
{
    close(fd_energia);
    close(fd_alertas);
    unlink(PIPE_ENERGIA);
    unlink(PIPE_ALERTAS);
    write(1, "CTRL+C recebido. Terminar...\n", 29);
    _exit(0);
}

int main()
{
    signal(SIGINT, terminar); // termina quando o utilizador digita Ctrl+C

    // cria pipes nomeados
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

    //abre os pipes para leitura 
    //O_NONBLOCK: o programa não fica bloqueado se não houver dados
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

    char buffer[tamanho];
    int energia_total = 0;
    int potencia_instantanea = 0;

    while (1) // o gestor está sempre ativo
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd_energia, &read_fds);
        FD_SET(fd_alertas, &read_fds);
        // Define quais os pipes que o programa vai observar

        int max_fd = (fd_energia > fd_alertas) ? fd_energia : fd_alertas; 
        // o select() precisa do maior descritor + 1

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL); 
        // fica à espera até haver dados em qualquer um dos pipes

        if (activity < 0)
        {
            perror("Erro na função select.\n");
            return -1;
        }

        if (FD_ISSET(fd_energia, &read_fds))
        {   
            ssize_t n = read(fd_energia, buffer, sizeof(buffer) - 1); 
            // ssize_t é um tipo inteiro com sinal, usado para guardar o número de bytes lidos

            if (n > 0)
            {
                buffer[n] = '\0'; // garante que o buffer termina em '\0', tornando-o uma string válida
                int valor = atoi(buffer); // converte a string recebida para inteiro

                energia_total += valor;
                potencia_instantanea = valor;

                printf("[DADOS] Energia Total Produzida: %d MWh |[DADOS] Potência Instantânea Produzida: %d kW\n", energia_total, potencia_instantanea);

            }
            else if (n == 0)
            {
                close(fd_energia); 
                // se read() devolver 0, significa que o processo escritor fechou a extremidade de escrita do pipe
                fd_energia = open(PIPE_ENERGIA, O_RDONLY | O_NONBLOCK); 
                // reabre o pipe para continuar a receber dados de futuros escritores
                if(fd_energia == -1)
                {
                    perror("Não foi possivel abrir a pipe energia.\n");
                    return -1;
                }
            }
        }

        if (FD_ISSET(fd_alertas, &read_fds))
        {
            ssize_t i = read(fd_alertas, buffer, sizeof(buffer) - 1); 

            if (i > 0)
            {
                buffer[i] = '\0'; // termina a string lida do pipe com '\0'
                printf("[ALERTA] %s\n", buffer);
            }
            else if (i == 0)
            {
                close(fd_alertas); 
                // se read() devolver 0, significa que o processo que envia alertas terminou ou fechou o pipe
                fd_alertas = open(PIPE_ALERTAS, O_RDONLY | O_NONBLOCK); 
                // reabre o pipe para permitir novas ligações de escritores no futuro
                if (fd_alertas == -1)
                {
                    perror("Não foi possivel abrir a pipe alertas.\n");
                    return -1;
                }
            }
        }
    }

    return 0;
}
