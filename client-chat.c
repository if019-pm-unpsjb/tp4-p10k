#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int sock = 0;
char username[50];

void *receive_messages(void *arg)
{
  char buffer[BUFFER_SIZE];
  int read_size;
  while ((read_size = recv(sock, buffer, BUFFER_SIZE, 0)) > 0)
  {
    buffer[read_size] = '\0';
    printf("%s\n", buffer);
  }
  return NULL;
}

int main()
{
  struct sockaddr_in serv_addr;
  pthread_t tid;
  char buffer[BUFFER_SIZE - 1];

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    printf("\n Error : Socket creation error \n");
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);

  if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
  {
    printf("\nInvalid address/ Address not supported \n");
    return -1;
  }

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    printf("\nConnection Failed \n");
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
      // Manejo del envío de archivos
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
