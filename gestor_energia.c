#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#define tamanho 256
int main()
{
    const char *pipe_energia = "/tmp/pipe_energia";
    const char *pipe_alertas = "/tmp/pipe_alertas";

    if (mkfifo(pipe_energia, 0666) == -1)
    {
        if (errno != EEXIST)
        {
            perror("Não foi possível criar o pipe de energia.\n");
            return -1;
        }
    }
    if (mkfifo(pipe_alertas, 0666) == -1)
    {
        if (errno != EEXIST)
        {
            perror("Não foi possivel criar o pipe de alertas.\n");
            return -1;
        }
    }

    int fd_energia = open(pipe_energia, O_RDONLY | O_NONBLOCK);
    if(fd_energia == -1)
    {
        perror("Não foi possivel abrir a pipe energia.\n");
        return -1;
    }
    int fd_alertas = open(pipe_alertas, O_RDONLY | O_NONBLOCK);
    if (fd_alertas == -1)
    {
        perror("Não foi possivel abrir a pipe alertas.\n");
        return -1;
    }

    char buffer[tamanho];
    int energia_total = 0;
    int potencia_instananea = 0;
    while (1)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd_energia, &read_fds);
        FD_SET(fd_alertas, &read_fds);

        int max_fd = (fd_energia > fd_alertas) ? fd_energia : fd_alertas;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0)
        {
            perror("Erro na função select.\n");
            return -1;
        }
        if (FD_ISSET(fd_energia, &read_fds))
        {   
            ssize_t n = read(fd_energia, buffer, sizeof(buffer) - 1);
            if (n > 0)
            {
                buffer[n] = '\0';
                int valor = atoi(buffer);
                energia_total += valor;
                potencia_instananea = valor;
                printf("[DADOS] Energia Total Produzida: %d MWh |[DADOS] Potência Instantânea Produzida: %d kW\n", energia_total, potencia_instananea);
            }
            else if (n == 0)
            {
                close(fd_energia);
                fd_energia = open(pipe_energia, O_RDONLY | O_NONBLOCK);
            }
        }
        if (FD_ISSET(fd_alertas, &read_fds))
        {
            ssize_t i = read(fd_alertas, buffer, sizeof(buffer) - 1);
            if (i > 0)
            {
                buffer[i] = '\0';
                printf("%s\n", buffer);
            }
            else if (i == 0)
            {
                close(fd_alertas);
                fd_alertas = open(pipe_alertas, O_RDONLY | O_NONBLOCK);
            }
        }
    }
    close(fd_energia);
    close(fd_alertas);
    unlink("/tmp/pipe_energia");
    unlink("/tmp/pipe_alertas");
    return 0;
}