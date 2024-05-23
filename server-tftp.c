#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define TAMANO_BUFFER 516
#define TAMANO_DATOS 512
#define DIRECTORIO_BASE "./ficherosTFTPserver/"

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

void enviar_error(int sockfd, struct sockaddr_in *addr_cliente, socklen_t len_cliente, int codigo_error, const char *msg_error)
{
    char buffer[TAMANO_BUFFER];
    memset(buffer, 0, TAMANO_BUFFER);

    *(uint16_t *)buffer = htons(ERROR);
    *(uint16_t *)(buffer + 2) = htons(codigo_error);
    strcpy(buffer + 4, msg_error);

    sendto(sockfd, buffer, 4 + strlen(msg_error) + 1, 0, (struct sockaddr *)addr_cliente, len_cliente);
    printf("Enviado paquete ERROR código: %d, mensaje: %s\n", codigo_error, msg_error);
}

void manejar_rrq(int sockfd, struct sockaddr_in *addr_cliente, socklen_t len_cliente, const char *nombre_archivo)
{
    char ruta_archivo[256];
    snprintf(ruta_archivo, sizeof(ruta_archivo), "%s%s", DIRECTORIO_BASE, nombre_archivo);

    int archivo = open(ruta_archivo, O_RDONLY);
    if (archivo < 0)
    {
        if (errno == ENOENT)
        {
            enviar_error(sockfd, addr_cliente, len_cliente, 1, "Archivo no encontrado");
        }
        else if (errno == EACCES)
        {
            enviar_error(sockfd, addr_cliente, len_cliente, 2, "Violación de acceso");
        }
        else
        {
            enviar_error(sockfd, addr_cliente, len_cliente, 0, "No definido");
        }
        return;
    }

    char buffer[TAMANO_BUFFER];
    char buffer_datos[TAMANO_DATOS];
    int bloque = 1;
    ssize_t bytes_leidos;

    while ((bytes_leidos = read(archivo, buffer_datos, TAMANO_DATOS)) > 0)
    {
        memset(buffer, 0, TAMANO_BUFFER);
        *(uint16_t *)buffer = htons(DATA);
        *(uint16_t *)(buffer + 2) = htons(bloque);
        memcpy(buffer + 4, buffer_datos, bytes_leidos);

        printf("Enviando paquete DATA número: %d\n", bloque);
        sendto(sockfd, buffer, 4 + bytes_leidos, 0, (struct sockaddr *)addr_cliente, len_cliente);

        // Esperar ACK
        recvfrom(sockfd, buffer, TAMANO_BUFFER, 0, (struct sockaddr *)addr_cliente, &len_cliente);

        if (ntohs(*(uint16_t *)buffer) != ACK || ntohs(*(uint16_t *)(buffer + 2)) != bloque)
        {
            enviar_error(sockfd, addr_cliente, len_cliente, 4, "Operación TFTP ilegal");
            close(archivo);
            return;
        }
        printf("Recibido paquete ACK número: %d\n", ntohs(*(uint16_t *)(buffer + 2)));

        bloque++;
    }

    close(archivo);
    printf("Envío concluido\n");
}

void manejar_wrq(int sockfd, struct sockaddr_in *addr_cliente, socklen_t len_cliente, const char *nombre_archivo)
{
    char ruta_archivo[256];
    snprintf(ruta_archivo, sizeof(ruta_archivo), "%s%s", DIRECTORIO_BASE, nombre_archivo);

    int archivo = open(ruta_archivo, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (archivo < 0)
    {
        if (errno == EEXIST)
        {
            enviar_error(sockfd, addr_cliente, len_cliente, 6, "El archivo ya existe");
        }
        else if (errno == EACCES)
        {
            enviar_error(sockfd, addr_cliente, len_cliente, 2, "Violación de acceso");
        }
        else
        {
            enviar_error(sockfd, addr_cliente, len_cliente, 0, "No definido");
        }
        return;
    }

    char buffer[TAMANO_BUFFER];
    int bloque = 0;
    while (1)
    {
        // Enviar ACK
        memset(buffer, 0, TAMANO_BUFFER);
        *(uint16_t *)buffer = htons(ACK);
        *(uint16_t *)(buffer + 2) = htons(bloque);

        printf("Enviando paquete ACK número: %d\n", bloque);
        sendto(sockfd, buffer, 4, 0, (struct sockaddr *)addr_cliente, len_cliente);

        // Recibir datos
        ssize_t n = recvfrom(sockfd, buffer, TAMANO_BUFFER, 0, (struct sockaddr *)addr_cliente, &len_cliente);

        if (n < 0)
        {
            enviar_error(sockfd, addr_cliente, len_cliente, 0, "No definido");
            close(archivo);
            return;
        }

        int opcode = ntohs(*(uint16_t *)buffer);
        int bloque_recibido = ntohs(*(uint16_t *)(buffer + 2));

        if (opcode == DATA && bloque_recibido == bloque + 1)
        {
            printf("Recibido paquete DATA número: %d\n", ntohs(*(uint16_t *)(buffer + 2)));
            if (write(archivo, buffer + 4, n - 4) < 0)
            {
                if (errno == ENOSPC)
                {
                    enviar_error(sockfd, addr_cliente, len_cliente, 3, "Disco lleno o asignación excedida");
                }
                else
                {
                    enviar_error(sockfd, addr_cliente, len_cliente, 0, "No definido");
                }
                close(archivo);
                return;
            }
            bloque++;

            if (n < TAMANO_BUFFER)
            {
                // Enviar último ACK
                memset(buffer, 0, TAMANO_BUFFER);
                *(uint16_t *)buffer = htons(ACK);
                *(uint16_t *)(buffer + 2) = htons(bloque);

                printf("Enviando paquete ACK número: %d\n", bloque);
                sendto(sockfd, buffer, 4, 0, (struct sockaddr *)addr_cliente, len_cliente);
                break; // Fin de archivo
            }
        }
        else
        {
            enviar_error(sockfd, addr_cliente, len_cliente, 4, "Operación TFTP ilegal");
            close(archivo);
            return;
        }
    }

    close(archivo);
    printf("Fichero recibido.\n");
}

void manejar_solicitud(int sockfd, struct sockaddr_in *addr_cliente, socklen_t len_cliente)
{
    char buffer[TAMANO_BUFFER];
    ssize_t n;

    n = recvfrom(sockfd, buffer, TAMANO_BUFFER, 0, (struct sockaddr *)addr_cliente, &len_cliente);
    if (n < 0)
    {
        salida_error("recvfrom");
    }

    int opcode = ntohs(*(uint16_t *)buffer);
    char *nombre_archivo = buffer + 2;

    if (opcode == RRQ)
    {
        printf("Solicitud de lectura recibida para el archivo: %s\n", nombre_archivo);
        manejar_rrq(sockfd, addr_cliente, len_cliente, nombre_archivo);
    }
    else if (opcode == WRQ)
    {
        printf("Solicitud de escritura recibida para el archivo: %s\n", nombre_archivo);
        manejar_wrq(sockfd, addr_cliente, len_cliente, nombre_archivo);
    }
    else
    {
        enviar_error(sockfd, addr_cliente, len_cliente, 4, "Operación TFTP ilegal");
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Uso: %s <DIRECCION_IP> <PUERTO>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *direccion_ip = argv[1];
    int puerto = atoi(argv[2]);
    if (puerto <= 0 || puerto > 65535)
    {
        fprintf(stderr, "Puerto inválido: %d\n", puerto);
        exit(EXIT_FAILURE);
    }

    int sockfd;
    struct sockaddr_in addr_servidor, addr_cliente;
    socklen_t len_cliente = sizeof(addr_cliente);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        salida_error("socket");
    }

    memset(&addr_servidor, 0, sizeof(addr_servidor));
    addr_servidor.sin_family = AF_INET;
    if (inet_pton(AF_INET, direccion_ip, &addr_servidor.sin_addr) <= 0)
    {
        fprintf(stderr, "Dirección IP inválida: %s\n", direccion_ip);
        exit(EXIT_FAILURE);
    }
    addr_servidor.sin_port = htons(puerto);

    if (bind(sockfd, (struct sockaddr *)&addr_servidor, sizeof(addr_servidor)) < 0)
    {
        salida_error("bind");
    }

    while (1)
    {
        printf("\nServidor TFTP escuchando en %s:%d\n\n", direccion_ip, puerto);
        manejar_solicitud(sockfd, &addr_cliente, len_cliente);
    }

    close(sockfd);
    return 0;
}
