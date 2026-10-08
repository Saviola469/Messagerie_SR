#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>
#define BUFSIZE 1024


int nr_clients;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct client_data_s {
    int sock;
    pthread_t thread;
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

    nr_clients--;

    pthread_mutex_unlock(&lock);

    free(cli);
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
        snprintf(resp, BUFSIZE, "ok %d\n", n%a);
        return 1;
    }
    return -1;
}

int do_time(char *resp){
    time_t now;
    time(&now);
    snprintf(resp, BUFSIZE, "ok %s\n", ctime(&now));
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

int do_list(char *resp){
    client_data* cur = first;
    int offset = 0;

    pthread_mutex_lock(&lock);
    while(cur != NULL){
        offset += snprintf(resp + offset, BUFSIZE - offset,"Client %d\n", cur->sock);
        cur = cur->next;
    }
    pthread_mutex_unlock(&lock);

    return 0;
}

int eval_quest(char *question, char *resp) {
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
        return do_list(resp);
    else {
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
        if(eval_quest(question, response) < 0)
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
        int s2=accept(s,NULL,NULL);
        if (s2<0){
            perror("accept"); continue;
        }
        client_arrived(s2);
    }
}


int main(int argc, char* argv[]){
    int sock=socket(PF_INET6,SOCK_STREAM,0);
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