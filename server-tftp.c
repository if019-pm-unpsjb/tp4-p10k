#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define PORT 69
#define BUFFER_SIZE 516
#define DATA_SIZE 512
#define BASE_DIR "./ficherosTFTPserver/" // Cambia esto al directorio deseado

enum
{
    RRQ = 1,
    WRQ,
    DATA,
    ACK,
    ERROR
};

void error_exit(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

void send_error(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, int error_code, const char *error_msg)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    *(uint16_t *)buffer = htons(ERROR);
    *(uint16_t *)(buffer + 2) = htons(error_code);
    strcpy(buffer + 4, error_msg);

    sendto(sockfd, buffer, 4 + strlen(error_msg) + 1, 0, (struct sockaddr *)client_addr, client_len);
    printf("Enviado paquete ERROR código: %d, mensaje: %s\n", error_code, error_msg);
}

void handle_rrq(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename)
{
    char filepath[BUFFER_SIZE];
    snprintf(filepath, BUFFER_SIZE, "%s%s", BASE_DIR, filename);

    int file = open(filepath, O_RDONLY);
    if (file < 0)
    {
        if (errno == ENOENT)
        {
            send_error(sockfd, client_addr, client_len, 1, "File not found");
        }
        else if (errno == EACCES)
        {
            send_error(sockfd, client_addr, client_len, 2, "Access violation");
        }
        else
        {
            send_error(sockfd, client_addr, client_len, 0, "Not defined");
        }
        return;
    }

    char buffer[BUFFER_SIZE];
    char data_buffer[DATA_SIZE];
    int block = 1;
    ssize_t read_bytes;

    while ((read_bytes = read(file, data_buffer, DATA_SIZE)) > 0)
    {
        memset(buffer, 0, BUFFER_SIZE);
        *(uint16_t *)buffer = htons(DATA);
        *(uint16_t *)(buffer + 2) = htons(block);
        memcpy(buffer + 4, data_buffer, read_bytes);

        printf("Enviando paquete DATA número: %d\n", block);
        sendto(sockfd, buffer, 4 + read_bytes, 0, (struct sockaddr *)client_addr, client_len);

        // Esperar ACK
        recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)client_addr, &client_len);
        printf("Recibido paquete ACK número: %d\n", ntohs(*(uint16_t *)(buffer + 2)));

        if (ntohs(*(uint16_t *)buffer) != ACK || ntohs(*(uint16_t *)(buffer + 2)) != block)
        {
            send_error(sockfd, client_addr, client_len, 4, "Illegal TFTP operation");
            close(file);
            return;
        }

        block++;
    }

    close(file);
    printf("Envio concluido\n");
}

void handle_wrq(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len, const char *filename)
{
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s%s", BASE_DIR, filename);
    printf("ruta completa: %s\n", filepath);
    int file = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file < 0)
    {
        if (errno == EEXIST)
        {
            send_error(sockfd, client_addr, client_len, 6, "File already exists");
        }
        else if (errno == EACCES)
        {
            send_error(sockfd, client_addr, client_len, 2, "Access violation");
        }
        else
        {
            send_error(sockfd, client_addr, client_len, 0, "Not defined");
        }
        return;
    }

    char buffer[BUFFER_SIZE];
    int block = 0;

    while (1)
    {
        // Enviar ACK
        memset(buffer, 0, BUFFER_SIZE);
        *(uint16_t *)buffer = htons(ACK);
        *(uint16_t *)(buffer + 2) = htons(block);

        printf("Enviando paquete ACK número: %d\n", block);
        sendto(sockfd, buffer, 4, 0, (struct sockaddr *)client_addr, client_len);

        // Recibir datos
        ssize_t n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)client_addr, &client_len);
        printf("Recibido paquete DATA número: %d\n", ntohs(*(uint16_t *)(buffer + 2)));
        if (n < 0)
        {
            send_error(sockfd, client_addr, client_len, 0, "Undefined error");
            close(file);
            return;
        }

        int opcode = ntohs(*(uint16_t *)buffer);
        int recv_block = ntohs(*(uint16_t *)(buffer + 2));

        if (opcode == DATA && recv_block == block + 1)
        {
            if (write(file, buffer + 4, n - 4) < 0)
            {
                if (errno == ENOSPC)
                {
                    send_error(sockfd, client_addr, client_len, 3, "Disk full or allocation exceeded");
                }
                else
                {
                    send_error(sockfd, client_addr, client_len, 0, "Not defined");
                }
                close(file);
                return;
            }
            block++;

            if (n < BUFFER_SIZE)
            {
                // Enviar ultimo ACK
                memset(buffer, 0, BUFFER_SIZE);
                *(uint16_t *)buffer = htons(ACK);
                *(uint16_t *)(buffer + 2) = htons(block);

                printf("Enviando paquete ACK número: %d\n", block);
                sendto(sockfd, buffer, 4, 0, (struct sockaddr *)client_addr, client_len);
                break; // Fin de archivo
            }
        }
        else
        {
            send_error(sockfd, client_addr, client_len, 4, "Illegal TFTP operation");
            close(file);
            return;
        }
    }

    close(file);
    printf("Fichero recibido.\n");
}

void handle_request(int sockfd, struct sockaddr_in *client_addr, socklen_t client_len)
{
    char buffer[BUFFER_SIZE];
    ssize_t n;

    n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)client_addr, &client_len);
    if (n < 0)
    {
        error_exit("recvfrom");
    }

    int opcode = ntohs(*(uint16_t *)buffer);
    char *filename = buffer + 2;

    if (opcode == RRQ)
    {
        printf("Solicitud de lectura recibida para el archivo: %s\n", filename);
        handle_rrq(sockfd, client_addr, client_len, filename);
    }
    else if (opcode == WRQ)
    {
        printf("Solicitud de escritura recibida para el archivo: %s\n", filename);
        handle_wrq(sockfd, client_addr, client_len, filename);
    }
    else
    {
        send_error(sockfd, client_addr, client_len, 4, "Illegal TFTP operation");
    }
}

int main()
{
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        error_exit("socket");
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        error_exit("bind");
    }

    while (1)
    {
        handle_request(sockfd, &client_addr, client_len);
    }

    close(sockfd);
    return 0;
}
