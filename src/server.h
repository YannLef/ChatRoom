#ifndef SERVER_H
#define SERVER_H

#define RED   "\x1B[1;31m"
#define GRN   "\x1B[1;32m"
#define YEL   "\033[1;33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"

/**
 * Structure servant de maillon à la liste chainée qui nous sert à garder en mémoire les informations concernant les clients connectés au serveur
 * */
typedef struct ClientList {
    int data; // Le numéro de la socket
    struct ClientList* prev; // Le maillon précédent
    struct ClientList* link; // Le maillon suivant
    char ip[16]; // L'ip du client
    char name[MAX_LENGTH_NAME]; // Le nom du client (entré par l'utilisateur)
} ClientList;

ClientList* newNode(int sockfd, char* ip);
void catch_ctrl_c_and_exit(int sig);
void send_to_all_clients(ClientList *np, char* tmp_buffer);
void client_handler(void *p_client);
void addCharToString(char** string, char c);
void getStringUntilChar(char* input, char** output, char test, int* i);
void send_to_clients(ClientList *np, char tmp_buffer[], char*login);
void envoiListeClients(ClientList *np);
void send_credit_client_console(ClientList *np);
void print_credit_server_console();
void concateneHeure(char* send_buffer);

#endif
