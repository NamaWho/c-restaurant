#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_LINE_INPUT 128
#define BUF_LEN 1024

// ------------------------ SIGNATURES ------------------------
void handleHelp();
void handleMenu(char*, int);
void handleComanda(char*, int, uint8_t);
void handleConto(char*, int);
void handleEsc(int);
// ------------------------ SIGNATURES ------------------------

// ------------------------ FUNCTIONS ------------------------
/**
 * handleHelp()
 * 
 * Prints out a list of available commands
*/
void handleHelp(){
    printf("I comandi disponibili sono:\n• menu\n• comanda {<piatto_1-quantità_1>...<piatto_n-quantità_n>}\n• conto\n");
    fflush(stdout);
}

/**
 * handleMenu(buf, sd)
 * buf - buffer
 * sd - socket descriptor
 * 
 * Handles communication with server in order to retrieve and print out menu of the restaurant
*/
void handleMenu(char *buf, int sd){
    uint ret;
    uint16_t mess_len;

    // call to 'menu' API to server
    strcpy(buf, "M\n");
    ret = send(sd, (void*)buf, strlen(buf), 0);

    // server sends back message length to display [BINARY]
    ret = recv(sd, (void*)&mess_len, sizeof(uint16_t), 0);
    mess_len = ntohs(mess_len);

    // receive message and display to stdout [TEXT]
    ret = recv(sd, (void*)buf, mess_len, 0); 

    printf("%s", buf);
    fflush(stdout);
}

/**
 * handleComanda(buf, sd, num_comanda)
 * buf - buffer
 * sd - socket descriptor
 * num_comanda - incremental id of the order made by the table device
*/
void handleComanda(char* buf, int sd, uint8_t num_comanda){
    int com_found = 0;
    int ch, i = 0;
    char com[8];
    uint8_t com_length = 0;
    char first_part[5];
    int second_part;

    // call to 'comanda' API to server
    strcpy(com, "C\n");
    send(sd, (void*)com, strlen(com), 0);

    // send number of order made
    send(sd, (void*)&num_comanda, sizeof(uint8_t), 0);

    memset(com, 0, sizeof(com));
    // Scan buffer to get all the orders properly
    do
    {
        ch = buf[i++]; 
        
        if (com_found){
            if(ch == ' ' || ch == '\n'){
                com[com_length++] = '\n';

                // Basic check for correct format
                first_part[0] = '\0';
                second_part = 0;
                sscanf(com, "%[^-]%*c%d", first_part, &second_part);
                
                if (strlen(first_part) > 0 && strlen(first_part) < 4 && second_part > 0 && second_part < 50){
                    // E.g. 
                    // com = 'A2-2\n"
                    // Table sends each single order to server which parses properly the fields
                    send(sd, (void*)&com_length, sizeof(uint8_t), 0);
                    send(sd, (void*)com, com_length, 0);
                }

                com_length = 0;
            } else 
                com[com_length++] = ch;
        }

        if (!com_found && ch == ' ')
            com_found = 1;        
    } while (ch != EOF && ch != '\n');

    // Send finish code to server
    strcpy(com, "0\n");
    com_length = strlen(com);
    send(sd, (void*)&com_length, sizeof(uint8_t), 0);
    send(sd, (void*)com, com_length, 0);

    recv(sd, (void*)&com_length, sizeof(uint8_t), 0);
    
    if(com_length)
        printf("COMANDA RICEVUTA\n");
    else 
        printf("COMANDA INVALIDA\n");

    fflush(stdout);
}

/**
 * handleConto(buf, sd)
 * buf - buffer
 * sd - socket descriptor
 * 
 * Handles communication with server in order to close the meal and get the receipt
*/
void handleConto(char* buf, int sd){
    uint ret;
    uint8_t mess_len;

    // call to 'conto' API to server
    strcpy(buf, "R\n");
    ret = send(sd, (void*)buf, strlen(buf), 0);

    // server sends back message length to display [BINARY]
    recv(sd, (void*)&mess_len, sizeof(uint8_t), 0);
    if(!mess_len){
        printf("Impossibile richiedere il conto durante il servizio, attendere la fine del pasto.\n");
        fflush(stdout);
        return;
    }

    memset(buf, 0, sizeof(buf));
    do
    {
        if(mess_len == 0){
            break;
        } else 
            recv(sd, (void*)buf, mess_len, 0);

        buf[mess_len] = '\0';
        printf("%s", buf);
        fflush(stdout);
        recv(sd, (void*)&mess_len, sizeof(uint8_t), 0);
    } while (1);

    exit(0);
}

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

// ------------------------ FUNCTIONS ------------------------

// ------------------------ MAIN -------------------------
void main(int argc, char* argv[]){
    int sd, ret,
        fdmax;
    char newline;
    uint8_t mess_len_8, num_comanda = 0;
    uint16_t mess_len;
    ushort port;
    struct sockaddr_in srv_addr;
    char temp_buffer[128];
    char buffer[BUF_LEN];
    char command[8];
    uint8_t is_logged = 0;

    fd_set master;
    fd_set read_fds;

    memset(buffer, 0, sizeof(buffer));

    if( argc != 2 ) {
        printf("One argument expected: <port>\n");
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
        perror("Error during connection: \n");
        exit(-1);
    }

    // Handshake with server ['T' - Table device]
    strcpy(buffer, "T\n");                         
    ret = send(sd, (void*)buffer, strlen(buffer), 0);      
    if(ret < 0){
        perror("Error during transmission: \n");
        exit(-1);
    }
    // ------------------------------------------------------------
    
    // Login procedure with reservation code authentication
    do {

        printf("Inserire codice della prenotazione:\n");
        fflush(stdout);
        scanf("%s%c", buffer, &newline);

        strcpy(temp_buffer, "L\n");
        send(sd, (void*)temp_buffer, strlen(temp_buffer), 0);
        
        mess_len_8 = strlen(buffer)+1;
        send(sd, (void*)&mess_len_8, sizeof(uint8_t), 0);
        send(sd, (void*)buffer, mess_len_8, 0);

        // Server sends back 1 or 0 whether code is valid or not respectively 
        recv(sd, (void*)&mess_len_8, sizeof(uint8_t), 0);
        is_logged = (mess_len_8) ? 1 : 0;
    } while (!is_logged);
    // ------------------------------------------------------------

    // Reservation procedure
    printf("***************************** BENVENUTO *****************************\nDigita un comando:\n\n1) help\t\t\t--> mostra i dettagli dei comandi\n2) menu\t\t\t--> mostra il menu dei piatti\n3) comanda\t\t--> invia una comanda\n4) conto\t\t--> chiede il conto\n");
    fflush(stdout);

    // Reset FDs
    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    FD_SET(sd, &master);
    FD_SET(0, &master);

    fdmax = sd;

    while(1){
        printf("\n> ");
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
                    memset(buffer, 0, BUF_LEN);
                    fgets(buffer, MAX_LINE_INPUT, stdin);
                    sscanf(buffer,"%s", command);

                    if(!strcmp("help", command))
                        handleHelp(buffer);
                    else if (!strcmp("menu", command))
                        handleMenu(buffer, sd);
                    else if (!strcmp("comanda", command))
                        handleComanda(buffer, sd, ++num_comanda);
                    else if (!strcmp("conto", command))
                        handleConto(buffer, sd);
                    else 
                        continue;
                } else {
                    // server 
                    recv(sd, (void*)&mess_len_8, sizeof(uint8_t), 0); 
                    
                    if(!mess_len_8)
                        // server has started its closing procedure
                        handleEsc(sd);
                    else {
                        // server is sending an updated status of the orders made by the table
                        recv(sd, (void*)buffer, mess_len_8, 0);
                        printf("%s", buffer);
                        fflush(stdout);
                    }

                }
            }
        }
    }
}
// ------------------------ MAIN -------------------------