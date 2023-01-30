#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h> 
#include <errno.h>
#include <wchar.h>
#include <stdio.h>

#define MAX_PLAYER_COUNT   2
#define BUFFER_SIZE        1024
#define ERROR              -1
#define ALL                -1

#define WHITE              0
#define BLACK              1

#define ROWS               8
#define COLS               8

#define EMPTY              0x0020 // 
#define WHITE_KING         0x2654 // ♔
#define WHITE_QUEEN        0x2655 // ♕
#define WHITE_ROOK         0x2656 // ♖
#define WHITE_BISHOP       0x2657 // ♗
#define WHITE_KNIGHT       0x2658 // ♘
#define WHITE_PAWN         0x2659 // ♙
#define BLACK_KING         0x265A // ♚
#define BLACK_QUEEN        0x265B // ♛
#define BLACK_ROOK         0x265C // ♜
#define BLACK_BISHOP       0x265D // ♝
#define BLACK_KNIGHT       0x265E // ♞
#define BLACK_PAWN         0x265F // ♟

typedef struct player {
    int sock;
    int lost;
} Player;

void intro();
void createBoard();
void removeBoard();
void initiateBoard();
void parseMoveCommand(char*, int*, int*, int*, int*);
bool isValidCommand();
void movePiece(int, int, int, int);
bool isPromotionPossible(int, int);
void promote(char*, int, int, int);
void whoIsNext(int, int);
void sendBoard(int);
bool isEmpty(int, int);
bool isOwnPiece(int, int, int);
bool isOpponentPiece(int, int, int);
void post(int, char*);
void debugBoard();
bool didAnyoneLose();
void gameover();

Player PLAYER[2];
wchar_t** BOARD;

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("usage - %s <port>\n", argv[0]);
        return ERROR;
    }

    setlocale(LC_ALL, "");

    char header, message[BUFFER_SIZE];
    char* msg;
    int server_sock, client_sock, str_len, server_opt, someone, color;
    int from_y, from_x, to_y, to_x, prom_y, prom_x;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_addr_size;
 
    struct epoll_event event;
    struct epoll_event* events;
    int epfd, event_count;

    int player_count = 0;
    bool is_game_start = false;
    bool is_running = true;
    bool is_king_dead = false;

    // allocate epoll events
    events = malloc(sizeof(struct epoll_event) * 50);

    // create & initiate the board
    intro();
    createBoard();
    initiateBoard();

    // socket()
    server_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == ERROR) {
        printf("ERR - socket()\n");
        return ERROR;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(atoi(argv[1]));

    // prevent bind() err
    server_opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &server_opt, sizeof(server_opt));

    // bind()
    if(bind(server_sock, (struct sockaddr*) &server_addr, sizeof(server_addr)) == ERROR) {
        printf("ERR - bind()\n");
        return ERROR;
    }

    // listen()
    if (listen(server_sock, 5) == ERROR) {
        printf("ERR - listen()\n");
        return ERROR;
    }

    client_addr_size = sizeof(client_addr);

    epfd = epoll_create(50);
    event.events = EPOLLIN;
    event.data.fd = server_sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_sock, &event);

    // run
    while (is_running) {
        event_count = epoll_wait(epfd, events, 50, -1);

        if (event_count == ERROR) {
            is_running = false;
            printf("ERR - epoll_wait()\n");
            continue;
        }

        for (int e = 0; e < event_count; e++) {
            // if the socket is the server socket
            if(events[e].data.fd == server_sock) {
                client_sock = accept(server_sock, (struct sockaddr*) &client_addr, &client_addr_size);
                if (client_sock == ERROR) {
                    is_running = false;
                    printf("ERR - accept()\n");
                    break;
                }
                
                printf("%d is connected!!\n", client_sock);

                event.events = EPOLLIN;
                event.data.fd = client_sock;
                epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &event);

                player_count++;

                if (player_count <= MAX_PLAYER_COUNT) {
                    // player settings
                    PLAYER[player_count - 1].sock = client_sock;
                    PLAYER[player_count - 1].lost = false;

                    if (player_count == 1) {
                        // send color & wait
                        post(PLAYER[WHITE].sock, "S The white is yours.");
                        sendBoard(PLAYER[WHITE].sock);
                    }
                    else if (player_count == MAX_PLAYER_COUNT) {
                        // send color & start soon
                        post(PLAYER[BLACK].sock, "S The black is yours.");
                        sendBoard(PLAYER[BLACK].sock);
                        sleep(3);
                    
                        post(PLAYER[WHITE].sock, "S The game'll start soon.");
                        post(PLAYER[BLACK].sock, "S The game'll start soon.");                        
                        sleep(2);

                        post(PLAYER[WHITE].sock, "S Start!!");
                        post(PLAYER[BLACK].sock, "S Start!!");      
                        sleep(1);

                        is_game_start = true;

                        // send game start message, the white piece first
                        whoIsNext(PLAYER[BLACK].sock, BLACK);
                    }
                }
                else {
                    // send failed to connect
                    post(client_sock, "F The game is already running");
                    printf("But the game is already running. export %d from the server.\n", client_sock);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, server_sock, NULL);
                    close(client_sock);
                    player_count--;
                }
            }
            // the socket is the client socket
            else {
                someone = events[e].data.fd;
                color = PLAYER[WHITE].sock == someone ? WHITE : BLACK;

                memset(message, 0, sizeof(char) * BUFFER_SIZE); // clear
                str_len = recv(someone, message, BUFFER_SIZE, 0);
                if (str_len == 0) {
                    // player's out
                    printf("%d player's out!!\n", someone);

                    PLAYER[color].lost = true;
                    
                    gameover();
                    
                    epoll_ctl(epfd, EPOLL_CTL_DEL, someone, NULL);
                    close(someone);
                } 

                // printf("%d - %s\n", someone, message); // for debugging

                if (is_game_start && (PLAYER[WHITE].sock == someone || PLAYER[BLACK].sock == someone)) {
                    // split
                    header = message[0];
                    msg = message;
                    msg = msg + 2;

                    if (header == 'C') { // e.g. 2224
                        parseMoveCommand(msg, &from_y, &from_x, &to_y, &to_x); // e.g. from_y = 2 from_x = 2 to_y = 4 to_x = 2
                        if (isValidCommand(color, from_y, from_x, to_y, to_x)) { // e.g. if the command is valid, send successful msg & move
                            if (BOARD[to_y][to_x] == BLACK_KING || BOARD[to_y][to_x] == WHITE_KING) {
                                is_king_dead = true;
                                if (color == WHITE)
                                    PLAYER[BLACK].lost = true;
                                else 
                                    PLAYER[WHITE].lost = true;
                            }

                            movePiece(from_y, from_x, to_y, to_x); // move

                            if (!is_king_dead && isPromotionPossible(to_y, to_x)) { // e.g. the player can promote the pawn, send the request
                                prom_y = to_y;
                                prom_x = to_x;

                                post(someone, "P What do you wanna change it to?");

                                continue;
                            }

                            // send the players the board 
                            sendBoard(ALL);
                            debugBoard();

                            if (!is_king_dead)
                                whoIsNext(someone, color); // next
                        }
                        else { //  the command is invalid, send try again
                            post(someone, "R The invalid Command, Try again.");
                        }

                    }
                    else if (header == 'P') { // change the pawn & send the board & change the turn
                        promote(msg, color, prom_y, prom_x);

                        sendBoard(ALL);
                        debugBoard();
                        
                        whoIsNext(someone, color); 
                    }
                    else if (header == 'G') { // gameover & send win
                        PLAYER[color].lost = true;
                    }
                }
                if (didAnyoneLose()) {
                    gameover();
                    is_running = false;
                }
            }
        }
    }

    removeBoard();
    close(server_sock);

    return 0;
}

void intro() {
printf("      ____ _                     ____                           \n");
printf("     / ___| |__   ___  ___ ___  / ___|  ___ _ ____   _____ _ __ \n");
printf("    | |   | '_ \\ / _ \\/ __/ __| \\___ \\ / _ \\ '__\\ \\ / / _ \\ '__|\n");
printf("    | |___| | | |  __/\\__ \\__ \\  ___) |  __/ |   \\ V /  __/ |   \n");
printf("     \\____|_| |_|\\___||___/___/ |____/ \\___|_|    \\_/ \\___|_|   \n\n");
}

void createBoard() {
    BOARD = (wchar_t**) malloc(sizeof(wchar_t*) * ROWS);
    for (int row = 0; row < ROWS; row++) 
        BOARD[row] = (wchar_t*) malloc(sizeof(wchar_t) * COLS);
}

void removeBoard() {
    for (int row = 0;  row < ROWS; row++) 
        free(BOARD[row]);
    free(BOARD);
}

void initiateBoard() {
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            if (row == 0 || row == 7) {
                if (col == 0 || col == 7) 
                    BOARD[row][col] = row == 0 ? BLACK_ROOK : WHITE_ROOK;
                else if (col == 1 || col == 6)
                    BOARD[row][col] = row == 0 ? BLACK_KNIGHT : WHITE_KNIGHT;
                else if (col == 2 || col == 5)
                    BOARD[row][col] = row == 0 ? BLACK_BISHOP : WHITE_BISHOP;
                else if (col == 3)
                    BOARD[row][col] = row == 0 ? BLACK_KING : WHITE_KING;
                else if (col == 4)
                    BOARD[row][col] = row == 0 ? BLACK_QUEEN : WHITE_QUEEN;
            }
            else if (row == 1) 
                BOARD[row][col] = BLACK_PAWN;
            else if (row == 6) 
                BOARD[row][col] = WHITE_PAWN;
            else 
                BOARD[row][col] = EMPTY;
        }
    }
}

void parseMoveCommand(char* command, int* from_y, int* from_x, int* to_y, int* to_x) {
    *from_y = command[0] - '0';
    *from_x = command[1] - '0';
    *to_y = command[2] - '0';
    *to_x = command[3] - '0';

    printf("%lc [%c%d] to %lc [%c%d]\n", BOARD[*from_y][*from_x], 97 + *from_x, 8 - *from_y, BOARD[*to_y][*to_x], 97 + *to_x, 8 - *to_y);
}

bool isValidCommand(int color, int from_y, int from_x, int to_y, int to_x) {
    if (isOpponentPiece(color, from_y, from_x)) {
        return false;
    }

    wchar_t piece = BOARD[from_y][from_x]; // from

    if (piece == WHITE_PAWN || piece == BLACK_PAWN) {
        if (color == WHITE) {
            if (from_x == to_x) { // straight forward
                if (from_y - 1 == to_y && isEmpty(to_y, to_x)) { // one step
                    return true;
                }
                else if (from_y == 6 && from_y - 2 == to_y) { // two steps
                    if (isEmpty(to_y, to_x) && isEmpty(to_y + 1, to_x)) {
                        return true;
                    }
                }
            }
            else if (from_x - 1 == to_x || from_x + 1 == to_x) { // diagonally 
                if (from_y - 1 == to_y) {
                    if (isOpponentPiece(color, to_y, to_x)) {
                        return true;
                    }
                }
            }
        }
        else if (color == BLACK) {
            if (from_x == to_x) { // straight forward
                if (from_y + 1 == to_y && isEmpty(to_y, to_x)) { // one step
                    return true;
                }
                else if (from_y == 1 && from_y + 2 == to_y) { // two steps
                    if (isEmpty(to_y, to_x) && isEmpty(to_y - 1, to_x)) {
                        return true;
                    }
                }
            }
            else if (from_x - 1 == to_x || from_x + 1 == to_x) { // diagonally
                if (from_y + 1 == to_y) {
                    if(isOpponentPiece(color, to_y, to_x)) {
                        return true;
                    }
                }
            }
        }
    }
    else if (piece == WHITE_ROOK || piece == BLACK_ROOK) {
        if (isOpponentPiece(color, to_y, to_x) || isEmpty(to_y, to_x)) { // the destination must be empty or opponent
            if (from_y == to_y) { // horizontally
                if (from_x < to_x) { // right
                    for (int col = to_x - 1; col > from_x; col--) {
                        if (!isEmpty(from_y, col)) {
                            return false;
                        }
                    }
                    return true;
                }
                else if (from_x > to_x) { // left
                    for (int col = to_x + 1; col < from_x; col++) {
                        if (!isEmpty(from_y, col)) {
                            return false;
                        }
                    }
                    return true;
                }
            }
            else if (from_x == to_x) { // vertically
                if (from_y < to_y) { // down
                    for (int row = to_y - 1; row > from_y; row--) {
                        if (!isEmpty(row, from_x)) {
                            return false;
                        }
                    }
                    return true;
                }
                else if (from_y > to_y) { // up
                    for (int row = to_y + 1; row < from_y; row++) {
                        if (!isEmpty(row, from_x)) {
                            return false;
                        }
                    }
                    return true;
                }
            }
        }
    }
    else if (piece == WHITE_KNIGHT || piece == BLACK_KNIGHT) {
        if (isOpponentPiece(color, to_y, to_x) || isEmpty(to_y, to_x)) { // the destination must be empty or opponent
            if (from_y - 2 == to_y) {
                if (from_x - 1 == to_x || from_x + 1 == to_x) {
                    return true;
                }
            }
            else if (from_y - 1 == to_y) {
                if (from_x - 2 == to_x || from_x + 2 == to_x) {
                    return true;
                }
            }
            else if (from_y + 1 == to_y) {
                if (from_x - 2 == to_x || from_x + 2 == to_x) {
                    return true;
                }
            }
            else if (from_y + 2 == to_y) {
                if (from_x - 1 == to_x || from_x + 1 == to_x) {
                    return true;
                }
            }
        }
    }
    else if (piece == WHITE_BISHOP || piece == BLACK_BISHOP) {
        if (isOpponentPiece(color, to_y, to_x) || isEmpty(to_y, to_x)) { // the destination must be empty or opponent
            if (abs(from_y - to_y) == abs(from_x - to_x)) {
                if (from_x > to_x) { // left
                    if (from_y > to_y) { // left + up
                        for (int row = to_y + 1; row < from_y; row++) {
                            if (!isEmpty(row, to_x + (row - to_y))) {
                                return false;
                            }
                        }
                        return true;
                    }
                    else if (from_y < to_y) { // left + down
                        for (int row  = to_y - 1; row > from_y; row--) {
                            if (!isEmpty(row, to_x + (to_y - row))) {
                                return false;
                            }
                        }
                        return true;
                    }
                }
                else if (from_x < to_x) { // right
                    if (from_y > to_y) { // right + up
                        for (int row = to_y + 1; row < from_y; row++) {
                            if (!isEmpty(row, to_x - (row - to_y))) {
                                return false;
                            }
                        }
                        return true;
                    }
                    else if (from_y < to_y) { // right + down
                        for (int row = to_y - 1; row < from_y; row--) {
                            if (!isEmpty(row, to_x - (to_y - row))) {
                                return false;
                            }
                        }
                        return true;
                    }
                }
            }
        }
    }
    else if (piece == WHITE_QUEEN || piece == BLACK_QUEEN) {
        if (isOpponentPiece(color, to_y, to_x) || isEmpty(to_y, to_x)) { // the destination must be empty or opponent
            if (abs(from_y - to_y) == abs(from_x - to_x)) { // diagonally
                if (from_x > to_x) { // left
                    if (from_y > to_y) { // left + up
                        for (int row = to_y + 1; row < from_y; row++) {
                            if (!isEmpty(row, to_x + (row - to_y))) {
                                return false;
                            }
                        }
                        return true;
                    }
                    else if (from_y < to_y) { // left + down
                        for (int row  = to_y - 1; row > from_y; row--) {
                            if (!isEmpty(row, to_x + (to_y - row))) {
                                return false;
                            }
                        }
                        return true;
                    }
                }
                else if (from_x < to_x) { // right
                    if (from_y > to_y) { // right + up
                        for (int row = to_y + 1; row < from_y; row++) {
                            if (!isEmpty(row, to_x - (row - to_y))) {
                                return false;
                            }
                        }
                        return true;
                    }
                    else if (from_y < to_y) { // right + down
                        for (int row = to_y - 1; row < from_y; row--) {
                            if (!isEmpty(row, to_x - (to_y - row))) {
                                return false;
                            }
                        }
                        return true;
                    }
                }
            }
            else if (from_y == to_y) { // horizontally
                if (from_x < to_x) { // right
                    for (int col = to_x - 1; col > from_x; col--) {
                        if (!isEmpty(from_y, col)) {
                            return false;
                        }
                    }
                    return true;
                }
                else if (from_x > to_x) { // left
                    for (int col = to_x + 1; col < from_x; col++) {
                        if (!isEmpty(from_y, col)) {
                            return false;
                        }
                    }
                    return true;
                }
            }
            else if (from_x == to_x) { // vertically
                if (from_y < to_y) { // down
                    for (int row = to_y - 1; row > from_y; row--) {
                        if (!isEmpty(row, from_x)) {
                            return false;
                        }
                    }
                    return true;
                }
                else if (from_y > to_y) { // up
                    for (int row = to_y + 1; row < from_y; row++) {
                        if (!isEmpty(row, from_x)) {
                            return false;
                        }
                    }
                    return true;
                }
            }
        }
    }
    else if (piece == WHITE_KING || piece == BLACK_KING) {
        if (isOpponentPiece(color, to_y, to_x) || isEmpty(to_y, to_x)) { // the destination must be empty or opponent
            if (from_y - 1 == to_y) {
                if (from_x - 1 == to_x || from_x == to_x || from_x + 1 == to_x) {
                    return true;
                }
            }
            else if (from_y == to_y) {
                if (from_x - 1 == to_x || from_x + 1 == to_x) {
                    return true;
                }
            }
            else if (from_y + 1 == to_y) {
                if (from_x - 1 == to_x || from_x == to_x || from_x + 1 == to_x) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool isEmpty(int y, int x) {
    wchar_t piece = BOARD[y][x];

    return piece == EMPTY ? true : false;
}

bool isOwnPiece(int color, int y, int x) {
    wchar_t piece = BOARD[y][x];
    
    if (piece != EMPTY) {
        if (color == WHITE) 
            return piece <= WHITE_PAWN ? true : false;
        else if (color == BLACK)
            return piece >= BLACK_KING ? true : false;            
    }

    return false;
}

bool isOpponentPiece(int color, int y, int x) {
    wchar_t piece = BOARD[y][x];

    if (piece != EMPTY) {
        if (color == WHITE) {
            if (BLACK_KING <= piece && piece <= BLACK_PAWN) {
                return true;
            }
        }
        else if (color == BLACK) {
            if (WHITE_KING <= piece && piece <= WHITE_PAWN) {
                return true;
            }
        }
    }

    return false;
}

bool isPromotionPossible(int y, int x) {
    if (BOARD[y][x] == WHITE_PAWN || BOARD[y][x] == BLACK_PAWN) { // piece must be pawn
        if (y == 0 || y == 7) { // the pawn reaches the eighth rank
            return true;
        }
    }
    return false;
}

void movePiece(int from_y, int from_x, int to_y, int to_x) {
    BOARD[to_y][to_x] = BOARD[from_y][from_x];
    BOARD[from_y][from_x] = EMPTY;
}

void promote(char* msg, int color, int prom_y, int prom_x) {  
    if (strcmp(msg, "queen") == 0) {
        BOARD[prom_y][prom_x] = color == WHITE ? WHITE_QUEEN : BLACK_QUEEN;
    }
    else if (strcmp(msg, "rook") == 0) {
        BOARD[prom_y][prom_x] = color == WHITE ? WHITE_ROOK : BLACK_ROOK;
    }
    else if (strcmp(msg, "bishop") == 0) {
        BOARD[prom_y][prom_x] = color == WHITE ? WHITE_BISHOP : BLACK_BISHOP;
    }
    else if (strcmp(msg, "knight") == 0) {
        BOARD[prom_y][prom_x] = color == WHITE ? WHITE_KNIGHT : BLACK_KNIGHT;
    }

    printf("the promotion is successful!!\n");    
}

void whoIsNext(int socket, int color) {
    sleep(0.5);
    post(socket, "W Wait for your opponent's turn.");
    if (color == WHITE) {
        post(PLAYER[BLACK].sock, "U Your turn. Select the BLACK.");
    }
    else {
        post(PLAYER[WHITE].sock, "U Your turn. Select the WHITE.");
    }
}

void post(int sock, char* message) {
    usleep(100000);
    send(sock, message, strlen(message), 0);
}

void debugBoard() {
    printf("\n");
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            printf("%lc ", BOARD[y][x]);
        }
        printf("\n");
    }
    printf("\n");
}

void sendBoard(int sock) {
    char buffer[BUFFER_SIZE] = {0, };
    char temp[100] = {0, };

    strcat(buffer, "B ");

    strcat(buffer, "    +---+---+---+---+---+---+---+---+\n");
    strcat(buffer, "    | a | b | c | d | e | f | g | h |\n");
    for (int y = 0; y < ROWS; y++) {
        strcat(buffer, "+---+---+---+---+---+---+---+---+---+---+\n");
        sprintf(temp, "| %d | %lc | %lc | %lc | %lc | %lc | %lc | %lc | %lc | %d |\n", 8 - y, BOARD[y][0], BOARD[y][1], BOARD[y][2], BOARD[y][3], BOARD[y][4], BOARD[y][5], BOARD[y][6], BOARD[y][7], 8 - y);
        strcat(buffer, temp);
    }
    strcat(buffer, "+---+---+---+---+---+---+---+---+---+---+\n");
    strcat(buffer, "    | a | b | c | d | e | f | g | h |\n");
    strcat(buffer, "    +---+---+---+---+---+---+---+---+\n");
    
    if (sock == ALL) {
        post(PLAYER[WHITE].sock, buffer);
        post(PLAYER[BLACK].sock, buffer);
    }
    else {
        post(sock, buffer);
    }
}

bool didAnyoneLose() {
    if (PLAYER[WHITE].lost || PLAYER[BLACK].lost) {
        return true;
    }
    return false;
}

void gameover() {
    int winner = PLAYER[WHITE].lost ? PLAYER[BLACK].sock : PLAYER[WHITE].sock;
    int loser = PLAYER[WHITE].lost ? PLAYER[WHITE].sock : PLAYER[BLACK].sock;
 
    printf("winner : %d loser : %d\n", winner, loser);
    post(loser, "G You lose.");
    post(winner, "G You have won the game!");
}
