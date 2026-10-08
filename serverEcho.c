#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>


void client_arrived(int sock){
    int r;
    char buff[100];
    char* aff = "Vous avez envoyé: ";
    if (sock<0){
        perror("accept"); 
        exit(1);
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
    
    if(argc!=2){
        perror("Syntaxe: ./serveur port_num");
        exit(1);
    }

    int port_num = atoi(argv[1]);

    listen_port(port_num);
    return 0;
}