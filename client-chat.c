#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#define BUFFER_SIZE 1024
#define DIRECTORIO_BASE_ENVIAR "./ficherosChatEnviar/"
#define DIRECTORIO_BASE_RECIBIR "./ficherosChatRecibir/"

int sock = 0;
char username[50];
pthread_mutex_t ack_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ack_cond = PTHREAD_COND_INITIALIZER;
int ack_received = 0;

void receive_file(char *remitente, char *file_name, int file_size)
{
  char ruta_archivo[256];
  snprintf(ruta_archivo, sizeof(ruta_archivo), "%s%s", DIRECTORIO_BASE_RECIBIR, file_name);

  FILE *file = fopen(ruta_archivo, "wb");
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

    // Enviar ACK por cada bloque recibido
    send(sock, "ACK", 3, 0);
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
    else if (strncmp(buffer, "ACK", 3) == 0)
    {
      pthread_mutex_lock(&ack_mutex);
      ack_received = 1;
      pthread_cond_signal(&ack_cond);
      pthread_mutex_unlock(&ack_mutex);
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
  snprintf(ruta_archivo, sizeof(ruta_archivo), "%s%s", DIRECTORIO_BASE_ENVIAR, file_name);
  FILE *file = fopen(ruta_archivo, "rb");
  if (file == NULL)
  {
    printf("Error al abrir el archivo revisar formato\n");
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
      // Esperar a recibir el ACK antes de continuar
      pthread_mutex_lock(&ack_mutex);
      while (!ack_received)
      {
        pthread_cond_wait(&ack_cond, &ack_mutex);
      }
      ack_received = 0;
      pthread_mutex_unlock(&ack_mutex);
    }
  }
  fclose(file);
  printf("Archivo enviado a %s\n", destinatario);
}

int main(int argc, char *argv[])
{
  struct sockaddr_in serv_addr;
  pthread_t tid;
  char buffer[BUFFER_SIZE];

  if (argc != 3)
  {
    fprintf(stderr, "Uso: %s <dirección IP server> <puerto>\n", argv[0]);
    return -1;
  }

  char *ip_address = argv[1];
  int port = atoi(argv[2]);

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    printf("Error : Creación del socket\n");
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);

  if (inet_pton(AF_INET, ip_address, &serv_addr.sin_addr) <= 0)
  {
    printf("Dirección inválida\n");
    return -1;
  }

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    printf("Conexión fallida\n");
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
    fgets(buffer, BUFFER_SIZE, stdin);
    buffer[strcspn(buffer, "\n")] = 0;
    if (strncmp(buffer, "FILE", 4) == 0)
    {
      char destinatario[50], file_name[50];
      sscanf(buffer, "FILE @%s %s", destinatario, file_name);
      send_file(destinatario, file_name);
    }
    else
    {
      send(sock, buffer, strlen(buffer), 0);
    }
  }

  close(sock);
  return 0;
}
