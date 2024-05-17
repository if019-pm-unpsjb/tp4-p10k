#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define SERVER_PORT 69
#define BUFFER_SIZE 516
#define DATA_SIZE 512
#define BASE_DIR "./ficherosTFTPcliente/" // Cambia esto al directorio deseado

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

void send_rrq(int sockfd, struct sockaddr_in *server_addr, const char *filename)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    *(uint16_t *)buffer = htons(RRQ);
    strcpy(buffer + 2, filename);
    strcpy(buffer + 2 + strlen(filename) + 1, "octet");

    sendto(sockfd, buffer, 2 + strlen(filename) + 1 + strlen("octet") + 1, 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    printf("Enviando solicitud de lectura para el archivo: %s\n", filename);

    char filepath[BUFFER_SIZE];
    snprintf(filepath, BUFFER_SIZE, "%s%s", BASE_DIR, filename);

    FILE *file = fopen(filepath, "wb");
    if (!file)
    {
        error_exit("fopen");
    }

    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t n;
    int block = 1;

    while (1)
    {
        n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
        int opcode = ntohs(*(uint16_t *)buffer);
        int recv_block = ntohs(*(uint16_t *)(buffer + 2));

        if (opcode == DATA && recv_block == block)
        {
            fwrite(buffer + 4, 1, n - 4, file);
            printf("Recibido paquete DATA número: %d\n", recv_block);

            memset(buffer, 0, BUFFER_SIZE);
            *(uint16_t *)buffer = htons(ACK);
            *(uint16_t *)(buffer + 2) = htons(block);

            sendto(sockfd, buffer, 4, 0, (struct sockaddr *)&from_addr, from_len);
            printf("Enviando paquete ACK número: %d\n", block);

            if (n < BUFFER_SIZE)
                break; // Fin de archivo
            block++;
        }
        else if (opcode == ERROR)
        {
            fprintf(stderr, "Error recibido: %s\n", buffer + 4);
            fclose(file);
            return;
        }
        else
        {
            fprintf(stderr, "Error: operación TFTP ilegal\n");
            fclose(file);
            return;
        }
    }

    fclose(file);
    printf("Fichero recibido.\n");
}

void send_wrq(int sockfd, struct sockaddr_in *server_addr, const char *filename)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    *(uint16_t *)buffer = htons(WRQ);
    strcpy(buffer + 2, filename);
    strcpy(buffer + 2 + strlen(filename) + 1, "octet");

    sendto(sockfd, buffer, 2 + strlen(filename) + 1 + strlen("octet") + 1, 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    printf("Enviando solicitud de escritura para el archivo: %s\n", filename);

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s%s", BASE_DIR, filename);
    printf("ruta completa: %s\n", filepath);

    FILE *file = fopen(filepath, "rb");
    if (!file)
    {
        error_exit("fopen");
    }

    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t n;
    int block = 0;

    while (1)
    {
        // Esperar ACK
        n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
        printf("Recibido paquete ACK número: %d\n", ntohs(*(uint16_t *)(buffer + 2)));
        if (n < 0)
        {
            error_exit("recvfrom");
        }

        int opcode = ntohs(*(uint16_t *)buffer);
        int recv_block = ntohs(*(uint16_t *)(buffer + 2));

        if (opcode == ACK && recv_block == block)
        {
            block++;

            memset(buffer, 0, BUFFER_SIZE);
            *(uint16_t *)buffer = htons(DATA);
            *(uint16_t *)(buffer + 2) = htons(block);

            n = fread(buffer + 4, 1, DATA_SIZE, file);
            printf("Enviando paquete DATA número: %d\n", block);
            sendto(sockfd, buffer, 4 + n, 0, (struct sockaddr *)&from_addr, from_len);

            if (n < DATA_SIZE)
            {
                // Esperar ultimo ACK
                n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
                printf("Recibido paquete ACK número: %d\n", ntohs(*(uint16_t *)(buffer + 2)));
                break; // Fin de archivo
            }
        }
        else if (opcode == ERROR)
        {
            fprintf(stderr, "Error recibido: %s\n", buffer + 4);
            fclose(file);
            return;
        }
        else
        {
            fprintf(stderr, "Error: operación TFTP ilegal\n");
            fclose(file);
            return;
        }
    }

    fclose(file);
    printf("Envio concluido\n");
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "Uso: %s <IP servidor> <nombre de archivo> <r|w>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    const char *filename = argv[2];
    char mode = argv[3][0];

    int sockfd;
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        error_exit("socket");
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0)
    {
        error_exit("inet_pton");
    }

    if (mode == 'r')
    {
        send_rrq(sockfd, &server_addr, filename);
    }
    else if (mode == 'w')
    {
        send_wrq(sockfd, &server_addr, filename);
    }
    else
    {
        fprintf(stderr, "Modo no válido. Use 'r' para lectura y 'w' para escritura.\n");
        exit(EXIT_FAILURE);
    }

    close(sockfd);
    return 0;
}
