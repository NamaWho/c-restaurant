#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_LINE_INPUT 128

// ------------------------ SIGNATURES ------------------------
void handleTake(char*, int);
void handleShow(char*, int);
void handleReady(char*, int, char*);
void handleEsc(int);
// ------------------------ SIGNATURES ------------------------

// ------------------------ STRUCTURES ------------------------
// Booking options
struct reservation {
    char name[24];      
    uint8_t guest_num;
    char date[9];
    uint8_t hour;
} details;
// ------------------------ STRUCTURES ------------------------

// ------------------------ FUNCTIONS -------------------------
/**
 * handleTake(buf, sd)
 * buf - buffer
 * sd - socket descriptor
 * 
 * Handles communication with server in order to take in charge a reservation
*/
void handleTake(char* buf, int sd){
    uint ret;
    uint8_t mess_len;

    // query 'take' API to server
    strcpy(buf, "T\n");
    ret = send(sd, (void*)buf, strlen(buf), 0);

    recv(sd, (void*)&mess_len, sizeof(uint8_t), 0);

    if(mess_len){
        recv(sd, (void*)buf, mess_len, 0);
        printf("%s", buf);
        fflush(stdout);
    }
    else {
        printf("Nessuna comanda disponibile.\n");
        fflush(stdout);
    }
}

/**
 * handleShow(buf, sd)
 * buf - buffer
 * sd - socket descriptor
 * 
 * Handles communication with server in order to show reservation which are took in charge by the table device
*/
void handleShow(char* buf, int sd){
    uint ret;
    uint8_t mess_len;

    // query 'show' API to server
    strcpy(buf, "S\n");
    ret = send(sd, (void*)buf, strlen(buf), 0);

    recv(sd, (void*)&mess_len, sizeof(uint8_t), 0);
    if(mess_len){
        recv(sd, (void*)buf, mess_len, 0);
        printf("%s", buf);
        fflush(stdout);
    }
};

/**
 * handleReady(buf, sd)
 * buf - buffer
 * sd - socket descriptor
 * com - id of the order to deliver
*/
void handleReady(char* buf, int sd, char *com){
    uint ret;
    uint8_t mess_len;

    strcpy(buf, "R\n");
    send(sd, (void*)buf, strlen(buf), 0);

    mess_len = strlen(com);
    send(sd, (void*)&mess_len, sizeof(uint8_t), 0);
    send(sd, (void*)com, mess_len, 0);

    // wait server for response 
    recv(sd, (void*)&mess_len, sizeof(uint8_t), 0);
    if (mess_len){
        printf("COMANDA IN SERVIZIO\n");
        fflush(stdout);
    }
    else{
        printf("Impossibile servire la comanda\n");
        fflush(stdout);
    } 
};

/**
 * handleEsc(sd)
 * sd - socket descriptor to close
 * 
 * Closes socket connection with server
*/
void handleEsc(int sd){
    close(sd);
    exit(0);
};
// ------------------------ FUNCTIONS -------------------------

int main(int argc, char* argv[]){
    int ret, sd, len,
        fdmax; 
    uint8_t msg_server_len;
    ushort port;
    struct sockaddr_in srv_addr;
    char command[5];            // command given by user
    char com[24];
    char buffer[1024];

    fd_set master;
    fd_set read_fds;

    memset(buffer, 0, sizeof(buffer));

    if( argc != 2 ) {
        printf("One argument expected: <port>\n");
        fflush(stdout);
        exit(1);
    }

    // ------ Socket initialization and handshake with server ------
    port = (ushort)strtol(argv[1], NULL, 10); // argument binded to port number
    sd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &srv_addr.sin_addr);

    ret = connect(sd, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
    if(ret < 0){
        perror("Error during connection:");
        exit(-1);
    } 

    // Handshake with server ['K' - Kitchen device]
    strcpy(buffer, "K\n");   
    ret = send(sd, (void*)buffer, strlen(buffer), 0);      
    if(ret < 0){
        perror("Error during transmission: \n");
        exit(-1);
    }
    // ------------------------------------------------------------

    printf("take >\taccetta una comanda\nshow >\tmostra le comande accettate (in preparazione)\nready >\timposta lo stato della comanda\n\n");
    fflush(stdout);

    // Reset FDs
    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    FD_SET(sd, &master);
    FD_SET(0, &master);

    fdmax = sd;

    while(1){
        printf("> ");
        fflush(stdout);

        read_fds = master;
        ret = select(fdmax+1, &read_fds, NULL, NULL, NULL);
        if(ret<0){
            perror("Errore durante la select:");
            exit(1);
        }

        int i;
        for (i = 0; i <= fdmax; i++){
            if(FD_ISSET(i, &read_fds)){
                if(i == 0){
                    // user input with command retrieval
                    fgets(buffer, MAX_LINE_INPUT, stdin);
                    sscanf(buffer,"%s", command);

                    if(!strcmp("take", command)){
                        handleTake(buffer, sd);
                    }
                    else if (!strcmp("show", command)){
                        handleShow(buffer, sd);
                    }
                    else if (!strcmp("ready", command)){
                        sscanf(buffer, "%*s %s\n", com);

                        if (!strlen(com))
                            printf("Sintassi errata, confermare una comanda.\n");
                        else 
                            handleReady(buffer, sd, com);
                    }
                } else {
                    recv(sd, (void*)&msg_server_len, sizeof(uint8_t), 0); 
                    
                    if(!msg_server_len)
                        // server has started its closing procedure
                        handleEsc(sd);
                    else {
                        // server is notifying kitchen a change in orders pending
                        int j;
                        for (j = 0; j < msg_server_len; j++)
                            printf("*");
                        printf("\n");
                        fflush(stdout);
                    }
                }
            }
        }
    }
}
