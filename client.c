#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define MAX_LINE_INPUT 128
#define RESERVATION_CODE_LENGTH 7

// ------------------------ SIGNATURES ------------------------
void handleFind(char*, int);
void handleBook(char*, int, uint8_t);
void handleEsc(int);
// ------------------------ SIGNATURES ------------------------

// ------------------------ STRUCTURES ------------------------
struct date
{
    int int_yy;
    int int_mm;
    int int_dd;
    char yy[3];
    char mm[3];
    char dd[3];
};

// Booking options
struct reservation {
    char name[24];      
    uint8_t guest_num;
    char date[9];
    uint8_t hour;
} details;
// ------------------------ STRUCTURES ------------------------


// ------------------------ UTILITIES -------------------------
// Function to check leap year.
int  IsLeapYear(int year)
{
    return (((year % 4 == 0) &&
             (year % 100 != 0)) ||
            (year % 400 == 0));
}

int isValidDate(struct date *validDate, uint8_t hour)
{
    validDate->int_yy = atoi(validDate->yy) + 2000;
    validDate->int_mm = atoi(validDate->mm);
    validDate->int_dd = atoi(validDate->dd);

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    // handle hour range
    // ----- PRODUCTION
    // if (hour < 12 || (hour > 15 && hour < 19) || hour > 23) 
    //     return 0;
    // ----- PRODUCTION
    // ----- TESTING
    if (hour < 8 || hour > 23) 
        return 0;
    // ----- TESTING

    // check range of year,month and day
    if (validDate->int_yy > (tm.tm_year + 1901) ||
            validDate->int_yy < (tm.tm_year + 1900))
        return 0;
    if (validDate->int_mm < 1 || validDate->int_mm > 12)
        return 0;
    if (validDate->int_dd < 1 || validDate->int_dd > 31)
        return 0;

    //Handle feb days in leap year
    if (validDate->int_mm == 2)
    {
        if (IsLeapYear(validDate->int_yy))
            return (validDate->int_dd <= 29);
        else
            return (validDate->int_dd <= 28);
    }

    // handle months which has only 30 days
    if (validDate->int_mm == 4 || validDate->int_mm == 6 ||
            validDate->int_mm == 9 || validDate->int_mm == 11)
        return (validDate->int_dd <= 30);

    // handle if reserving a day already passed
    if( (validDate->int_yy == tm.tm_year+1900 && validDate->int_mm < tm.tm_mon+1) ||
        (validDate->int_yy == tm.tm_year+1900 && validDate->int_mm == tm.tm_mon+1 && validDate->int_dd < tm.tm_mday))
        return 0;

    // handle if reserving the same day but at an hour already passed
    if (validDate->int_yy == tm.tm_year+1900 && 
        validDate->int_mm == tm.tm_mon + 1 &&
        validDate->int_dd == tm.tm_mday && 
        hour < tm.tm_hour)
        return 0;

    return 1;
}
// ------------------------ UTILITIES -------------------------

// ------------------------ FUNCTIONS -------------------------

/**
 * handleFind(buf, sd)
 * buf - buffer with parameters of the reservation request
 * sd - socket descriptor
 * 
 * Handles communication with server in order to display available tables
*/
void handleFind(char* buf, int sd){
    uint ret;
    uint16_t payload, mess_len;

    // query 'find' API to server
    strcpy(buf, "F\n");
    ret = send(sd, (void*)buf, strlen(buf), 0);

    // ----- transmission of booking details
    sprintf(buf, "%s %d %s %d\n", details.name, details.guest_num, details.date, details.hour);
    mess_len = htons(strlen(buf));
    ret = send(sd, (void*)&mess_len, sizeof(uint16_t), 0);
    ret = send(sd, (void*)buf, ntohs(mess_len), 0);

    // server sends back message length to display [BINARY]
    ret = recv(sd, (void*)&mess_len, sizeof(uint16_t), 0);
    mess_len = ntohs(mess_len);

    if(mess_len){
        // receive message and display to stdout [TEXT]
        ret = recv(sd, (void*)buf, mess_len, 0); 
        buf[mess_len] = '\0';
    } else 
        sprintf(buf, "Impossibile prenotare con le specifiche richieste.\n\0");

    puts(buf);
}

/**
 * handleBook(buf, sd, num)
 * buf - buffer
 * sd - socket descriptor
 * num - proposal id to confirm
 * 
 * Handles communication with server in order to confirm the booking
*/
void handleBook(char* buf, int sd, uint8_t num){
    uint ret;
    uint8_t mess_len;

    // query 'book' API to server
    strcpy(buf, "B\n");
    ret = send(sd, (void*)buf, strlen(buf), 0);

    // num [BINARY]
    ret = send(sd, (void*)&num, sizeof(uint8_t), 0);

    // server sends back message length to display [BINARY]
    // ret = recv(sd, (void*)&mess_len, sizeof(uint8_t), 0);
    // mess_len = ntohs(mess_len);

    // receive message and display to stdout [TEXT]
    ret = recv(sd, (void*)buf, RESERVATION_CODE_LENGTH, 0); 

    // If reservation code is '000000' it means that booking did not end correctly
    if(!strcmp(buf, "000000")){
        printf("Prenotazione non andata a buon fine, riprovare.\n");
        fflush(stdout);
    } else {
        printf("PRENOTAZIONE EFFETTUATA.\n");
        printf("Codice: %s\n", buf);
        fflush(stdout);

        recv(sd, (void*)&mess_len, sizeof(uint8_t), 0);
        recv(sd, (void*)buf, mess_len, 0);
        buf[mess_len] = '\0';
        printf("Tavolo: %s\n", buf);
        fflush(stdout);

        recv(sd, (void*)&mess_len, sizeof(uint8_t), 0);
        recv(sd, (void*)buf, mess_len, 0); 
        buf[mess_len] = '\0';
        printf("Sala: %s\n", buf);
        fflush(stdout);

        handleEsc(sd);
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

// ------------------------ MAIN -------------------------

int main(int argc, char* argv[]){
    int ret, 
        sd,                         // socket descriptor
        len,                        // length
        fdmax;                      // max socket to listen to
    ushort port;
    uint8_t booking_id;             // id of the proposal chosen
    struct sockaddr_in srv_addr;
    char command[5];                // command given by user
    char buffer[1024];          
    struct date date_input;         // datestring in input 

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

    // Handshake with server ['C' - Client device]
    strcpy(buffer, "C\n");   
    ret = send(sd, (void*)buffer, strlen(buffer), 0);      
    if(ret < 0){
        perror("Error during transmission: \n");
        exit(-1);
    }
    // ------------------------------------------------------------

    printf("find >\tricerca la disponibilita' per una prenotazione\nbook >\tinvia una prenotazione\nesc  >\ttermina il client\n\n");
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

                    if(!strcmp("find", command)){
                        sscanf(buffer, "%s %s %d %s %d", command, &details.name, &details.guest_num, &details.date, &details.hour);

                        // Input sanitization
                        sscanf(details.date, "%[^-]-%[^-]-%[^-]", date_input.dd, date_input.mm, date_input.yy);
                        if (strlen(details.name) <= 0 ||
                            strlen(details.name) > 24 ||
                            details.guest_num <= 0 ||
                            strlen(details.date) != 8 ||
                            !isValidDate(&date_input, details.hour)){           
                            printf("Parametri errati. Sintassi: find <nome> <posti> <data> <ora>\n");
                            fflush(stdout);
                        } else 
                            handleFind(buffer, sd);
                    }
                    else if (!strcmp("book", command)){
                        sscanf(buffer, "%s %d", command, &booking_id);

                        if(!booking_id){
                            printf("Parametri errati. Sintassi: book <option>\n");
                            fflush(stdout);
                        } else
                            handleBook(buffer, sd, booking_id);
                    }
                    else if (!strcmp("esc", command))
                        handleEsc(sd);
                    else 
                        continue;
                } else {
                    // server has started its closing procedure
                    recv(sd, (void*)&len, sizeof(uint8_t), 0); // read message to unblock server (it would block otherwise)
                    handleEsc(sd);
                }
            }
        }
    }
}
