#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h> // Pour pouvoir utiliser les threads (pour le multi-clients)
#include "const.h" // Pour utiliser les constantes définies pour le projet
#include "server.h" // Contenu spécifique à server.c

// Variables globales
int server_sockfd = 0, client_sockfd = 0; 
ClientList *root, *now;

/**
 * Fonction permettant d'allouer et d'initialiser un maillon quelconque de la liste chainée des clients.
 * Se veut assez généraliste (prev et link à NULL) car est aussi utilisé pour initialiser la tête de la liste chainée
 * */
ClientList* newNode(int sockfd, char* ip) {
    ClientList *np = (ClientList *)malloc( sizeof(ClientList) ); // On alloue dynamiquement la mémoire permettant de stocker le maillon
    np->data = sockfd; // Le numéro de la socket
    // On initialise les pointeurs servant à intégrer le maillon dans la liste chainée
    np->prev = NULL;
    np->link = NULL;
    strcpy(np->ip, ip); // On copie l'adresse IPV4 récupérée précédemment (16 bits suffisent normalement)
    strcpy(np->name, "NULL"); // On initialise le nom à NULL (pas encore connu au moment de la création du maillon)
    return np; // on renvoit la structure créee et initialisée, prête à l'usage
}

/**
 * Fonction se lancer au moment ou un CTRL+C est effectué
 * Permet de fermer toutes les sockets et de libérer les maillons de la liste chainée
 * */
void catch_ctrl_c_and_exit(int sig) {
    ClientList *tmp;
    send_to_all_clients(root, "\nLe serveur a fermé");
    while (root != NULL) { // Tant que l'élément root n'est pas NULL <=> tant qu'il y a des maillons dans la liste chainée
        printf(RED "\nFermeture de la socket %d" RESET, root->data); // On annonce la fermeture de la socket dans les logs
        close(root->data); // On ferme la socket
        tmp = root; // On stock route dans un maillon temporaire avant de le modifier
        root = root->link; // On modifie root pour supprimer le reste de la liste chainée
        free(tmp); // On libère le maillon qui vient dont la socket vient d'être fermée
    }
    // On ferme les dernières sockets ouvertes
    close(server_sockfd);
    close(client_sockfd);
    
    printf(RED "\nLe serveur s'est éteint\n" RESET); 
    exit(EXIT_SUCCESS);
}

/**
 * Fonction permettant d'envoyer un message à tous les clients excepté celui à l'origine du message 
 * */
void send_to_all_clients(ClientList *np, char* tmp_buffer) {
    ClientList *tmp = root->link;
    while (tmp != NULL) { // Tant que tmp != NULL <=> Tant qu'il y a des maillons dans la liste chainée
        if (np->data != tmp->data) { // On envoi le message à tous les clients sauf celui qui a envoyé le message
            printf("Envoyé à <%s | %s | socket %d> : %s\n", tmp->name, tmp->ip, tmp->data, tmp_buffer);
            send(tmp->data, tmp_buffer, LENGTH_SEND, 0); // On envoi le message au client
        }
        tmp = tmp->link; // On passe au client suivant en suivant le maillage de la liste chainée. Si il n'y a pas de suivant, tmp->link vaudra NULL
    }
}

/**
 * Fonction s'occuppant d'interagir avec un unique client.
 * Est isolé sur un thread ce qui nous permet de gérer plusieurs clients à la fois (un par thread)
 * */
void client_handler(void *p_client) {
    int leave_flag = 0; // Fonction permettant la gestion d'erreur, passe à 1 en cas d'erreur dans le processus
    char nickname[MAX_LENGTH_NAME] = {}; // On défini la variable qui servira à stocker le nom du client
    char recv_buffer[LENGTH_MSG] = {}; // On défini la variable qui servira à stocker le message reçu
    char send_buffer[LENGTH_SEND] = {}; // On défini la variable qui servira à stocker le message envoyé
    ClientList *np = (ClientList *)p_client; // On défini une structure pour le client

    // Concernant le nom du client
    if (recv(np->data, nickname, MAX_LENGTH_NAME, 0) <= 0 || strlen(nickname) < MIN_LENGTH_NAME || strlen(nickname) >= MAX_LENGTH_NAME-1 || strlen(nickname) <= MIN_LENGTH_NAME-1) { // FONCTION BLOCANTE> On récupère le message reçu -> renvois -1 si erreur, 0 si la connexion est coupée, n la taille du message sinon
        // Si le retour est < 0, il y a une erreur
        printf("Une erreur est survenue ! \n");
        leave_flag = 1; // On passe le flag a 1 pour gérer l'erreur
    } else { // Si il n'y a pas eu d'erreurs, on peut continuer
        strncpy(np->name, nickname, MAX_LENGTH_NAME); // On enregistre le nom du client dans la structure
        printf(RED "<%s | %s | socket %d> vient de se connecter.\n" RESET, np->name, np->ip, np->data);
        // On envoi un message à tous les clients pour les informer de la connexion
        sprintf(send_buffer, "%s vient de se connecter.", np->name);
        send_to_all_clients(np, send_buffer);
    }
    
    // On envoi le message de bienvenue maintenant qu'on a récupérer le nom du client
	sprintf(send_buffer, "Bienvenue %s sur le serveur !", np->name);
    printf("Envoyé à <%s | %s | socket %d> : %s\n", np->name, np->ip, np->data, send_buffer); // On affiche côté serveur
    send(client_sockfd, send_buffer, LENGTH_SEND, 0); // On envoi le message au client

    // Conversation
    while (1) {
        if (leave_flag) { // Si il y a eu une erreur, on sors de la boucle infinie
            break;
        }
        int receive = recv(np->data, recv_buffer, LENGTH_MSG, 0); // FONCTION BLOCANTE> On récupère le message reçu -> renvois -1 si erreur, 0 si la connexion est coupée, n la taille du message sinon
        if (receive > 0) { // Si le message a été reçu correctement
			
			printf("<%s | %s | socket %d> %s\n", np->name, np->ip, np->data, recv_buffer); // On l'affiche côté serveur
			
			//////////////////////////////////////////////////////////////////////////////////////////
			//////////////////////////////////////////////////////////////////////////////////////////
			// On vérifie si le message correspond à une commande quelconque (à interpréter) ou non //
			//////////////////////////////////////////////////////////////////////////////////////////
			//////////////////////////////////////////////////////////////////////////////////////////
			
			// On récupère le premier mot du message envoyé par le client (c'est celui ci qu'il faut analysé car il est le mot clé qui permet d'utiliser les commandes !)
			char* mot1 = NULL; // La variable permettant de stocker le premier mot récupéré
			int cptMsgRecv = 0; // Le compteur qui permet le parcours de chaîne
			getStringUntilChar(recv_buffer, &mot1, ' ', &cptMsgRecv); // On récupère le premier mot
			
			// On vérifie si le message correspond à une quelconque commande
			
			if(strcmp(recv_buffer, "!quit") == 0){ // Si le client s'est déconnecté volontairement avec la commande !quit (ici on compare le message reçu en entier et non juste le premier mot
												   // afin de ne pas interpréter la commande !quit si il y a quelque chose derrière
				printf(RED "<%s | %s | socket %d> a quitté le serveur.\n" RESET, np->name, np->ip, np->data); // On affiche la déconnexion côté serveur
				sprintf(send_buffer, "<%s> a quitté le serveur.", np->name); // On prépare le message pour prévenir tous les clients de la déconnexion
				send_to_all_clients(np, send_buffer); // On envoit le message à tous les clients
				leave_flag = 1; // On passe le flag a 1 pour quitter la boucle de conversation avec le client car celui ci est déconnecté
			}else if(strcmp(mot1, "!login") == 0){ // Si le mot clé est login, on change le pseudo de l'utilisateur par ce qui précède le mot clé
				
				char* nouveauPseudo = NULL; // variable temporaire
				for(int i=cptMsgRecv;i<strlen(recv_buffer);i++){ // On récupère la fin du message (qui correspond au nouveau pseudo que l'utilisateur souhaite utiliser)
					addCharToString(&nouveauPseudo,recv_buffer[i]);
				}
					if(nouveauPseudo != NULL){ // On vérifie qu'il n'y a pas eu de problèmes et que le nouveau pseudo est bien fonctionnel
						if(strlen(nouveauPseudo) < MIN_LENGTH_NAME || strlen(nouveauPseudo) >= MAX_LENGTH_NAME +1){ // Si le pseudo est trop court ou trop long, on renvoie une erreur
							sprintf(send_buffer, "Votre nom doit comporter entre %d et %d caractères.", MIN_LENGTH_NAME, MAX_LENGTH_NAME); // On prépare le message d'information à envoyer à tous les clients
							send(client_sockfd, send_buffer, LENGTH_SEND, 0); // On envoi le message au client
						}else{
							int err = 0;
							for (int i = 0; nouveauPseudo[i] != '\0'; ++i){ // on parcours le pseudo
								if (ispunct(nouveauPseudo[i])){ // On verifie qu'il n'y a pas de ponctuation
									err = 1;
								}
							}if(err == 1){
								sprintf(send_buffer, "Votre nom ne doit pas comporter de ponctuation."); // On prépare le message d'information à envoyer à tous les clients
								send(client_sockfd, send_buffer, LENGTH_SEND, 0); // On envoi le message au client
							}else{
								
								printf(RED "%s est maintenant connecté en tant que %s\n" RESET, np->name, nouveauPseudo); // On affiche pour les logs
								sprintf(send_buffer, "%s est maintenant connecté en tant que %s", np->name, nouveauPseudo); // On prépare le message d'information à envoyer à tous les clients
								strcpy(np->name,nouveauPseudo); // On attribu le nouveau pseudo à l'utilisateur
								
								// On libère la mémoire utilisée dans le processus
								free(nouveauPseudo);
								nouveauPseudo = NULL;
								
								send_to_all_clients(np, send_buffer); // On envoit le message à tous les clients
								sprintf(send_buffer, "Vous êtes désormais connecté en tant que %s", np->name); // On prépare le message d'information à envoyer à tous les clients
								send(client_sockfd, send_buffer, LENGTH_SEND, 0); // On envoi le message au client
							}
						}
				}
			}else if(strcmp(recv_buffer, "!help") == 0){ // Si le premier mot est help, c'est un mot clé et il faut envoyer la liste des commandes au client
				sprintf(send_buffer, "Liste des commandes :\n\t!help \n\t!quit \n\t!login <PSEUDO> \n\t!msg <DESTINATAIRE> (mettre * pour tous) <MESSAGE> \n\t!credits \n\t!hello \n\t!version"); // On prépare le message pour prévenir tous les clients de la déconnexion
				printf("Envoyé à <%s | %s | socket %d> : %s\n", np->name, np->ip, np->data, send_buffer); // On affiche côté serveur
				send(np->data, send_buffer, LENGTH_SEND, 0); // On envoi le message au client
			}else if(strcmp(mot1, "!msg") == 0){ // Si le premier mot est !msg, c'est un mot clé
				char* destinataire = NULL;
				getStringUntilChar(recv_buffer, &destinataire, ' ', &cptMsgRecv); // On récupère le deuxième mot qui s'avère être le destinataire
				if(destinataire != NULL){ // On vérifie qu'il n'y a pas eu de problème lors de la récupération du destinataire
					char* msg = NULL;
				for(int i=cptMsgRecv;i<strlen(recv_buffer);i++){ // On récupère la fin du message (qui correspond au message à transmettre)
					addCharToString(&msg,recv_buffer[i]);
				}
					if(msg != NULL){ // On vérifie qu'il n'y a pas eu de problème lors de la récupération du destinataire
						if(strcmp(destinataire,"*") == 0){
							sprintf(send_buffer, "<%s (privé)> %s", np->name, msg); // On prépare le message avant de l'envoyer en précisant qui l'a envoyé
							send_to_all_clients(np, send_buffer); // On envoie le message a tous le monde
						}else{
							send_to_clients(np, msg, destinataire); // On envoie le message au client choisi dans login
						}
					}
				}
				
			}else if(strcmp(recv_buffer, "!list") == 0){ // ici on compare le message reçu en entier et non juste le premier mot car !list ne doit pas être accompagné
				envoiListeClients(np);
			}else if(strcmp(recv_buffer, "!credits") == 0){
				print_credit_server_console();
				send_credit_client_console(np);
			}else if(strcmp(recv_buffer,"!hello") == 0) {
				sprintf(send_buffer, "Bienvenue %s sur le serveur ! ! Il est actuellement ",np->name);
				concateneHeure(send_buffer);
				printf("Envoyé à <%s | %s | socket %d> : %s\n", np->name, np->ip, np->data, send_buffer); // On affiche côté serveur
				send(np->data, send_buffer, LENGTH_SEND, 0); // On envoi le message au client
			}else if(strcmp(recv_buffer,"!version") == 0){
					sprintf(send_buffer, "La version du protocole utilisé est la version 1");
					printf("Envoyé à <%s | %s | socket %d> : %s\n", np->name, np->ip, np->data, send_buffer); // On affiche côté serveur
					send(np->data, send_buffer, LENGTH_SEND, 0); // On envoi le message au client
				}else{ // Cas général : le mot1 ne correspond à aucunes commandes -> on l'envoi à tous les utilisateurs : chat global
				sprintf(send_buffer, "<%s> %s", np->name, recv_buffer); // On prépare le message avant de l'envoyer en précisant qui l'a envoyé
				send_to_all_clients(np, send_buffer); // On envoit le message à tous les clients
			}
        } else if (receive == 0) { // Si le client a été déconnecté pour une raison quelconque
            printf(RED "<%s | %s | socket %d> a quitté le serveur.\n" RESET, np->name, np->ip, np->data); // On affiche la déconnexion côté serveur
            sprintf(send_buffer, "<%s> a quitté le serveur.", np->name); // On prépare le message pour prévenir tous les clients de la déconnexion
            send_to_all_clients(np, send_buffer); // On envoit le message à tous les clients
            leave_flag = 1; // On passe le flag a 1 pour quitter la boucle de conversation avec le client car celui ci est déconnecté
        } else { // Si il y a eu une erreur dans la transmission du message
            printf("Fatal Error: -1\n");
            leave_flag = 1; // On passe le flag a 1 pour quitter la boucle de conversation avec le client
        }
    }

    // On supprime le maillon de la liste chainée
    close(np->data); // On ferme la socket
    // 3 Cas possibles pour un maillon dans une liste chainée :
    // soit il est la tête de la liste chainée (impossible dans notre cas car la tête est fixée et n'a rien à voir avec les maillons clients),
    // soit il est en fin de la liste chainée
    // soit il est dans le milieu de la liste chainée (pas une extrémité)
    if (np == now) { // Dans le cas ou le maillon est le dernier de la liste chainée
        now = np->prev; // Son prédécésseur devient le nouveau dernier maillon
        now->link = NULL; // Son prédécesseur n'a plus de suivant
    } else { // Dans le cas ou le maillon est dans le milieu de la liste chainée
        np->prev->link = np->link; // On fait pointé le précédent vers le suivant
        np->link->prev = np->prev; // On fait pointé le suivant vers le précédent
    }
    free(np); // On libère la mémoire occupée par le maillon
}

int main(void)
{
    signal(SIGINT, catch_ctrl_c_and_exit); // Permet de ne pas interrompre le serveur lors d'un CTRL+C mais d'appeller la fonction catch_ctrl_c_and_exit à la place

    // Création de la socket
    server_sockfd = socket(AF_INET , SOCK_STREAM , 0);
    if (server_sockfd == -1) { // Si la socket = -1, il y a eu une erreur.
        printf("Erreur lors de la création de la socket serveur.");
        return 0; // On quitte le programme
    }

    // Informations de la socket
    struct sockaddr_in server_info, client_info;
    int s_addrlen = sizeof(server_info);
    int c_addrlen = sizeof(client_info);
    memset(&server_info, 0, s_addrlen);
    memset(&client_info, 0, c_addrlen);
    server_info.sin_family = PF_INET;
    server_info.sin_addr.s_addr = INADDR_ANY;
    server_info.sin_port = htons(PORT);
    
    // D'après les recherches sur internet, l'utilisation de SO_REUSEADDR devrait permettre de libérer plus rapidemment le port après fermeture du serveur.
    // A VERIFIER SUR LE LONG TERME, pour le moment ça à l'air de marcher
    // Faire remonter tout problème à @Yann LEFEVRE
    if (setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0){
		printf("L'utilisation de SO_REUSEADDR a échoué ! ");
	}

    // Bind et Listen
    bind(server_sockfd, (struct sockaddr *)&server_info, s_addrlen);
    listen(server_sockfd, 3);

    // Affiche les informations du serveur
    getsockname(server_sockfd, (struct sockaddr*) &server_info, (socklen_t*) &s_addrlen);
    printf(RED "Démarrage du serveur: %s:%d\n" RESET, inet_ntoa(server_info.sin_addr), ntohs(server_info.sin_port));

    // On crée la tête de liste pour les clients
    root = newNode(server_sockfd, inet_ntoa(server_info.sin_addr)); // Root représente la racine (tête) de la liste chainée
    now = root; // Now représente le dernier élément / l'élément actuel de la liste chainée

    while (1) { // Boucle infinie pour attendre en continu des connexions de clients
        client_sockfd = accept(server_sockfd, (struct sockaddr*) &client_info, (socklen_t*) &c_addrlen); // On accepte toutes les connexions

        // Récupère les informations sur le client qui vient de se connecter et les affiche
        getpeername(client_sockfd, (struct sockaddr*) &client_info, (socklen_t*) &c_addrlen);
        printf(RED "%s:%d est connecté.\n" RESET, inet_ntoa(client_info.sin_addr), ntohs(client_info.sin_port));

        // On actualise la liste chainée en ajoutant le nouveau client dedans
        ClientList *c = newNode(client_sockfd, inet_ntoa(client_info.sin_addr)); // On crée un nouveau maillon pour le client
        c->prev = now; // Le prédécesseur du nouveau maillon est l'ancien maillon courant (now)
        now->link = c; // Le suuivant de l'ancien maillon courant est le nouveau maillon 
        now = c; // Le nouveau maillon est le nouveau maillon courant

		// On isole les interactions entre le serveur et le client sur un thread afin de pouvoir interagir avec plusieurs cliens à la fois
        pthread_t id; // l'id du thread sur lequel sera isolé l'interaction avec le client. Sera initialisé par pthread_create
        if (pthread_create(&id, NULL, (void *)client_handler, (void *)c) != 0) { // Si la création de thread renvoi autre chose que 0, il y a une erreur
            perror("Erreur lors de la création du thread !\n");
            return 0; // On quitte le programme
        }
    }

    return 0;
}

void addCharToString(char** string, char c){
	// Si le tableau est vide, on en crée un de deux cases
	if(*string == NULL){
		*string = (char*)malloc(sizeof(char)*2);
		// On copie le nouveau caractère dedans
		(*string)[0] = c;
		(*string)[1] = '\0';
	}else{
		// On crée un tableau avec une case en plus
		char* tmp = (char*)malloc(sizeof(char)*(strlen(*string) + 2));
		// On recopie l'ancien mot dans le nouveau tableau
		strcpy(tmp,*string);
		// On copie le nouveau caractère dedans
		tmp[strlen(*string)] = c;
		tmp[strlen(*string) + 1] = '\0';
		free(*string);
		*string = tmp;
		tmp = NULL;
	}
	
}

void getStringUntilChar(char* input, char** output, char test, int* i){
	char c = 'a'; // Variable de stockage temporaire stockant les char récupéré
	while(1){ // On récupère les char jusqu'à ce qu'on rencontre la condition d'arrêt (passée en paramètre)
		c = input[*i]; // On récupère le char
		if(c != test && c != '\0'){ // On vériie la condition d'arrêt
			addCharToString(output,c); // On ajoute le char à l'output
			*i = *i + 1;
			//~ printf("%c\n",c); // Debugging
		}else{
			*i = *i + 1; // On incrémente tout de même le compteur afin de passer l'espace et faciliter l'interprétation de la suite du message
			break;
		}
	}
}

/**
 * Fonction permettant d'envoyer un message à un client uniquement
 * */
void send_to_clients(ClientList *np, char tmp_buffer[], char*login) {
    ClientList *tmp = root->link;
    char send_buffer[LENGTH_SEND];
    sprintf(send_buffer, "<%s (privé)> %s", np->name, tmp_buffer); // On prépare le message avant de l'envoyer en précisant qui l'a envoyé

        while (tmp != NULL) { // Tant que tmp != NULL <=> Tant qu'il y a des maillons dans la liste chainée
            if (np->data != tmp->data) { // On evite l'envoie a celui qui a envoyé le message
          if(strcmp(login,tmp->name) == 0) { // On envoie au client qui a été choisi
            printf("Envoyé à  <%s | %s | socket %d> : %s\n", tmp->name, tmp->ip, tmp->data, tmp_buffer);
            send(tmp->data, send_buffer, LENGTH_SEND, 0); // On envoi le message au client
          }
        }
        tmp = tmp->link;// On passe au client suivant en suivant le maillage de la liste chainée. Si il n'y a pas de suivant, tmp->link vaudra NULL
    }
}

void envoiListeClients(ClientList *np){
    ClientList *tmp = root->link;
    char* liste = NULL;
    char* liste2 = NULL;
    char send_buffer[LENGTH_SEND];

        while (tmp != NULL) { // Tant que tmp != NULL <=> Tant qu'il y a des maillons dans la liste chainée
            if (np->data != tmp->data) { // On evite l'envoie a celui qui a envoyé le message
				if(liste == NULL){
					liste = (char*)malloc(sizeof(char)*(strlen(tmp->name) + 1));
					strcpy(liste,tmp->name);
					strcat(liste,",");
				}else{
					liste2 = (char*)malloc(sizeof(char)*(strlen(liste) + strlen(tmp->name) + 1));
					strcpy(liste2,liste);
					strcat(liste2,tmp->name);
					strcat(liste2,",");
					free(liste);
					liste = liste2;
					liste2 = NULL;
				}
        }
        tmp = tmp->link;// On passe au client suivant en suivant le maillage de la liste chainée. Si il n'y a pas de suivant, tmp->link vaudra NULL
    }
    sprintf(send_buffer, "Clients connectés : %s",liste); // On prépare le message avant de l'envoyer en précisant qui l'a envoyé
    printf("Envoyé à  <%s | %s | socket %d> : %s\n", np->name, np->ip, np->data, send_buffer);
    send(np->data, send_buffer, LENGTH_SEND, 0); // On envoi le message au client
    
}

void send_credit_client_console(ClientList *np){

  char send_creditPierre[LENGTH_SEND] = {};
  char send_creditYann[LENGTH_SEND] = {};
  char send_creditLaura[LENGTH_SEND] = {};
  char send_creditMathias[LENGTH_SEND] = {};


  strcpy(send_creditLaura, "\r");
  strcat(send_creditLaura, "888 \n");
  strcat(send_creditLaura, "888 \n");
  strcat(send_creditLaura, "888 \n");
  strcat(send_creditLaura, "888       8888b.  888  888 888d888  8888b.\n");
  strcat(send_creditLaura, "888          88b  888  888 888P         88b \n");
  strcat(send_creditLaura, "888      .d888888 888  888 888     .d888888\n");
  strcat(send_creditLaura, "888      888  888 Y88b 888 888     888  888\n");
  strcat(send_creditLaura, "88888888 Y888888    Y88888 888     Y888888\n");
  strcat(send_creditLaura, "\n");
  strcat(send_creditLaura, "\n");
  send(np->data, send_creditLaura, LENGTH_SEND, 0);


  strcpy(send_creditMathias, "");
  strcat(send_creditMathias, "888b     d888          888    888      d8b \n");
  strcat(send_creditMathias, "8888b   d8888          888    888      Y8P \n");
  strcat(send_creditMathias, "88888b.d88888          888    888 \n");
  strcat(send_creditMathias, "888Y88888P888  8888b.  888888 88888b.  888  8888b.  .d8888b \n");
  strcat(send_creditMathias, "888 Y888P 888      88b 888    888  88b 888      88b 88K \n");
  strcat(send_creditMathias, "888  Y8P  888 .d888888 888    888  888 888 .d888888  Y8888b. \n");
  strcat(send_creditMathias, "888       888 888  888 Y88b.  888  888 888 888  888      X88 \n");
  strcat(send_creditMathias, "888       888  Y888888   Y888 888  888 888  Y888888  88888P' \n");
  strcat(send_creditMathias, "\n");
  strcat(send_creditMathias, "\n");
  send(np->data, send_creditMathias, LENGTH_SEND, 0);

  strcpy(send_creditYann, "");
  strcat(send_creditYann, "Y88b   d88P   \n");
  strcat(send_creditYann, " Y88b d88P   \n");
  strcat(send_creditYann, "  Y88o88P   \n");
  strcat(send_creditYann, "   Y888P  8888b.    88888b.    88888b.   \n");
  strcat(send_creditYann, "    888       88b   888  88b   888  88b  \n");
  strcat(send_creditYann, "    888  .d888888   888  888   888  888  \n");
  strcat(send_creditYann, "    888  888  888   888  888   888  888  \n");
  strcat(send_creditYann, "    888   Y888888   888  888   888  888  \n");
  send(np->data, send_creditYann, LENGTH_SEND, 0);

  strcpy(send_creditPierre, "");
  strcat(send_creditPierre, "8888888b.  d8b \n");
  strcat(send_creditPierre, "888   Y88b Y8P\n");
  strcat(send_creditPierre, "888    888\n");
  strcat(send_creditPierre, "888   d88P 888  .d88b.  888d888 888d888  .d88b.\n");
  strcat(send_creditPierre, "8888888P   888 d8P  Y8b 888P    888P    d8P  Y8b\n");
  strcat(send_creditPierre, "888        888 88888888 888     888     88888888\n");
  strcat(send_creditPierre, "888        888 Y8b.     888     888     Y8b.\n");
  strcat(send_creditPierre, "888        888  Y8888   888     888      Y8888\n");
  strcat(send_creditPierre, "\n");
  strcat(send_creditPierre, "\n");
  send(np->data, send_creditPierre, LENGTH_SEND, 0);
}

void print_credit_server_console(){


  printf("888 \n");
  printf("888 \n");
  printf("888 \n");
  printf("888       8888b.  888  888 888d888  8888b.\n");
  printf("888          88b  888  888 888P         88b \n");
  printf("888      .d888888 888  888 888     .d888888\n");
  printf("888      888  888 Y88b 888 888     888  888\n");
  printf("88888888 Y888888    Y88888 888     Y888888\n");
  printf("\n");
  printf("\n");



  printf("888b     d888          888    888      d8b \n");
  printf("8888b   d8888          888    888      Y8P \n");
  printf("88888b.d88888          888    888 \n");
  printf("888Y88888P888  8888b.  888888 88888b.  888  8888b.  .d8888b \n");
  printf("888 Y888P 888      88b 888    888  88b 888      88b 88K \n");
  printf("888  Y8P  888 .d888888 888    888  888 888 .d888888  Y8888b. \n");
  printf("888       888 888  888 Y88b.  888  888 888 888  888      X88 \n");
  printf("888       888  Y888888   Y888 888  888 888  Y888888  88888P' \n");
  printf("\n");
  printf("\n");



  printf("Y88b   d88P   \n");
  printf(" Y88b d88P   \n");
  printf("  Y88o88P   \n");
  printf("   Y888P  8888b.    88888b.    88888b.   \n");
  printf("    888       88b   888  88b   888  88b  \n");
  printf("    888  .d888888   888  888   888  888  \n");
  printf("    888  888  888   888  888   888  888  \n");
  printf("    888   Y888888   888  888   888  888  \n");


  printf("\n");
  printf("8888888b.  d8b \n");
  printf("888   Y88b Y8P\n");
  printf("888    888\n");
  printf("888   d88P 888  .d88b.  888d888 888d888  .d88b.\n");
  printf("8888888P   888 d8P  Y8b 888P    888P    d8P  Y8b\n");
  printf("888        888 88888888 888     888     88888888\n");
  printf("888        888 Y8b.     888     888     Y8b.\n");
  printf("888        888  Y8888   888     888      Y8888\n");
  printf("\n");
  printf("\n");
}

void concateneHeure(char* send_buffer){
	
	time_t t;
	struct tm * timeinfo;
	time(&t);
	char buf[20];
	timeinfo = localtime(&t);  
	
	strftime(buf,sizeof(buf),"%H:%M à Toulon !\n",timeinfo);
	strcat(send_buffer,buf);
	
}
