#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define BUF_LEN 1024
#define HANDSHAKE_LEN 2
#define ENDPOINTCODE_LEN 2
#define RESERVATION_CODE_LEN 7

// Specifying relative paths starting from root folder where project will be compiled my makefile
#define CONNECTIONS "./server/connections.txt"
#define RESERVATIONS "./server/reservations.txt"
#define TABLES "./server/tables.txt"
#define MENU "./server/menu.txt"
#define ORDERS "./server/orders/orders.txt"

#define MAX_TABLES 20

// ------------------------ UTILITIES ------------------------

// Removes a line from a file
void deleteLine(char* filename, int i){
    FILE* ptr = fopen(filename, "r");
    FILE* temp = fopen("./temp.txt", "w");
    int line = 0, c;
    char ch[1];

    while ((c = fgetc(ptr)) != EOF) {
        ch[0] = c;
        if (line != i)
            putc(ch[0], temp);
        if (ch[0] == '\n')
            line++;
    }

    fclose(ptr);
    fclose(temp);

    remove(filename);
    rename("./temp.txt", filename);
}

// Sends orders status to the table selected
void printTableStatus(char* table, int sd){
    FILE* fd_orders;
    char buffer[BUF_LEN];
    char table_id[10], comanda_id[10], orders[128], kitchen[10], dish[10];
    char comanda[256];
    char* ptr;
    uint8_t order_found, dish_quantity, msg_len;

    memset(buffer, 0, sizeof(buffer));
    buffer[0] = '\n';
    buffer[1] = '\0';

    fd_orders = fopen(ORDERS, "r");

    order_found = fscanf(fd_orders, "%*d %s %s %[^K]%s\n", table_id, comanda_id, orders, kitchen); 
    while (order_found != EOF && order_found != 255){

        // Check if order is related to the actual table
        if(!strcmp(table_id, table)){
            strcat(buffer, comanda_id);
            strcat(buffer, " ");
            if(!strcmp(kitchen, "K-"))
                strcat(buffer, "<in attesa>\n");
            else if(!strcmp(kitchen, "KK"))
                strcat(buffer, "<in servizio>\n");
            else 
                strcat(buffer, "<in preparazione>\n");

            ptr = strtok(orders, " ");
            while (ptr != NULL){
                sscanf(ptr, "%[^-]%*c%d", dish, &dish_quantity);
                ptr = strtok(NULL, " ");
                
                sprintf(comanda, "%s %d\n", dish, dish_quantity);
                strcat(buffer, comanda);
            }
        }

        order_found = fscanf(fd_orders, "%*d %s %s %[^K]%s\n", table_id, comanda_id, orders, kitchen); 
    }             
    
    msg_len = strlen(buffer);
    buffer[msg_len++] = '\0';
    send(sd, (void*)&msg_len, sizeof(uint8_t), 0);
    send(sd, (void*)buffer, msg_len, 0);
    fclose(fd_orders);
}

// Compares current datetime with the given one. 
// Returns 0 if equal, 1 otherwise
int compareDateTime(struct tm* tm_info, char* date, uint8_t hour){
    char cur_date[9];

    strftime(cur_date, sizeof(cur_date), "%d-%m-%y", tm_info);

    if (tm_info->tm_hour != hour) return 1;

    if(strcmp(cur_date, date) != 0) return 1;

    return 0;
}

// Checks the presence of a given dish in menu
// Returns 1 if present, 0 otherwise
int dishInMenu(char* dish){

    FILE* fd_menu = fopen(MENU, "r");
    uint8_t dish_found;
    char menu_dish[5];

    do
    {
        dish_found = fscanf(fd_menu, "%s %*[^\n]%*c", menu_dish);

        if(!strcmp(menu_dish, dish))
           return 1;

    } while (dish_found != EOF && dish_found != 255);

    fclose(fd_menu);

    return 0;
}

// ------------------------ UTILITIES ------------------------

// ------------------------ MAIN ------------------------
void main(int argc, char* argv[]){
    // Properties of a communication channel
    struct connection
    {
        int fd;         // file descriptor 
        char type;      // type of the device connected ('c', 't' or 'k')
        ushort step;    // step at which the device is stopped
        char table[5];  // table id if the client is a table device
    } connection;

    // Properties of a reservation
    // res_request -> incoming new request of reservation
    // res_booked -> reservation already booked 
    struct reservation
    {
        char id[7];             // id of reservation
        char table[5];          // id of the table reserved
        uint32_t fd;            // file descriptor of the table device who will handle this reservation
        char timestamp[26];     // timestamp of the reservation
        char name[24];          // name of the guest
        uint8_t guest_num;      // guest number
        char date[9];           // date of the reservation
        uint8_t hour;           // hour of the reservation
        uint8_t is_logged;      // signals if the reservation id has been inserted in a table device
    } res_request, res_booked;

    // Properties of a table 
    struct table 
    {
        char id[5];         // id of table
        uint8_t seats;      // number of seats 
        char room[24];      // name of the room
        char descr[24];     // description of the room
    }  temp_table;

    // Properties of an order (single dish inside a 'comanda' list of dishes)
    // order -> used when retrieving data from table device request
    // total_dish -> used when calculating final receipt after a 'conto' request
    struct order
    {
        char dish[4];       // name of the dish
        uint8_t quantity;   // quantity of the dish
        uint8_t price;      // price of the dish
    } order, total_dish;

    fd_set master;
    fd_set read_fds;
    int fdmax, 
        listener, 
        ret, 
        addrlen, 
        new_fd, 
        destination_fd,     // file descriptor of the td to contact when updating orders status (order shipped, order in preparation...)
        conn_found,
        table_found,
        res_found,
        dish_found,
        order_found,
        order_counter,
        occur_found,
        line_number = 0,    // number of lines read in connections.txt
        res_counter = 0,    // incremental id of reservations       
        res_number,         // number of lines read in reservations.txt
        order_added,
        receipt;
    uint8_t msg_len_8,
            orders_pending = 0;
    uint16_t msg_len;
    struct sockaddr_in my_addr, cl_addr;
    char buffer[BUF_LEN];       // main buffer
    char temp_buffer[128];      // working buffer
    char temp_buffer_2[128];    // working buffer
    char temp_buffer_3[128];    // working buffer
    char temp_buffer_4[128];    // working buffer
    char id_comanda[5];
    char *ptr;
    char ch;
    ushort port;

    FILE* fd_connections;
    FILE* fd_reservations;
    FILE* fd_proposals;
    FILE* fd_tables;
    FILE* fd_menu;
    FILE* fd_orders;
    FILE* fd_order;
    FILE* fd_temp;

    uint8_t proposal_option_i;      // index for proposals sent during 'book' procedure
    uint8_t num_order;              // incremental number of the order made from a specific client

    time_t timer;
    struct tm* tm_info;

    memset(buffer, 0, sizeof(buffer));

    if( argc != 2 ) {
        printf("One argument expected: <port>\n");
        exit(1);
    }

    port = (ushort)strtol(argv[1], NULL, 10); 
    listener = socket(AF_INET, SOCK_STREAM, 0);
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(port);
    my_addr.sin_addr.s_addr = INADDR_ANY;
    ret = bind(listener, (struct sockaddr*)&my_addr, sizeof(my_addr));
    
    if( ret < 0 ){
        perror("Bind non riuscita\n");
        exit(0);
    }

    listen(listener, 15);
   
    // Reset FDs
    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    FD_SET(listener, &master);
    FD_SET(0, &master);

    fdmax = listener;

    // Clear files
    fclose(fopen(CONNECTIONS, "w"));
    fclose(fopen(RESERVATIONS, "w"));
    fclose(fopen(ORDERS, "w"));

    // main loop
    while(1){

        fd_connections = fopen(CONNECTIONS, "a+");
        line_number = 0;

        read_fds = master;
        ret = select(fdmax+1, &read_fds, NULL, NULL, NULL);
        if(ret<0){
            perror("Errore durante la select:");
            exit(1);
        }

        int i;
        for(i = 0; i <= fdmax; i++) {
            if(FD_ISSET(i, &read_fds)) {

                // ------------------------ SERVER COMMANDS------------------------
                if(i == 0){
                    fgets(buffer, BUF_LEN, stdin);
                    memset(temp_buffer_4, 0, sizeof(temp_buffer_4));
                    sscanf(buffer, "%s %[^\n]%*c", temp_buffer, temp_buffer_4);

                    if (!strcmp(temp_buffer, "stat")){
                        if(strlen(temp_buffer_4)){
                            // status or table id
                            if( !strcmp(temp_buffer_4, "a") ||
                                !strcmp(temp_buffer_4, "p") ||
                                !strcmp(temp_buffer_4, "s")){
                                
                                fd_orders = fopen(ORDERS, "r");
                                memset(buffer, 0, sizeof(buffer));

                                while ((order_found = fscanf(fd_orders, "%*d %s %s %[^K]%s", temp_table.id, id_comanda, temp_buffer, temp_buffer_3)) != EOF){

                                    if((temp_buffer_4[0] == 'a' && !strcmp(temp_buffer_3, "K-")) ||
                                        (temp_buffer_4[0] == 's' && !strcmp(temp_buffer_3, "KK")) ||
                                        (temp_buffer_4[0] == 'p' && strcmp(temp_buffer_3, "K-") != 0 && strcmp(temp_buffer_3, "KK") != 0)){
                                        strcat(buffer, id_comanda);
                                        strcat(buffer, " ");
                                        strcat(buffer, temp_table.id);
                                        buffer[strlen(buffer)] = '\n';

                                        ptr = strtok(temp_buffer, " ");
                                        while (ptr != NULL){
                                            sscanf(ptr, "%[^-]%*c%d", order.dish, &order.quantity);
                                            ptr = strtok(NULL, " ");
                                            
                                            sprintf(temp_buffer_2, "%s %d\n", order.dish, order.quantity);
                                            strcat(buffer, temp_buffer_2);
                                        }
                                    }
                                }                 

                                printf("%s\n", buffer);
                                fflush(stdout);
                                fclose(fd_orders);

                            } else if (temp_buffer_4[0] == 'T'){

                                fd_orders = fopen(ORDERS, "r");
                                memset(buffer, 0, sizeof(buffer));

                                while ((order_found = fscanf(fd_orders, "%*d %s %s %[^K]%s", temp_table.id, id_comanda, temp_buffer, temp_buffer_3)) != EOF){
                                    // Check if order is related to the actual table
                                    if(!strcmp(temp_table.id, temp_buffer_4)){
                                        strcat(buffer, id_comanda);
                                        strcat(buffer, " ");
                                        if(!strcmp(temp_buffer_3, "K-"))
                                            strcat(buffer, "<in attesa>\n");
                                        else if(!strcmp(temp_buffer_3, "KK"))
                                            strcat(buffer, "<in servizio>\n");
                                        else 
                                            strcat(buffer, "<in preparazione>\n");

                                        ptr = strtok(temp_buffer, " ");
                                        while (ptr != NULL){
                                            sscanf(ptr, "%[^-]%*c%d", order.dish, &order.quantity);
                                            ptr = strtok(NULL, " ");
                                            
                                            sprintf(temp_buffer_2, "%s %d\n", order.dish, order.quantity);
                                            strcat(buffer, temp_buffer_2);
                                        }
                                    }
                                }             

                                printf("%s\n", buffer);
                                fflush(stdout);
                                fclose(fd_orders);
                            }

                        } else {
                            // no parameters
                            fd_orders = fopen(ORDERS, "r");
                            memset(buffer, 0, sizeof(buffer));

                            while ((order_found = fscanf(fd_orders, "%*d %s %s %[^K]%s", temp_table.id, id_comanda, temp_buffer, temp_buffer_3)) != EOF){

                                strcat(buffer, id_comanda);
                                strcat(buffer, " ");
                                strcat(buffer, temp_table.id);
                                if(!strcmp(temp_buffer_3, "K-"))
                                    strcat(buffer, " <in attesa>\n");
                                else if(!strcmp(temp_buffer_3, "KK"))
                                    strcat(buffer, " <in servizio>\n");
                                else 
                                    strcat(buffer, " <in preparazione>\n");

                                ptr = strtok(temp_buffer, " ");
                                while (ptr != NULL){
                                    sscanf(ptr, "%[^-]%*c%d", order.dish, &order.quantity);
                                    ptr = strtok(NULL, " ");
                                    
                                    sprintf(temp_buffer_2, "%s %d\n", order.dish, order.quantity);
                                    strcat(buffer, temp_buffer_2);
                                }
                            }             

                            printf("%s\n", buffer);
                            fflush(stdout);
                            fclose(fd_orders);
                        }

                    } else if (!strcmp(temp_buffer, "stop")){

                        // check if server can stop
                        // it can proceed only if there are no orders pending or in preparation
                        fd_orders = fopen(ORDERS, "r");
                        while ((order_found = fscanf(fd_orders, "%*d %*s %*s %*[^K]%s", temp_buffer)) != EOF){
                            if(strcmp(temp_buffer, "KK") != 0){
                                order_found = 1;
                                break;    
                            }
                        }         
                        fclose(fd_orders);
                        // -----------------------------------------
    
                        if (order_found == EOF){
                            fd_connections = fopen(CONNECTIONS, "r");

                            // send a termination code to all the devices
                            while ((conn_found = fscanf(fd_connections, "%d %c %d %s", &connection.fd, &connection.type, &connection.step, &connection.table)) != EOF){
                                // Client, Td, Kd will close connection when receiving message_length = 0 from server
                                msg_len_8 = 0;
                                ret = send(connection.fd, (void*)&msg_len_8, sizeof(uint8_t), 0);
                            }
                            
                            fclose(fd_connections);
                            exit(0);
                        }
                    }

                }
                // ------------------------ SERVER COMMANDS ------------------------

                // ------------------------ NEW SOCKET HANDLING ------------------------
                // New connection to be established
                else if(i == listener) {
                    addrlen = sizeof(cl_addr);
                    new_fd = accept(listener,(struct sockaddr *)&cl_addr, &addrlen);
                    FD_SET(new_fd, &master);
                    if(new_fd > fdmax){ fdmax = new_fd; }
                }
                // ------------------------ NEW SOCKET HANDLING ------------------------

                // ------------------------ SOCKET CONNECTED HANDLING ------------------------
                // If device is already connected
                else {
                    // Find device which socket is ready, otherwise manage handshake with device
                    do {
                        // Retrieve a connection log from file connections.txt
                        conn_found = fscanf(fd_connections, "%d %c %d %s", &connection.fd, &connection.type, &connection.step, &connection.table);

                        // ------------------------ DEVICE ALREADY HANDSHAKED ------------------------
                        // Device found
                        if (i == connection.fd){

                            // Receive Endpoint code from clients or Closing Connection signal
                            memset(buffer, 0, sizeof(buffer));
                            ret = recv(i, (void*)buffer, ENDPOINTCODE_LEN, 0);

                            if (ret == 0){
                                printf("CLOSING CONNECTION - {fd: %d, type: %c}\n", connection.fd, connection.type);
                                fflush(stdout);
                                close(i);
                                FD_CLR(i, &master);
                                fclose(fd_connections);
                                deleteLine(CONNECTIONS, line_number);
                                line_number = 0;
                                break;
                            }
              
                            // Check the role of the device which socket is ready
                            // - CLIENT ('C')
                            // - TABLE ('B')
                            // - KITCHEN ('K')
                            switch (connection.type)
                            {

                                // ------------------------ CLIENT DEVICE HANDLING ------------------------
                                case 'C':
                                    // CLIENT steps:
                                    // - 1) FIND ('F')
                                    // - 2) BOOK ('B')

                                    // Client can always perform a 'find' action 
                                    if (buffer[0] == 'F'){ 
                                        printf("[CLIENT %d] - find request\n", connection.fd);
                                        fflush(stdout);

                                        // Receive reservation details from client 
                                        recv(i, (void*)&msg_len, sizeof(uint16_t), 0);
                                        msg_len = ntohs(msg_len);
                                        ret = recv(i, (void*)buffer, msg_len, 0);
                                        
                                        ret = connection.fd;
                                        sscanf(buffer, "%s %d %s %d", res_request.name, &res_request.guest_num, res_request.date, &res_request.hour);
                                        connection.fd = ret;
                                        // Proposal, reservation and tables opening
                                        sprintf(buffer, "./server/proposals/P%d.txt", connection.fd);

                                        fd_proposals = fopen(buffer, "w");
                                        fd_reservations = fopen(RESERVATIONS, "r");
                                        fd_tables = fopen(TABLES, "r");

                                        //Populate proposals with tables with enough seats and with no reservations already booked
                                        proposal_option_i = 0;
                                        buffer[0] = '\0';
                                        fprintf(fd_proposals, "%s %d %s %d\n", res_request.name, res_request.guest_num, res_request.date, res_request.hour);
                                        do
                                        {
                                            table_found = fscanf(fd_tables, "%s %d %s %s", temp_table.id, &temp_table.seats, temp_table.room, temp_table.descr);
                                            if(table_found == EOF) break;

                                            if (temp_table.seats >= res_request.guest_num){
                                                do
                                                {
                                                    res_found = fscanf(fd_reservations, "%s %s %s %d %s %d %d %*[^\n]", res_booked.id, res_booked.table, res_booked.name, &res_booked.guest_num, res_booked.date, &res_booked.hour, &res_booked.fd, res_booked.timestamp);

                                                    if (!strcmp(temp_table.id, res_booked.table) && 
                                                        !strcmp(res_request.date, res_booked.date) && 
                                                        (res_request.hour == res_booked.hour)){
                                                        res_found = 1;
                                                        break;
                                                        }
                                                } while (res_found != EOF);

                                                if (res_found == EOF){
                                                    proposal_option_i++;
                                                    fprintf(fd_proposals, "%d) %s %s %s\n", proposal_option_i, temp_table.id, temp_table.room, temp_table.descr);
                                                    if(i == 1)
                                                        sprintf(buffer, "%d) %s %s %s\n", proposal_option_i, temp_table.id, temp_table.room, temp_table.descr);
                                                    else {
                                                        sprintf(temp_buffer, "%d) %s %s %s\n", proposal_option_i, temp_table.id, temp_table.room, temp_table.descr);
                                                        strcat(buffer, temp_buffer);
                                                    }
                                                }
                                            }
                                        } while (table_found != EOF);

                                        fclose(fd_tables);
                                        fclose(fd_reservations);
                                        fclose(fd_proposals);

                                        // Send to client proposals
                                        msg_len = strlen(buffer);
                                        // Empty set
                                        msg_len = htons(msg_len);
                                        ret = send(i, (void*)&msg_len, sizeof(uint16_t), 0);
                                        
                                        if (msg_len != 0){
                                            ret = send(i, (void*)buffer, ntohs(msg_len), 0);
                                        
                                            // Update of the connection status
                                            connection.step = 2;
                                            fprintf(fd_connections, "%d %c %d %s\n", connection.fd, connection.type, connection.step, connection.table);
                                            fclose(fd_connections);
                                            deleteLine(CONNECTIONS, line_number);
                                            line_number = 0;
                                        } else {
                                            fclose(fd_connections);
                                        }

                                    } 
                                    // Client can perform a 'book' action only at this step
                                    else if (connection.step == 2 && buffer[0] == 'B'){   
                                        printf("[CLIENT %d] - book request\n", connection.fd);
                                        fflush(stdout);

                                        // Receive back index of proposal chosen by client
                                        ret = recv(i, (void*)&proposal_option_i, sizeof(uint8_t), 0);

                                        sprintf(buffer, "./server/proposals/P%d.txt", connection.fd);
                                        fd_proposals = fopen(buffer, "r");
                                        
                                        // retrieve first line with request details
                                        fscanf(fd_proposals, "%s %d %s %d\n", res_request.name, &res_request.guest_num, res_request.date, &res_request.hour);

                                        // retrieve proposal selected by client
                                        // parameter needed is just res_request.table and temp_table.room
                                        // temp_table.seats is just used to store the current id of the proposal, to check later the loop
                                        short j;
                                        for (j = 0; j < proposal_option_i; j++)
                                            fscanf(fd_proposals, "%d) %s %s %s\n", &temp_table.seats, res_request.table, temp_table.room, temp_table.descr);
                                        
                                        if (temp_table.seats < proposal_option_i){
                                            sprintf(temp_buffer, "000000");
                                            ret = send(i, (void*)temp_buffer, RESERVATION_CODE_LEN, 0);
                                            fclose(fd_proposals);
                                            
                                            break;
                                        }

                                        // check if in the meantime other clients have booked the same table
                                        fd_reservations = fopen(RESERVATIONS, "r+");

                                        do
                                        {   
                                            res_found = fscanf(fd_reservations, "%s %s %s %d %s %d %d %[^\n]\n",
                                                                res_booked.id, 
                                                                res_booked.table, 
                                                                res_booked.name,
                                                                &res_booked.guest_num,
                                                                res_booked.date,
                                                                &res_booked.hour,
                                                                &res_booked.fd,
                                                                res_booked.timestamp);
                                            if(res_found != EOF){
                                                if (!strcmp(res_booked.table, res_request.table) &&
                                                    !strcmp(res_booked.date, res_request.date) &&
                                                    res_request.hour == res_booked.hour){
                                                    break;
                                                }
                                            } else
                                                break;
                                        } while (res_found != EOF);

                                        if (res_found == EOF){
                                            sprintf(res_request.id, "%d", (++res_counter));

                                            timer = time(NULL);
                                            tm_info = localtime(&timer);
                                            strftime(res_request.timestamp, 26, "%Y-%m-%d %H:%M:%S", tm_info);
                                       
                                            fprintf(fd_reservations, "%s %s %s %d %s %d %d %s\t%d\n",
                                                                    res_request.id, 
                                                                    res_request.table, 
                                                                    res_request.name,
                                                                    res_request.guest_num,
                                                                    res_request.date,
                                                                    res_request.hour,
                                                                    connection.fd,
                                                                    res_request.timestamp,
                                                                    0
                                                                    );
                                            
                                            ret = send(i, (void*)res_request.id, RESERVATION_CODE_LEN, 0);

                                            // send table id
                                            msg_len_8 = strlen(res_request.table);
                                            ret = send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                            ret = send(i, (void*)res_request.table, msg_len_8, 0);
                                            
                                            msg_len_8 = strlen(temp_table.room);
                                            ret = send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                            ret = send(i, (void*)temp_table.room, msg_len_8, 0);

                                            fclose(fd_proposals);
                                            sprintf(buffer, "./server/proposals/P%d.txt", connection.fd);
                                            remove(buffer);
                                        } else {
                                            sprintf(temp_buffer, "000000");
                                            ret = send(i, (void*)temp_buffer, RESERVATION_CODE_LEN, 0);
                                            fclose(fd_proposals);
                                        }

                                        fclose(fd_reservations);

                                    } else {
                                        printf("[CLIENT %d] - invalid request rejected\n", connection.fd);
                                        fflush(stdout);

                                        // If request is rejected because user client not follow the expected steps 
                                        if (buffer[0] == 'B'){
                                            sprintf(temp_buffer, "000000");
                                            send(i, (void*)temp_buffer, RESERVATION_CODE_LEN, 0);
                                        }
                                    }
 
                                    break;
                                // ------------------------ CLIENT DEVICE HANDLING ------------------------

                                // ------------------------ TABLE DEVICE HANDLING ------------------------
                                case 'T':
                                    // TABLE steps:
                                    // - 1) LOGIN ('L')
                                    // - 2) MENU ('M')/COMANDA('C')
                                    // - 3) CONTO ('R')

                                    // ------------------------ TABLE > [LOGIN] ------------------------
                                    if (buffer[0] == 'L' && connection.step == 1){
                                        printf("[TABLE CLIENT %d] - login request\n", connection.fd);
                                        fflush(stdout);

                                        // Receive login details from table 
                                        recv(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                        ret = recv(i, (void*)buffer, msg_len_8, 0);

                                        // Scan reservations to check reservation code and retrieve table id
                                        fd_reservations = fopen(RESERVATIONS, "a+");
                                        res_number = 0;
                                        do {
                                            res_found = fscanf(fd_reservations, "%s %s %s %d %s %d %d %[^\t]%*c%d\n",
                                                                res_booked.id, 
                                                                res_booked.table, 
                                                                res_booked.name,
                                                                &res_booked.guest_num,
                                                                res_booked.date,
                                                                &res_booked.hour,
                                                                &res_booked.fd,
                                                                res_booked.timestamp,
                                                                &res_booked.is_logged);
                                            if(res_found != EOF){
                                                if (!strcmp(res_booked.id, buffer)){
                                                    if(res_booked.is_logged == 1){
                                                        res_found = EOF;
                                                        fclose(fd_reservations);
                                                    }
                                                    else {
                                                        // must update is_logged field to 1
                                                        fprintf(fd_reservations, "%s %s %s %d %s %d %d %s\t%d\n",
                                                                res_booked.id, 
                                                                res_booked.table, 
                                                                res_booked.name,
                                                                res_booked.guest_num,
                                                                res_booked.date,
                                                                res_booked.hour,
                                                                res_booked.fd,
                                                                res_booked.timestamp,
                                                                1);
                                                        fclose(fd_reservations);
                                                        deleteLine(RESERVATIONS, res_number);
                                                    }
                                                    break;
                                                }
                                            } else
                                                break;
                                            res_number++;
                                        } while (res_found != EOF);

                                        // Check validity of the reservation code
                                        tm_info = localtime(&timer);

                                        // msg_len_8 is only used for storing the integer number, not to indicate the message length
                                        if (res_found == EOF || 
                                            (res_found != EOF && compareDateTime(tm_info, res_booked.date, res_booked.hour))){
                
                                            msg_len_8 = 0; // Reservation code not found
                                            printf("[TABLE CLIENT %d] - login request rejected\n", connection.fd);
                                            fflush(stdout);
                                        } else {
                                            msg_len_8 = 1; // Reservation code found
                                            printf("[TABLE CLIENT %d] - login request accepted\n", connection.fd);
                                            fflush(stdout);
                                        }

                                        // Send status to table [1 - valid code, 0 - invalid code ]
                                        send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);

                                        if (msg_len_8){
                                            // Update of the connection status
                                            connection.step = 2;
                                            strcpy(connection.table, res_booked.table);
                                            fprintf(fd_connections, "%d %c %d %s\n", connection.fd, connection.type, connection.step, connection.table);
                                            fclose(fd_connections);
                                            deleteLine(CONNECTIONS, line_number);
                                            line_number = 0;
                                        } 

                                    // ------------------------ TABLE > [LOGIN] ------------------------

                                    // ------------------------ TABLE > [MENU] ------------------------
                                    } else if(buffer[0] == 'M' && connection.step >= 2){
                                        printf("[TABLE CLIENT %d] - menu request\n", connection.fd);
                                        fflush(stdout);
                                        
                                        fd_menu = fopen(MENU, "r");
                                        memset(buffer, 0,BUF_LEN);
                                        do
                                        {
                                            dish_found = fscanf(fd_menu, "%[^\n]\n", temp_buffer);
                                            if (dish_found != EOF){
                                                strcat(buffer, temp_buffer);
                                                buffer[strlen(buffer)] = '\n';
                                            }
                                        } while (dish_found != EOF);
                                
                                        msg_len = htons(strlen(buffer));
                                        send(i, (void*)&msg_len, sizeof(uint16_t), 0);
                                        send(i, (void*)buffer, ntohs(msg_len), 0);

                                        fclose(fd_menu);
                                    // ------------------------ TABLE > [MENU] ------------------------

                                    // ------------------------ TABLE > [COMANDA] ------------------------
                                    } else if(buffer[0] == 'C' && connection.step >= 2){
                                        printf("[TABLE CLIENT %d] - comanda request\n", connection.fd);
                                        fflush(stdout);

                                        // receive order incremental id
                                        recv(i, (void*)&num_order, sizeof(uint8_t), 0);

                                        // must append in a ad hoc file the orders
                                        sprintf(buffer, "./server/orders/O%d.txt", connection.fd);
                                        fd_order = fopen(buffer, "a");

                                        order_added = 0;
                                        memset(buffer, 0, BUF_LEN);
                                        memset(temp_buffer, 0, sizeof(temp_buffer));
                                        memset(temp_buffer_2, 0, sizeof(temp_buffer_2));
                                      
                                        // must append the new order to orders file to let kitchen handle it
                                        fd_orders = fopen(ORDERS, "a");
                                        sprintf(temp_buffer, "%d %s com%d ", connection.fd, connection.table, num_order);
                                        do
                                        {
                                            // Get msg_len_8
                                            recv(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                            recv(i, (void*)buffer, msg_len_8, 0);

                                            // If received termination code
                                            if (buffer[0] == '0'){
                                                break;
                                            }

                                            sscanf(buffer, "%[^-]%*c%d", order.dish, &order.quantity);

                                            if (dishInMenu(order.dish)){
                                                order_added = 1;
                                                fprintf(fd_order, "%s %d\n", order.dish, order.quantity);
                                                sprintf(temp_buffer_2, "%s-%d ", order.dish, order.quantity);
                                                strcat(temp_buffer, temp_buffer_2);
                                            }
                                        } while (1);

                                        if (order_added){
                                            sprintf(temp_buffer_2, "K-\n");
                                            strcat(temp_buffer, temp_buffer_2);
                                            fprintf(fd_orders, "%s", temp_buffer);
                                            orders_pending++;

                                            fclose(fd_order);
                                            fclose(fd_orders);
                                       
                                            // broadcast to kitchen device that a new order has been added to queue
                                            rewind(fd_connections);
                                            while((conn_found = fscanf(fd_connections,  "%d %c %*[^\n]%*c", &destination_fd, &ch)) != EOF){
                                                if (ch == 'K'){
                                                    send(destination_fd, (void*)&orders_pending, sizeof(uint8_t), 0);
                                                }
                                            }
                                            conn_found = 1; // set to prevent addition of a new fake connection
                                            // ------------------------------------------
                                       
                                            // Update of the connection status
                                            connection.step = 3;
                                            fprintf(fd_connections, "%d %c %d %s\n", connection.fd, connection.type, connection.step, connection.table);
                                            fclose(fd_connections);
                                            deleteLine(CONNECTIONS, line_number);
                                            line_number = 0;

                                            msg_len_8 = 1;
                                            send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);

                                            printTableStatus(connection.table, connection.fd);                                       
                                        } else {
                                            msg_len_8 = 0;
                                            send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);

                                            fclose(fd_connections);
                                            fclose(fd_order);
                                            fclose(fd_orders);
                                        }
                                    
                                    // ------------------------ TABLE > [COMANDA] ------------------------

                                    // ------------------------ TABLE > [CONTO] ------------------------
                                    } else if(buffer[0] == 'R' && connection.step == 3){
                                        printf("[TABLE CLIENT %d] - conto request\n", connection.fd);
                                        fflush(stdout);

                                        fd_orders = fopen(ORDERS, "r");

                                        memset(temp_buffer, 0, sizeof(temp_buffer));
                                        do
                                        {
                                            order_found = fscanf(fd_orders, "%*d %s %*[^K]%s", temp_buffer, temp_buffer_2);
                                            if (!strcmp(temp_buffer, connection.table) && (strcmp(temp_buffer_2, "KK") != 0)){
                                                order_found = 1;
                                                break;
                                            }
                                        } while (order_found != EOF);
                                        
                                        fclose(fd_orders);

                                        // If there is still an order pending related to the table then request is rejected
                                        if (order_found != EOF){
                                            msg_len_8 = 0;
                                            send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                            break;
                                        } else {

                                            sprintf(buffer, "./server/orders/O%d.txt", connection.fd);
                                            fd_menu = fopen(MENU, "r");

                                            receipt = 0;

                                            do
                                            {
                                                fd_order = fopen(buffer, "r+");
                                                order_found = fscanf(fd_order, "%s %d\n", order.dish, &order.quantity);
                                                
                                                if(order_found == EOF)
                                                    break;
                                                
                                                fd_temp = fopen("./server/orders/temp.txt", "w");

                                                // sum all the quantities of the same dish ordered in different orders
                                                strcpy(total_dish.dish, order.dish);
                                                total_dish.quantity = order.quantity;

                                                while (fscanf(fd_order, "%s %d\n",order.dish, &order.quantity) != EOF){
               
                                                    if(!strcmp(total_dish.dish, order.dish)){
                                                        total_dish.quantity += order.quantity;
                                                    } else {
                                                        fprintf(fd_temp, "%s %d\n", order.dish, order.quantity);
                                                    }
                                                }

                                                fclose(fd_order);
                                                fclose(fd_temp);
                                                remove(buffer);
                                                rename("./server/orders/temp.txt", buffer);
                                                
                                                rewind(fd_menu);
                                                do
                                                {
                                                    dish_found = fscanf(fd_menu, "%s %*[^0-9]%d", temp_buffer, &order.price);

                                                    if(!strcmp(temp_buffer, total_dish.dish)){
                                                        receipt += order.price*total_dish.quantity; 
                                                        break;
                                                    }
                                                } while (dish_found != EOF);
                                                
                                                // Send order to table
                                                memset(temp_buffer, 0, sizeof(temp_buffer));
                                                sprintf(temp_buffer, "%s %d %d\n", total_dish.dish, total_dish.quantity, order.price*total_dish.quantity);

                                                msg_len_8 = strlen(temp_buffer);
                                                send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                                send(i, (void*)temp_buffer, msg_len_8, 0);
                                            
                                            } while (order_found != EOF);
                                            
                                            fclose(fd_menu);

                                            // Remove all orders related to this table device 
                                            fd_orders = fopen(ORDERS, "r");
                                            fd_temp = fopen("./server/orders/temp.txt", "w");

                                            memset(temp_buffer, 0, sizeof(temp_buffer));
                                            do
                                            {
                                                order_found = fscanf(fd_orders, "%[^\n]%*c", temp_buffer);
                                                sscanf(temp_buffer, "%*d %s %*[^\n]%*c", temp_buffer_2);

                                                if(order_found != EOF){
                                                    if (strcmp(temp_buffer_2, connection.table) != 0){
                                                        fprintf(fd_temp, "%s\n", temp_buffer);
                                                    }
                                                }

                                            } while (order_found != EOF);

                                            fclose(fd_orders);
                                            fclose(fd_temp);
                                            remove(ORDERS);
                                            rename("./server/orders/temp.txt", ORDERS);
                                            // -----------------------------------------------
                                        }
            
                                        sprintf(temp_buffer, "Totale: %d\n", receipt);
                                        msg_len_8 = strlen(temp_buffer);
                                        send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                        send(i, (void*)temp_buffer, msg_len_8, 0);

                                        // send finish code to table device
                                        msg_len_8 = 0;
                                        send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                        
                                    // ------------------------ TABLE > [CONTO] ------------------------
                                    
                                    // ------------------------ TABLE > [] ------------------------
                                    } else {
                                        printf("[TABLE CLIENT %d] - invalid request rejected\n", connection.fd);
                                        fflush(stdout);

                                        // If request is rejected because user client not follow the expected steps 
                                        if(buffer[0] == 'R'){
                                            msg_len_8 = 0;
                                            send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                        }
                                    }
                                    // ------------------------ TABLE > [] ------------------------
                                    
                                    break;
                                // ------------------------ TABLE DEVICE HANDLING ------------------------

                                // ------------------------ KITCHEN DEVICE HANDLING ------------------------
                                case 'K':
                                    // KITCHEN api:
                                    // - TAKE (T)
                                    // - SHOW ('S')
                                    // - READY ('R')

                                    if (buffer[0] == 'T'){
                                        printf("[KITCHEN CLIENT %d] - take request\n", connection.fd);
                                        fflush(stdout);

                                        fd_orders = fopen(ORDERS, "a+");

                                        memset(temp_buffer, 0, sizeof(temp_buffer));
                                        memset(temp_buffer_2, 0, sizeof(temp_buffer_2));
                                        memset(temp_buffer_3, 0, sizeof(temp_buffer_3));
                                        memset(buffer, 0, sizeof(buffer));
                                        order_counter = 0;
                                        do
                                        {
                                            order_found = fscanf(fd_orders, "%d %s %s %[^\n]s\n", &destination_fd, temp_table.id, id_comanda, buffer);
                                            
                                            if (order_found == EOF)
                                                break;

                                            // retrieve kitchen client fd
                                            sscanf(buffer, "%[^K]%s",temp_buffer_3,temp_buffer);

                                            // first order (FIFO) must be taken by the kitchen device
                                            if(!strcmp(temp_buffer, "K-")){
                                                strcpy(temp_buffer, buffer);

                                                memset(buffer, 0, sizeof(buffer));

                                                strcpy(buffer, id_comanda);
                                                strcat(buffer, " ");
                                                strcat(buffer, temp_table.id);
                                                buffer[strlen(buffer)] = '\n';

                                                ptr = strtok(temp_buffer, " ");
                                                while (ptr != NULL){
                                                    sscanf(ptr, "%[^-]%*c%d", order.dish, &order.quantity);
                                                    ptr = strtok(NULL, " ");
                                                    
                                                    if(order.dish[0] != 'K'){
                                                        sprintf(temp_buffer_2, "%s %d\n", order.dish, order.quantity);
                                                        strcat(buffer, temp_buffer_2);
                                                    } else{
                                                        break;                                                   
                                                    }
                                                }
                                                
                                                msg_len_8 = strlen(buffer);
                                                send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                                send(i, (void*)buffer,msg_len_8,0);
                                                
                                                // Update order record in orders.txt and add K[fd]
                                                fprintf(fd_orders, "%d %s %s %sK%d\n", destination_fd, temp_table.id, id_comanda, temp_buffer_3, connection.fd);
                                                fclose(fd_orders);
                                                deleteLine(ORDERS, order_counter);

                                                // Notify table device
                                                printTableStatus(temp_table.id, destination_fd);

                                                break;
                                            }

                                            order_counter++;
                                        } while (order_found != EOF);
                                        
                                        if (order_found == EOF){
                                            // Send error code if no order is pending
                                            msg_len_8 = 0;
                                            send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                            fclose(fd_orders);
                                        } else {
                                            // broadcast to all kitchen device that an order has been taken
                                            orders_pending--;
                                            if(orders_pending){
                                                rewind(fd_connections);
                                                while((conn_found = fscanf(fd_connections,  "%d %c %*[^\n]%*c", &destination_fd, &ch)) != EOF){
                                                    if (ch == 'K'){
                                                        send(destination_fd, (void*)&orders_pending, sizeof(uint8_t), 0);
                                                    }
                                                }
                                                conn_found = 1; // set to prevent addition of a new fake connection
                                            }
                                            // ------------------------------------------
                                        }

                                    } else if(buffer[0] == 'S'){
                                        printf("[KITCHEN CLIENT %d] - show request\n", connection.fd);
                                        fflush(stdout);

                                        fd_orders = fopen(ORDERS, "r");
                                        memset(temp_buffer, 0, sizeof(temp_buffer));
                                        memset(temp_buffer_2, 0, sizeof(temp_buffer_2));
                                        memset(buffer, 0, sizeof(buffer));
                                        buffer[0] = '\0';

                                        // Scan all the orders and check if are correspondent to the kitchen device (e.g. connection.fd=2 --> K2)
                                        do
                                        {
                                            order_found = fscanf(fd_orders, "%*d %s %s %[^\n]s\n", temp_table.id, id_comanda, temp_buffer_3);
                                            
                                            if (order_found == EOF)
                                                break;

                                            sscanf(temp_buffer_3, "%*[^K]%s",temp_buffer);
                                            sprintf(temp_buffer_2, "K%d", connection.fd);

                                            // Check if connection.fd = K[fd]
                                            if(!strcmp(temp_buffer, temp_buffer_2)){
                                                
                                                // Build the order string (com1 T1\n A1-2 D1-2\n)
                                                strcpy(temp_buffer, temp_buffer_3);
                                                memset(temp_buffer_3, 0, sizeof(temp_buffer_3));
                                                strcpy(temp_buffer_3, id_comanda);
                                                strcat(temp_buffer_3, " ");
                                                strcat(temp_buffer_3, temp_table.id);
                                                temp_buffer_3[strlen(temp_buffer_3)] = '\n';

                                                ptr = strtok(temp_buffer, " ");
                                                while (ptr != NULL){
                                                    sscanf(ptr, "%[^-]%*c%d", order.dish, &order.quantity);
                                                    ptr = strtok(NULL, " ");

                                                    if(order.dish[0] != 'K'){
                                                        sprintf(temp_buffer_2, "%s %d\n", order.dish, order.quantity);
                                                        strcat(temp_buffer_3, temp_buffer_2);
                                                    } else
                                                        break;                                                   
                                                }

                                                strcat(buffer, temp_buffer_3);
                                            }
                                        } while (order_found != EOF);

                                        msg_len_8 = strlen(buffer);
                                        send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                        if(msg_len_8)
                                            send(i, (void*)buffer, msg_len_8, 0);
                                        fclose(fd_orders);

                                    } else if(buffer[0] == 'R'){
                                        printf("[KITCHEN CLIENT %d] - ready request\n", connection.fd);
                                        fflush(stdout);

                                        recv(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                        recv(i, (void*)buffer, msg_len_8, 0);
                                        
                                        sscanf(buffer, "%[^-]%*c%s", temp_buffer, temp_buffer_2);

                                        // sanitization
                                        msg_len_8 = 0;
                                        if(!strlen(temp_buffer) || !strlen(temp_buffer_2)){
                                            send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                            break;
                                        }

                                        fd_orders = fopen(ORDERS, "a+");
                                        order_counter = 0;
                                        sprintf(temp_buffer_3, "K%d", connection.fd);

                                        do
                                        {
                                            order_found = fscanf(fd_orders, "%d %s %s %[^K]%s", &destination_fd, temp_table.id, id_comanda, temp_buffer_4, buffer);
                                            
                                            if(order_found == EOF){
                                                fclose(fd_orders);
                                                send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                                break;
                                            }

                                            if (!strcmp(temp_table.id, temp_buffer_2) && !strcmp(id_comanda, temp_buffer) && !strcmp(buffer, temp_buffer_3)){

                                                fprintf(fd_orders, "%d %s %s %sKK\n", destination_fd, temp_table.id, id_comanda, temp_buffer_4);
                                                fclose(fd_orders);
                                                deleteLine(ORDERS, order_counter);

                                                msg_len_8 = 1;
                                                send(i, (void*)&msg_len_8, sizeof(uint8_t), 0);
                                                break;
                                            }

                                            order_counter++;
                                        } while (order_found != EOF);

                                        printTableStatus(temp_table.id, destination_fd);

                                    } else {
                                        printf("[KITCHEN CLIENT %d] - invalid request rejected\n", connection.fd);
                                        fflush(stdout);
                                    }
                                        
                                    break;
                                // ------------------------ KITCHEN DEVICE HANDLING ------------------------
                                
                                default:
                                    break;
                            }

                            // Stop when communication with client has finished 
                            break;
                        }

                        line_number++;
                        // ------------------------ DEVICE ALREADY HANDSHAKED ------------------------

                    } while (conn_found != EOF);

                    // ------------------------ DEVICE NOT YET HANDSHAKED ------------------------
                    // Manage handshake with new device
                    if (conn_found == EOF){
                        ret = recv(i, (void*)buffer, HANDSHAKE_LEN, 0);
                        
                        if (ret < 0){
                            close(i);
                            FD_CLR(i, &master);
                        } else {
                            printf("ESTABLISHING CONNECTION - {fd: %d, type: %c}\n", i, buffer[0]);
                            fflush(stdout);
                            fprintf(fd_connections, "%d %c %d %s\n", i, buffer[0], 1, "-");
                            fclose(fd_connections);
                        }
                    }   
                    // ------------------------ DEVICE NOT YET HANDSHAKED ------------------------
                }

                // ------------------------ SOCKET CONNECTED HANDLING ------------------------
            }
        }

        // fclose(fd_connections);
    }
    printf("\nChiusura del server...\n");
    fflush(stdout);
    close(listener);
}

// ------------------------ MAIN ------------------------