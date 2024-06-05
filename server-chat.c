#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 1024

typedef struct
{
  int socket;
  char username[50];
} Client;

Client clients[10];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void send_message(char *message, int sender_socket)
{
  char destinatario[50];
  char msg[BUFFER_SIZE - sizeof(destinatario)];
  char remitente[50] = "";

  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < client_count; i++)
  {
    if (clients[i].socket == sender_socket)
    {
      strcpy(remitente, clients[i].username);
      break;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  if (sscanf(message, "@%s %[^\n]", destinatario, msg) != 2)
  {
    char msgError[BUFFER_SIZE];
    snprintf(msgError, sizeof(msgError), "mensaje no enviado revisar formato");
    send(sender_socket, msgError, strlen(msgError), 0);
    return;
  }

  char formatted_message[BUFFER_SIZE];
  snprintf(formatted_message, sizeof(formatted_message), "%s:%s", remitente, msg);

  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < client_count; i++)
  {
    if (strcmp(clients[i].username, destinatario) == 0)
    {
      send(clients[i].socket, formatted_message, strlen(formatted_message), 0);
      break;
    }
  }
  pthread_mutex_unlock(&clients_mutex);
}

void handle_file_transfer(char *message, int sender_socket)
{
  char destinatario[50], file_name[50];
  int file_size;
  char remitente[50] = "";

  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < client_count; i++)
  {
    if (clients[i].socket == sender_socket)
    {
      strcpy(remitente, clients[i].username);
      break;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  sscanf(message, "FILE @%s %s %d", destinatario, file_name, &file_size);

  char init_message[BUFFER_SIZE];
  snprintf(init_message, sizeof(init_message), "FILE %s %s %d", remitente, file_name, file_size);

  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < client_count; i++)
  {
    if (strcmp(clients[i].username, destinatario) == 0)
    {
      send(clients[i].socket, init_message, strlen(init_message), 0);

      char buffer[BUFFER_SIZE];
      int bytes_read;
      while (file_size > 0 && (bytes_read = recv(sender_socket, buffer, BUFFER_SIZE, 0)) > 0)
      {
        send(clients[i].socket, buffer, bytes_read, 0);
        file_size -= bytes_read;
      }
      break;
    }
  }
  pthread_mutex_unlock(&clients_mutex);
}

void list_clients(int client_socket)
{
  pthread_mutex_lock(&clients_mutex);
  char list_message[BUFFER_SIZE];
  snprintf(list_message, sizeof(list_message), "Usuarios conectados (%d):\n", client_count);
  for (int i = 0; i < client_count; i++)
  {
    strcat(list_message, clients[i].username);
    strcat(list_message, "\n");
  }
  pthread_mutex_unlock(&clients_mutex);
  send(client_socket, list_message, strlen(list_message), 0);
}

void *handle_client(void *arg)
{
  int client_socket = *((int *)arg);
  char buffer[BUFFER_SIZE];
  char username[50];
  int read_size;

  if ((read_size = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0)
  {
    buffer[read_size] = '\0';
    strcpy(username, buffer);
    username[strcspn(username, "\n")] = 0;

    pthread_mutex_lock(&clients_mutex);
    clients[client_count].socket = client_socket;
    strcpy(clients[client_count].username, username);
    client_count++;
    pthread_mutex_unlock(&clients_mutex);

    printf("Usuario conectado: %s\n", username);

    char auth_message[BUFFER_SIZE];
    sprintf(auth_message, "OK @%s\nPara enviar un mensaje: '@nombre_usuario_destinatorio mensaje'\nPara enviar un archivo: 'FILE @nombre_usuario_destinatorio nombre_archivo'\nPara ver los usuarios conectados: 'LIST'", username);
    send(client_socket, auth_message, strlen(auth_message), 0);
  }

  while ((read_size = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0)
  {
    buffer[read_size] = '\0';
    if (strncmp(buffer, "FILE", 4) == 0)
    {
      handle_file_transfer(buffer, client_socket);
    }
    else if (strcmp(buffer, "LIST") == 0)
    {
      list_clients(client_socket);
    }
    else
    {
      send_message(buffer, client_socket);
    }
  }

  close(client_socket);

  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < client_count; i++)
  {
    if (clients[i].socket == client_socket)
    {
      for (int j = i; j < client_count - 1; j++)
      {
        clients[j] = clients[j + 1];
      }
      client_count--;
      break;
    }
  }
  printf("Usuario desconectado: %s\n", username);
  pthread_mutex_unlock(&clients_mutex);

  pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
  int server_socket, client_socket;
  struct sockaddr_in server_addr, client_addr;
  socklen_t client_addr_size = sizeof(client_addr);
  pthread_t tid;

  if (argc != 3)
  {
    fprintf(stderr, "Uso: %s <dirección IP> <puerto>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  char *ip_address = argv[1];
  int port = atoi(argv[2]);

  if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0)
  {
    perror("Socket failed");
    exit(EXIT_FAILURE);
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr(ip_address);
  server_addr.sin_port = htons(port);

  if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
  {
    perror("Bind failed");
    close(server_socket);
    exit(EXIT_FAILURE);
  }

  if (listen(server_socket, 3) < 0)
  {
    perror("Listen failed");
    close(server_socket);
    exit(EXIT_FAILURE);
  }

  printf("Servidor escuchando en %s:%d\n", ip_address, port);

  while ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_size)) >= 0)
  {
    pthread_create(&tid, NULL, handle_client, (void *)&client_socket);
    pthread_detach(tid);
  }

  close(server_socket);
  return 0;
}
