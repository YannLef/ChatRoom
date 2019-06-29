#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <pthread.h> // Pour pouvoir utiliser les threads (pour le multi-clients)
#include "const.h" // Pour utiliser les constantes définies pour le projet
#include "client.h" // Contenu spécifique à client.c

// Variables globales
volatile sig_atomic_t flag = 0;
int sockfd = 0;
char nickname[MAX_LENGTH_NAME] = {};

void catch_ctrl_c_and_exit(int sig) {
    flag = 1;
}

/**
 * Fonction permettant de recevoir des messages en continu (ouvert sur un thread)
 * Affiche le message reçu dans le terminal
 * */
void recv_msg_handler() {
    char receiveMessage[LENGTH_SEND] = {}; // On initialise la variable qui stockera le message
    while (1) { // Boucle infini pour recevoir en continu
        int receive = recv(sockfd, receiveMessage, LENGTH_SEND, 0); // FONCTION BLOCANTE> On récupère le message reçu -> renvois -1 si erreur, 0 si la connexion est coupée, n la taille du message sinon
        if (receive > 0) { // Si receive > 0 -> un message a été reçu
            printf("\r%s\n", receiveMessage); // On affiche le message
            str_overwrite_stdout(); // Après avoir afficher le message on prépare la nouvelle ligne (prêt pour l'écriture d'une commande)
        } else if (receive == 0) { // si receive = 0, la connexion a été coupé -> on quitte
			flag=1;
            break;
        } else { // Si receive = -1, il y a eu un problème dans la reception du message, on ne fais rien.
			
        }
    }
}

/**
 * Fonction permettant d'envoyer des messages en continu (ouvert sur un thread)
 * */
void send_msg_handler() { 
    char message[LENGTH_MSG] = {}; // On initialise la variable qui stockera le message
    while (1) { // Boucle infini pour pouvoir envoyer en continu
        str_overwrite_stdout(); // On prépare la ligne du terminal pour l'écriture
        while (fgets(message, LENGTH_MSG, stdin) != NULL) { // Tant que la commande saisie n'est pas vide
            str_trim_lf(message, LENGTH_MSG); // On ajout le caractère de fin de chaine au bon endroit
            if (strlen(message) == 0) { // Si le message est vide, on reprépare une ligne pour écrire
                str_overwrite_stdout();
            } else { // Sinon on sort de la boucle, le message est prêt pour l'envoi
                break;
            }
        }
        send(sockfd, message, LENGTH_MSG, 0); // On envoi le message
    }
}

/**
 * Fonction permettant de définir ou se termine la commande saisie à partir du tableau de char renvoyé par fgets
 * */
void str_trim_lf (char* arr, int length) {
    for (int i = 0; i < length; i++) { // On parcours le tableau de char en entier (ou pas selon si on break avant la fin)
        if (arr[i] == '\n') { // Dès que l'on croise un \n, la commande se termine
            arr[i] = '\0'; // On indique la fin de la chaine grâce à \0
            break; // On quitte la boucle for, inutile de continuer
        }
    }
}

/**
 * Fonction permettant de préparer l'écriture d'une nouvelle commande au terminal client
 * */
void str_overwrite_stdout() {
    printf("\r%s", "<Vous> "); // On affiche le symbole indiquant qu'un message est prêt à être entré
    fflush(stdout); // On vide la mémoire tampon liée à l'affichage afin de pouvoir accueillir du nouveau texte sans déchets résiduels
}


int main(void)
{
    signal(SIGINT, catch_ctrl_c_and_exit);


    while(1){
	    // La première chose à faire en arrivant est de se connecter
		printf("Entrez votre pseudo: ");
		if (fgets(nickname, MAX_LENGTH_NAME, stdin) != NULL) {     // On récupère la saisi du client
			str_trim_lf(nickname, MAX_LENGTH_NAME);
		}
		if (strlen(nickname) < MIN_LENGTH_NAME || strlen(nickname) >= MAX_LENGTH_NAME-1) { // On vérifie que le nom saisi possède entre la taille min et la taille max définie dans const.h
			printf("\nVotre nom doit comporter entre %d et %d caractères.\n",MIN_LENGTH_NAME,MAX_LENGTH_NAME);
		}else{
			int err = 0;
			for (int i = 0; nickname[i] != '\0'; ++i){ // on parcours le pseudo
				if (ispunct(nickname[i])){ // On verifie qu'il n'y a pas de ponctuation
					err = 1;
				}
			}
			if(err == 1){
				printf("Votre nom ne doit pas comporter de ponctuation.\n");
			}else{
				break; // On quitte la boucle infini une fois que le pseudo est validé
			}
		}
	}	

    // On crée la socket
    sockfd = socket(AF_INET , SOCK_STREAM , 0);
    if (sockfd == -1) {
        printf("Erreur lors de la création de la socket.");
        exit(EXIT_FAILURE);
    }

    // Informations de la socket
    struct sockaddr_in server_info, client_info;
    int s_addrlen = sizeof(server_info);
    int c_addrlen = sizeof(client_info);
    memset(&server_info, 0, s_addrlen);
    memset(&client_info, 0, c_addrlen);
    server_info.sin_family = PF_INET;
    server_info.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_info.sin_port = htons(PORT);

    // Connexion au serveur
    int err = connect(sockfd, (struct sockaddr *)&server_info, s_addrlen);
    if (err == -1) {
        printf("Echec de la connexion au serveur !\n");
        exit(EXIT_FAILURE);
    }
    
    // Names
    getsockname(sockfd, (struct sockaddr*) &client_info, (socklen_t*) &c_addrlen); // On récupère le nom du socket client
    getpeername(sockfd, (struct sockaddr*) &server_info, (socklen_t*) &s_addrlen); // On récupère le nom du serveur

    send(sockfd, nickname, MAX_LENGTH_NAME, 0); // On envoi au serveur le nom entré par le client

	// On crée un thread pour s'occuper de l'envoi des messages en continu (en l'isolant sur un thread)
    pthread_t send_msg_thread;
    if (pthread_create(&send_msg_thread, NULL, (void *) send_msg_handler, NULL) != 0) {
        printf ("Erreur lors de la création du thread (envoi des messages) !\n");
        exit(EXIT_FAILURE);
    }

	// On crée un thread pour s'occuper de la réception des messages en continu (en l'isolant sur un thread) 
    pthread_t recv_msg_thread;
    if (pthread_create(&recv_msg_thread, NULL, (void *) recv_msg_handler, NULL) != 0) {
        printf ("Erreur lors de la création du thread (réception des messages) !\n");
        exit(EXIT_FAILURE);
    }
	
	// On boucle jusqu'à ce que flag passe à 1 == le client a fait ctrl+c et quitte le serveur
    while (1) {
        if(flag) {
            printf("\rVous avez quitté le serveur\n"); // On confirme à l'utilisateur qu'il a bien été déconnecté
            break;
        }
    }

    close(sockfd);
    return 0;
}
