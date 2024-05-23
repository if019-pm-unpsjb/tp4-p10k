#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define TAMANO_BUFFER 516
#define TAMANO_DATOS 512
#define DIRECTORIO_BASE "./ficherosTFTPcliente/"

enum
{
    RRQ = 1,
    WRQ,
    DATA,
    ACK,
    ERROR
};

void salida_error(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

void enviar_rrq(int sockfd, struct sockaddr_in *addr_servidor, const char *nombre_archivo)
{
    char buffer[TAMANO_BUFFER];
    memset(buffer, 0, TAMANO_BUFFER);

    *(uint16_t *)buffer = htons(RRQ);
    strcpy(buffer + 2, nombre_archivo);
    strcpy(buffer + 2 + strlen(nombre_archivo) + 1, "octet");

    sendto(sockfd, buffer, 2 + strlen(nombre_archivo) + 1 + strlen("octet") + 1, 0, (struct sockaddr *)addr_servidor, sizeof(*addr_servidor));
    printf("Enviando solicitud de lectura para el archivo: %s\n", nombre_archivo);

    char ruta_archivo[256];
    snprintf(ruta_archivo, sizeof(ruta_archivo), "%s%s", DIRECTORIO_BASE, nombre_archivo);

    FILE *archivo = fopen(ruta_archivo, "wb");
    if (!archivo)
    {
        salida_error("fopen");
    }

    struct sockaddr_in addr_from;
    socklen_t len_from = sizeof(addr_from);
    ssize_t n;
    int bloque = 1;

    while (1)
    {
        n = recvfrom(sockfd, buffer, TAMANO_BUFFER, 0, (struct sockaddr *)&addr_from, &len_from);
        if (n < 0)
        {
            salida_error("recvfrom");
        }
        int opcode = ntohs(*(uint16_t *)buffer);
        int bloque_recibido = ntohs(*(uint16_t *)(buffer + 2));

        if (opcode == DATA && bloque_recibido == bloque)
        {
            printf("Recibido paquete DATA número: %d\n", bloque_recibido);
            fwrite(buffer + 4, 1, n - 4, archivo);

            memset(buffer, 0, TAMANO_BUFFER);
            *(uint16_t *)buffer = htons(ACK);
            *(uint16_t *)(buffer + 2) = htons(bloque);

            printf("Enviando paquete ACK número: %d\n", bloque);
            sendto(sockfd, buffer, 4, 0, (struct sockaddr *)&addr_from, len_from);

            if (n < TAMANO_BUFFER)
                break; // Fin de archivo
            bloque++;
        }
        else if (opcode == ERROR)
        {
            fprintf(stderr, "Error recibido: %s\n", buffer + 4);
            fclose(archivo);
            return;
        }
        else
        {
            fprintf(stderr, "Error: operación TFTP ilegal\n");
            fclose(archivo);
            return;
        }
    }

    fclose(archivo);
    printf("Fichero recibido.\n");
}

void enviar_wrq(int sockfd, struct sockaddr_in *addr_servidor, const char *nombre_archivo)
{
    char buffer[TAMANO_BUFFER];
    memset(buffer, 0, TAMANO_BUFFER);

    *(uint16_t *)buffer = htons(WRQ);
    strcpy(buffer + 2, nombre_archivo);
    strcpy(buffer + 2 + strlen(nombre_archivo) + 1, "octet");

    char ruta_archivo[256];
    snprintf(ruta_archivo, sizeof(ruta_archivo), "%s%s", DIRECTORIO_BASE, nombre_archivo);

    FILE *archivo = fopen(ruta_archivo, "rb");
    if (!archivo)
    {
        salida_error("fopen");
    }

    sendto(sockfd, buffer, 2 + strlen(nombre_archivo) + 1 + strlen("octet") + 1, 0, (struct sockaddr *)addr_servidor, sizeof(*addr_servidor));
    printf("Enviando solicitud de escritura para el archivo: %s\n", nombre_archivo);

    struct sockaddr_in addr_from;
    socklen_t len_from = sizeof(addr_from);
    ssize_t n;
    int bloque = 0;

    while (1)
    {
        // Esperar ACK
        n = recvfrom(sockfd, buffer, TAMANO_BUFFER, 0, (struct sockaddr *)&addr_from, &len_from);
        if (n < 0)
        {
            salida_error("recvfrom");
        }

        int opcode = ntohs(*(uint16_t *)buffer);
        int bloque_recibido = ntohs(*(uint16_t *)(buffer + 2));

        if (opcode == ACK && bloque_recibido == bloque)
        {
            printf("Recibido paquete ACK número: %d\n", ntohs(*(uint16_t *)(buffer + 2)));
            bloque++;

            memset(buffer, 0, TAMANO_BUFFER);
            *(uint16_t *)buffer = htons(DATA);
            *(uint16_t *)(buffer + 2) = htons(bloque);

            n = fread(buffer + 4, 1, TAMANO_DATOS, archivo);
            printf("Enviando paquete DATA número: %d\n", bloque);
            sendto(sockfd, buffer, 4 + n, 0, (struct sockaddr *)&addr_from, len_from);

            if (n < TAMANO_DATOS)
            {
                // Esperar último ACK
                n = recvfrom(sockfd, buffer, TAMANO_BUFFER, 0, (struct sockaddr *)&addr_from, &len_from);
                printf("Recibido paquete ACK número: %d\n", ntohs(*(uint16_t *)(buffer + 2)));
                break; // Fin de archivo
            }
        }
        else if (opcode == ERROR)
        {
            fprintf(stderr, "Error recibido: %s\n", buffer + 4);
            fclose(archivo);
            return;
        }
        else
        {
            fprintf(stderr, "Error: operación TFTP ilegal\n");
            fclose(archivo);
            return;
        }
    }

    fclose(archivo);
    printf("Envío concluido\n");
}

int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        fprintf(stderr, "Uso: %s <IP servidor> <Puerto> <nombre de archivo> <r|w>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *ip_servidor = argv[1];
    int puerto = atoi(argv[2]);
    if (puerto <= 0 || puerto > 65535)
    {
        fprintf(stderr, "Puerto inválido: %d\n", puerto);
        exit(EXIT_FAILURE);
    }
    const char *nombre_archivo = argv[3];
    char modo = argv[4][0];

    int sockfd;
    struct sockaddr_in addr_servidor;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        salida_error("socket");
    }

    memset(&addr_servidor, 0, sizeof(addr_servidor));
    addr_servidor.sin_family = AF_INET;
    addr_servidor.sin_port = htons(puerto);
    if (inet_pton(AF_INET, ip_servidor, &addr_servidor.sin_addr) <= 0)
    {
        fprintf(stderr, "Dirección IP inválida: %s\n", ip_servidor);
        exit(EXIT_FAILURE);
    }

    if (modo == 'r')
    {
        enviar_rrq(sockfd, &addr_servidor, nombre_archivo);
    }
    else if (modo == 'w')
    {
        enviar_wrq(sockfd, &addr_servidor, nombre_archivo);
    }
    else
    {
        fprintf(stderr, "Modo no válido. Use 'r' para lectura y 'w' para escritura.\n");
        exit(EXIT_FAILURE);
    }

    close(sockfd);
    return 0;
}
