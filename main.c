#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<pthread.h>
#include<signal.h>
#include<ifaddrs.h>
#include<netinet/in.h>
#include<netdb.h>

// Inicializa o socket do servidor globalmente (para tratar interrupção)
int server_fd;

const char* get_content_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext != NULL) {
        if (strcmp(ext, ".html") == 0) return "text/html";
        if (strcmp(ext, ".jpg") == 0) return "image/jpeg";
    }
    return NULL;
}

void error(const char *msg){
    perror(msg);
    exit(1);
}

void *handle_client(void *arg){
    int client_fd = *((int*)arg);
    char buffer[1024];
    char method[16], path[256], protocol[16];
    char response_header[256];

    // Lê a requisição
    read(client_fd, buffer, sizeof(buffer));
    sscanf(buffer, "%s %s %s", method, path, protocol);
    printf("Received request:\n%s\n", buffer);

    // Remove o '/' inicial do path
    if (path[0] == '/') memmove(path, path + 1, strlen(path));
    if (strlen(path) == 0) strcpy(path, "index.html");

    // Abre o arquivo (HTML e Imagem)
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror("Failed to open file.");
        const char *not_found_response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n<html><body><h1>404 Not Found</h1></body></html>";
        write(client_fd, not_found_response, strlen(not_found_response));
        close(client_fd);
        free(arg);
        pthread_exit(NULL);
    }

    // Detecta o tipo de conteúdo
    const char *content_type = get_content_type(path);

    // Envia o cabeçalho HTTP
    sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n", content_type);
    write(client_fd, response_header, strlen(response_header));

    // Lê e envia o conteúdo do arquivo
    char file_buffer[1024];
    size_t n;
    while ((n = fread(file_buffer, 1, sizeof(file_buffer), file)) > 0) {
        write(client_fd, file_buffer, n);
    }

    fclose(file);
    close(client_fd);
    free(arg);
    pthread_exit(NULL);

}

// Função para lidar com o sinal SIGINT (Ctrl + C)
void handle_sigint(int sig){
    printf("\nReceived SIGINT. Closing server socket...\n");
    close(server_fd);
    exit(0);
}

void print_all_interface_ips() {
    struct ifaddrs *ifaddr, *ifa;
    int family;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on the following interfaces:\n");
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        // Verifica se é uma interface IPv4
        if (family == AF_INET) {
            if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                            host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == 0) {
                printf("Interface: %s\tIP: %s\n", ifa->ifa_name, host);
            }
        }
    }

    freeifaddrs(ifaddr);
}

int main(){
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int addrlen = sizeof(server_addr);

    signal(SIGINT, handle_sigint);

    // Criar o socket do servidor
    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        error("Socket failed to initialize.");
    }

    // Permite "bindar" a porta logo após interromper
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        error("setsockopt failed");
    }

    // Configuração do endereço do servidor
    server_addr.sin_family = AF_INET; //Define o protocolo ipv4
    server_addr.sin_addr.s_addr = INADDR_ANY; //Aceita conexão em qualquer interface de rede
    server_addr.sin_port = htons(8080); //Define a porta

    print_all_interface_ips();

    // Bind do socket à porta
    if(bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
        error("Failed to bind port to socket.");
    }
    printf("Server listening on port 8080.\n");

    // Inicializa o host servidor para escutar conexões
    if(listen(server_fd, 20) < 0){
        error("Failed to start listening.");
    }

    while(1){
        // Inicializa o socket do cliente
        int *client_fd = malloc(sizeof(int));

        // Aceita conexão do cliente
        if((*client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len)) < 0){
            perror("Failed to accept client.");
            free(client_fd);
            continue;
        }

        // Cria uma nova thread para lidar com a requisição do cliente
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, (void *)client_fd);
        pthread_detach(thread_id);
    }

    return 0;
}
