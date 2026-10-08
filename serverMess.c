#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>
#include <netdb.h>
#define BUFSIZE 1024


int nr_clients;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct s_msg {
    char sender[9];
    char* text;
    struct s_msg * next;
} msg;
msg* create_msg(char* author, char* contents) {
    // Allocation de la mémoire pour la structure elle-même
    msg* new_msg = malloc(sizeof(msg));
    if (new_msg == NULL) return NULL;

    // Copie sécurisée de l'auteur (max 8 caractères utiles)
    // On utilise strncpy comme recommandé dans l'énoncé
    strncpy(new_msg->sender, author, 8);
    new_msg->sender[8] = '\0'; // On s'assure de toujours garder '\0' à la fin

    // Allocation de la mémoire spécifiquement pour la taille du texte
    new_msg->text = malloc(strlen(contents) + 1); // +1 pour le '\0'
    if (new_msg->text == NULL) {
        free(new_msg); // En cas d'erreur, on nettoie ce qu'on a déjà alloué
        return NULL;
    }
    strcpy(new_msg->text, contents); // On peut utiliser strcpy car la taille est parfaitement allouée

    // Initialisation du pointeur suivant
    new_msg->next = NULL;
    
    return new_msg;
}
void destroy_msg(msg* mess) {
    if (mess != NULL) {
        free(mess->text); // Toujours libérer les sous-éléments d'abord !
        free(mess);       // Puis libérer le conteneur principal
    }
}
void show_msg(msg* mess) {
    if (mess != NULL) {
        printf("De : %s | Message : %s\n", mess->sender, mess->text);
    }
}



typedef struct s_mbox {
    msg *first, *last;
} mbox;
void init_mbox(mbox* box) {
    box->first = NULL;
    box->last = NULL;
}
void put(mbox* box, msg* mess) {
    if (mess == NULL) return;
    
    mess->next = NULL; // On s'assure qu'il n'y a rien après ce nouveau dernier message
    
    if (box->first == NULL) {
        // Cas 1 : La boîte est vide
        box->first = mess;
        box->last = mess;
    } else {
        // Cas 2 : La boîte n'est pas vide
        box->last->next = mess; // L'ancien dernier pointe vers le nouveau
        box->last = mess;       // Le nouveau devient officiellement le dernier
    }
} 
msg* get(mbox* box) {
    if (box->first == NULL) {
        // Cas 1 : Boîte vide
        return NULL;
    }
    
    msg* temp = box->first; // On sauvegarde le premier message pour le renvoyer
    
    if (box->first == box->last) {
        // Cas 2 : Un seul message dans la boîte
        box->first = NULL;
        box->last = NULL;
    } else {
        // Cas 3 : Plus d'un message
        box->first = box->first->next; // Le deuxième devient le premier
    }
    
    temp->next = NULL; // On détache complètement le message récupéré de la file
    return temp;
} 
void send_mess(mbox* box, char* author, char* contents) {
    // On réutilise create_msg de l'exo 1 !
    msg* nouveau = create_msg(author, contents);
    if (nouveau != NULL) {
        put(box, nouveau);
    }
}
void receive_mess(mbox* box) {
    msg* recu = get(box);
    if (recu == NULL) {
        printf("La boite est vide, aucun message a lire.\n");
    } else {
        show_msg(recu);
        destroy_msg(recu); // TRÈS IMPORTANT : une fois lu, on libère la mémoire !
    }
} 
void nett_mbox(mbox* box){
    msg* reste;
    while ((reste = get(box)) != NULL) {
        destroy_msg(reste);
    }
}


typedef struct client_data_s {
    int sock;
    pthread_t thread;
    char alias[9];
    mbox box;
    struct client_data_s* prev;
    struct client_data_s* next;
}client_data;

client_data *first,*last;

void init_clients(){
    nr_clients = 0;
    first = NULL;
    last = NULL;
}

client_data* alloc_client(int sock){
    client_data* cli = malloc(sizeof(client_data)); 
    if(cli == NULL)   return cli;

    pthread_mutex_lock(&lock);
    cli->sock = sock;
    strcpy(cli->alias,"");
    init_mbox(&(cli->box));
    if(first == NULL){
        cli->prev=NULL;
        cli->next = NULL;
        first = cli;
        last = cli;
    }else{
        cli->prev = last; 
        cli->next = NULL;
        last->next=cli;
        last = cli;  
    }
    
    nr_clients++;
    pthread_mutex_unlock(&lock);

    return cli;
}

void free_client(client_data* cli){
    pthread_mutex_lock(&lock);

    if(cli->prev != NULL)
        cli->prev->next = cli->next;
    else
        first = cli->next;

    if(cli->next != NULL)
        cli->next->prev = cli->prev;
    else
        last = cli->prev;

    nett_mbox(&(cli->box));
    nr_clients--;

    pthread_mutex_unlock(&lock);

    free(cli);
}

client_data* search_client(char* alias){
    client_data* cur=first;
    while(cur!=NULL){
        if(strcmp(cur->alias,alias)==0)
            break;
        cur = cur->next;
    }
    return cur;
}

int valid_alias(char* alias){
    int len = strlen(alias);
    if(len < 1 || len > 8)
        return 0;

    for(int i=0;i<len;i++)
        if(!isalnum(alias[i]))
            return 0;

    pthread_mutex_lock(&lock);
    int ok = (search_client(alias) == NULL);
    pthread_mutex_unlock(&lock);
    return ok;
}

void advance(char *buf, int *plen, int n) {
    memmove(buf, buf + n, *plen - n);
    *plen -= n;
}

int message_end(char *buf,int len){
    char* pos= memchr(buf,'\n',len );
    if (pos==NULL)
        return -1;
    return pos-buf+1;
}

int get_question(int sock, char *buf, int *plen, char* question){
    int n=-1;
    while(*plen < BUFSIZE) {
        n = message_end(buf, *plen);
        if(n >= 0)
            break;
        int rc = read(sock, buf + *plen, BUFSIZE - *plen);
        if(rc <= 0)
            break;
        *plen += rc;
    }
    if(n >= 0){
        memcpy(question,buf,n);
        if(question[n-2]=='\r')
            question[n-2]=0; //REMPLACE LE \r par \0 si il existe
        else question[n-1]=0; //REMPLACE LE \n par \0
        advance(buf, plen, n);
        return n;
    }
    return -1;
}

int write_full(int fd, char *buf, int len){
    int k = 0; //nb d’octets envoyés déjà
    while(k < len) {
        int rc = write(fd,buf+k,len-k);
        if(rc <= 0)
            return -1;
        k += rc;
    }
    return len;
}

int do_echo(char *args, char *resp){
    if(args == NULL){
        snprintf(resp, BUFSIZE, "ok \n");
        return 0;
    }
    else{ 
        snprintf(resp, BUFSIZE, "ok %s\n", args);
        return 1;
    }
    return -1;
}

int do_rand(char *args, char *resp){
    int n = rand();
    if(args == NULL || strlen(args)==0){
        snprintf(resp, BUFSIZE, "ok %d\n", n);
        return 0;
    }
    else{ 
        int a = atoi(args);

        if(a <= 0){
            snprintf(resp, BUFSIZE, "fail invalid argument\n");
            return -1;
        }

        snprintf(resp, BUFSIZE, "ok %d\n", n%a);
        return 1;
    }
    return -1;
}

int do_time(char *resp){
    time_t now;
    time(&now);
    snprintf(resp, BUFSIZE, "ok %s", ctime(&now));
    return 0;
    /*
    snprintf(resp, BUFSIZE, "fail unknown command time %s\n", args);
    return -1;
    */
}

int do_quit(char *resp){
    snprintf(resp, BUFSIZE, "quit\n");
    return -1;    
}


int do_list(client_data* cli, char *resp){
    if(strcmp(cli->alias,"")==0){
        snprintf(resp, BUFSIZE, "fail give alias first\n");
        return 0;
    }

    int offset = snprintf(resp, BUFSIZE, "ok ");

    pthread_mutex_lock(&lock);
    for(client_data* cur = first; cur != NULL; cur = cur->next)
        offset += snprintf(resp + offset, BUFSIZE - offset, "%s ", cur->alias);
    pthread_mutex_unlock(&lock);

    resp[offset++] = '\n';
    resp[offset] = '\0';
    return 0;
}

int do_alias(client_data* cli, char *args, char *resp){

    if(cli->alias[0] != '\0'){
        snprintf(resp, BUFSIZE, "fail alias already set\n");
        return 0;
    }

    if(args == NULL){
        snprintf(resp, BUFSIZE, "Syntaxe alias pseudo\n");
        return -1;
    }
    else if(valid_alias(args)==1){
        strcpy(cli->alias,args);
        snprintf(resp, BUFSIZE, "ok\n");
        return 1;
    }else{
        snprintf(resp, BUFSIZE, "fail alias unavailable\n");
        return -1;
    }
}

int do_send(client_data* cli, char *args, char *resp){
    if(strcmp(cli->alias,"")==0){
        snprintf(resp, BUFSIZE, "fail give alias first\n");
        return -1;
    }
    if(args == NULL){
        snprintf(resp, BUFSIZE, "Syntaxe send pseudo message\n");
        return -1;
    }
    else{
        char al[518], mess[518];
        int n=0;
        while(n<=strlen(args) && args[n]!=' ')
            n++;
        if(n==strlen(args)){
            strcpy(al, args);
            snprintf(resp, BUFSIZE, "Syntaxe send pseudo message\n");
            return -1;
        }else{
            strncpy(al,args,n);
            al[n]='\0';
            strcpy(mess, args+n+1);


        }

        pthread_mutex_lock(&lock);
        client_data* destinateur = search_client(al);
        if(destinateur==NULL){
            pthread_mutex_unlock(&lock);
            snprintf(resp, BUFSIZE, "pseudo unvailable\n");
            return -1;
        }
        send_mess(&(destinateur->box),cli->alias,mess);
        pthread_mutex_unlock(&lock);

        snprintf(resp, BUFSIZE, "ok\n");
        return 1;
    }
}

int do_recv(client_data* cli, char *args, char *resp){
    if(cli->alias[0]=='\0'){
        snprintf(resp, BUFSIZE, "fail give alias first\n");
        return 0;
    }

    msg* recu = get(&(cli->box));
    if (recu == NULL) {
        snprintf(resp, BUFSIZE, "ok\n");
        return 0;
    }

    snprintf(resp, BUFSIZE, "ok %s %s\n", recu->sender, recu->text);
    destroy_msg(recu);
    return 0;
}

int eval_quest(client_data* cli, char *question, char *resp) {
    char cmd[512], args[512], *pargs=NULL;
    // analyse question et separe la commande des arguments
    int n=0;
    while(n<=strlen(question) && question[n]!=' ')
        n++;
    if(n==strlen(question)){
        strcpy(cmd, question);
        pargs = NULL;
    }else{
        strncpy(cmd,question,n);
        cmd[n]='\0';
        strcpy(args, question+n+1);
        pargs=args;
    }
    
    if(strcmp(cmd, "echo") == 0)
        return do_echo(pargs, resp);
    else if(strcmp(cmd, "random") == 0)
        return do_rand(pargs, resp);
        //return do_rand(args, resp);
    else if(strcmp(cmd, "time") == 0)
        return do_time(resp);
    else if(strcmp(cmd, "quit") == 0)
        return do_quit(resp);
    else if(strcmp(cmd, "list") == 0)
        return do_list(cli,resp);
    else if(strcmp(cmd, "alias") == 0)
        return do_alias(cli,pargs,resp);
    else if(strcmp(cmd, "send") == 0)
        return do_send(cli,pargs,resp);
    else if(strcmp(cmd, "recv") == 0)
        return do_recv(cli,pargs,resp);
    else  {
        snprintf(resp, BUFSIZE, "fail unknown command %s\n", cmd);
        return 0;
    }
}

void* worker(void* arg){
    client_data* cli = (client_data*)arg;
    int len=0;
    char* aff = "Vous avez envoyé: ";
    char question[BUFSIZE], response[BUFSIZE], buf[BUFSIZE];

    printf("client arrivé\n");
    /*while ( (r=read(cli->sock,buff,99)) >0) {
        buff[r]='\0';    
        printf("%d octets reçus\n",r);
        printf("Ecriture dans le serveur %s\n",buff);
        write(cli->sock,aff,strlen(aff));
        write(cli->sock,buff,r);
        
    }*/

    while(1) {
        if(get_question(cli->sock,buf, &len, question) < 0)
            break; /* erreur: on deconnecte le client */
        if(eval_quest(cli,question, response) < 0)
            break; /* erreur: on deconnecte le client */
        if(write_full(cli->sock, response, strlen(response)) < 0) {
            perror("could not send message");
            break; /* erreur: on deconnecte le client */
        }
    }
    close(cli->sock);
    free_client(arg);
    printf("client parti\n");
}


void client_arrived(int sock){
    /*int r;
    char buff[100];
    char* aff = "Vous avez envoyé: ";
    */

    if (sock<0){
        perror("accept"); 
        exit(1);
    }

    client_data* cli=alloc_client(sock) ;
    int rc = pthread_create(&cli->thread,NULL,worker,(void*)cli);
    pthread_detach(cli->thread);
    printf("Nombre de clients: %d\n", nr_clients);
    /*client_data* cur=first;
    while(cur!=NULL){
        printf("Client: %d\n", cur->sock);
        cur = cur->next;
    }
    
    printf("client arrivé\n");
    while ( (r=read(sock,buff,99)) >0) {
        buff[r]='\0';
            
        printf("%d octets reçus\n",r);
        printf("Ecriture dans le serveur %s\n",buff);
        write(sock,aff,strlen(aff));
        write(sock,buff,r);
        
    }
    close(sock);
    printf("client parti\n");
    */
}


void listen_port(int port_num){
    int s=socket(PF_INET6,SOCK_STREAM,0);

    struct sockaddr_in6 sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin6_family=AF_INET6;
    sin.sin6_port=htons(port_num);

    int r=bind(s,(struct sockaddr *)&sin,sizeof(struct sockaddr_in6));
    if(r<0){
        perror("bind"); close(s);exit(1);
    }
    
    r=listen(s,1024);
    if(r<0){
        perror("listen");close(s);exit(1);
    }
    printf("j'attends mes clients\n");

    while(1){
        
        struct sockaddr_in6 addr;
        socklen_t addr_len = sizeof(addr);

        int s2 = accept(s, (struct sockaddr *)&addr, &addr_len);
        if (s2 < 0) {
            perror("accept");
            continue;
        }

        char host[BUFSIZE];

        if (getnameinfo((struct sockaddr *)&addr, addr_len,
                        host, sizeof(host),
                        NULL, 0, 0) == 0) {
            printf("connection from %s\n", host);
        } else {
            printf("connection from unknown host\n");
        }

        client_arrived(s2);
    }
}


int main(int argc, char* argv[]){
    
    srand( time( NULL ) );

    if(argc!=2){
        perror("Syntaxe: ./serveur port_num");
        exit(1);
    }

    int port_num = atoi(argv[1]);
    init_clients();
    listen_port(port_num);
    return 0;
}