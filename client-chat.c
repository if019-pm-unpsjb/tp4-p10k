#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define DIRECTORIO_BASE "./ficherosTFTPcliente/"

int sock = 0;
char username[50];

void receive_file(char *remitente, char *file_name, int file_size)
{
  FILE *file = fopen(file_name, "wb");
  if (file == NULL)
  {
    perror("Error al abrir el archivo");
    return;
  }

  char buffer[BUFFER_SIZE];
  int bytes_received;
  int total_bytes_received = 0;

  printf("Recibiendo archivo de %s: %s (%d bytes)\n", remitente, file_name, file_size);
  while (total_bytes_received < file_size && (bytes_received = recv(sock, buffer, BUFFER_SIZE, 0)) > 0)
  {
    fwrite(buffer, 1, bytes_received, file);
    total_bytes_received += bytes_received;
  }

  fclose(file);
  if (total_bytes_received == file_size)
  {
    printf("Archivo %s recibido con éxito.\n", file_name);
  }
  else
  {
    printf("Error al recibir el archivo %s.\n", file_name);
  }
}
void *receive_messages(void *arg)
{
  char buffer[BUFFER_SIZE];
  int read_size;
  while ((read_size = recv(sock, buffer, BUFFER_SIZE, 0)) > 0)
  {
    buffer[read_size] = '\0';
    if (strncmp(buffer, "FILE", 4) == 0)
    {
      char remitente[50], file_name[50];
      int file_size;
      sscanf(buffer, "FILE %s %s %d", remitente, file_name, &file_size);
      receive_file(remitente, file_name, file_size);
    }
    else
    {
      printf("%s\n", buffer);
    }
  }
  return NULL;
}

void send_file(char *destinatario, char *file_name)
{
  char ruta_archivo[256];
  snprintf(ruta_archivo, sizeof(ruta_archivo), "%s%s", DIRECTORIO_BASE, file_name);
  FILE *file = fopen(ruta_archivo, "rb");
  if (file == NULL)
  {
    perror("Error al abrir el archivo");
    return;
  }

  fseek(file, 0, SEEK_END);
  int file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char buffer[BUFFER_SIZE];
  snprintf(buffer, sizeof(buffer), "FILE @%s %s %d\n", destinatario, file_name, file_size);
  send(sock, buffer, strlen(buffer), 0);

  while (!feof(file))
  {
    int bytes_read = fread(buffer, 1, BUFFER_SIZE, file);
    if (bytes_read > 0)
    {
      send(sock, buffer, bytes_read, 0);
    }
  }
  fclose(file);
  printf("Archivo enviado a %s\n", destinatario);
}

int main()
{
  struct sockaddr_in serv_addr;
  pthread_t tid;
  char buffer[BUFFER_SIZE - 1];

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    printf("\nError : Creación del socket\n");
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);

  if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
  {
    printf("\nDirección inválida/ Dirección no soportada\n");
    return -1;
  }

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    printf("\nConexión fallida\n");
    return -1;
  }

  printf("Ingrese su nombre de usuario: ");
  fgets(username, 50, stdin);
  username[strcspn(username, "\n")] = 0;

  sprintf(buffer, "%s\n", username);
  send(sock, buffer, strlen(buffer), 0);

  pthread_create(&tid, NULL, receive_messages, NULL);

  while (1)
  {
    fgets(buffer, BUFFER_SIZE - 1, stdin);
    buffer[strcspn(buffer, "\n")] = 0;
    if (strncmp(buffer, "FILE", 4) == 0)
    {
      char destinatario[50], file_name[50];
      sscanf(buffer, "FILE @%s %s", destinatario, file_name);
      send_file(destinatario, file_name);
    }
    else
    {
      char message[BUFFER_SIZE];
      sprintf(message, "%s\n", buffer);
      send(sock, message, strlen(message), 0);
    }
  }

  close(sock);
  return 0;
}
