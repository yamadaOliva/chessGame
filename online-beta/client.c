#include <ncursesw/ncurses.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <locale.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <stdio.h>

#define BUFFER_SIZE           1024
#define ARROW                 0x25B6
#define ENTER                 10
#define ERROR                 -1
#define ESC                   27

#define CURSOR_HIDE           0
#define CURSOR_VISIBLE        1

#define HORIZONTAL_MOVE       4
#define VERTICAL_MOVE         2

#define BOARD_MIN_Y           3
#define BOARD_MAX_Y           17
#define BOARD_MIN_X           6
#define BOARD_MAX_X           34

bool isStart();
void *recvMsg(void* arg);
void showMsgFromServer(char*);
void showPromotion(int );
void showMenu();
void showBoard(char*);
void removeWindows();
void post(int, char*);
void postMoveCommand(int);
wchar_t selectPiece(int*, int*, int*, int*);
void timeCounter();
WINDOW* MENU_WIN;
WINDOW* SERVER_WIN;
WINDOW* PROMOTION_WIN;
WINDOW* BOARD_WIN;
WINDOW* SELECTED_FROM_WIN;
WINDOW* SELECTED_TO_WIN;

bool quit_flag;

int main(int argc, char** argv) {
    if (argc != 3) {
        printf("usage - %s <ip> <port>\n", argv[0]);
        return ERROR;
    }

    int sock, str_len, recv_len, recv_cnt;
    pthread_t send_thread, recv_thread;
    struct sockaddr_in serv_addr;
    void *thread_return;
    char msg[BUFFER_SIZE];

    setlocale(LC_ALL, "");
    initscr();
    noecho();
    start_color();
    keypad(stdscr, TRUE); 

    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_YELLOW, COLOR_BLACK);
    refresh();
   
    // display start screen
    if (!isStart()) {
        endwin();
        return 0;
    }

    clear();
    refresh();
    quit_flag = false;

    // create windows
    SERVER_WIN = newwin(3, 36, 1, 43);
    PROMOTION_WIN = newwin(4, 10, 8, 50);
    BOARD_WIN = newwin(23, 42, 0, 0);
    MENU_WIN = newwin(2, 15, 15, 54);

    // socket()
    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);   
    if (sock == ERROR)  {
        endwin();
        return ERROR;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    // connect()
    if (connect(sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == ERROR) {
        endwin();
        return ERROR;
    }

    // pthread_create() recv
    pthread_create(&recv_thread, NULL, recvMsg, (void*) &sock);

    // ptrhead_join() recv
    pthread_join(recv_thread, &thread_return);

    // close()
    close(sock);
    removeWindows();
    endwin();

    return 0;
}

void timeCounter(){
    int i = 900;
    int min, sec;
    while(i > 0){
        min = i / 60;
        sec = i % 60;
        mvprintw(1, 1, "%d:%d", min, sec);
        refresh();
        sleep(1);
        i--;
    }
}
 
void removeWindows() {
    delwin(SERVER_WIN);
    delwin(PROMOTION_WIN);
    delwin(BOARD_WIN);
    delwin(MENU_WIN);
    delwin(SELECTED_FROM_WIN);
    delwin(SELECTED_TO_WIN);
}

bool isStart() {
    bool not_done = true;
    int arrow_x = 33, arrow_y = 13;
    const int start_y = 13, exit_y = 14;

    curs_set(CURSOR_HIDE);

    printw("             ____ _  \n");
    printw("            / ___| |__   ___  ___ ___    __ _  __ _ _ __ ___   ___ \n");
    printw("           | |   | '_ \\ / _ \\/ __/ __|  / _` |/ _` | '_ ` _ \\ / _ \\\n");
    printw("           | |___| | | |  __/\\__ \\__ \\ | (_| | (_| | | | | | |  __/\n");
    printw("            \\____|_| |_|\\___||___/___/  \\__, |\\__,_|_| |_| |_|\\___|\n");
    printw("                                       |___/\n");


    mvprintw(start_y, arrow_x + 4, "START");
    mvprintw(exit_y, arrow_x + 4, "EXIT");
    mvprintw(arrow_y, arrow_x, "%lc", ARROW);
    
    while (not_done) {
        switch (getch()) {
            case KEY_UP:
                if (arrow_y == exit_y) {
                    mvprintw(arrow_y--, arrow_x, " ");
                    mvprintw(arrow_y, arrow_x, "%lc", ARROW);
                }
                break;
            case KEY_DOWN:
                if (arrow_y == start_y) {
                    mvprintw(arrow_y++, arrow_x, " ");
                    mvprintw(arrow_y, arrow_x, "%lc", ARROW);
                }
                break;
            case ENTER:
                not_done = false;
                break;
        }
        refresh();
    }

    return arrow_y == start_y ? true : false;
}

void *recvMsg(void* arg) {
    char header, message[BUFFER_SIZE];
    char* msg;
    int sock = *((int*)arg);
    int recv_cnt;
 
    while(true) {
        recv_cnt = read(sock, &message, BUFFER_SIZE - 1);
        if(recv_cnt == ERROR) 
            break;

        message[recv_cnt] = 0;
        header = message[0];
        msg = message;
        msg = msg + 2;
        if(header == 'F') { // Failed to connect 
            removeWindows();
            endwin();
            exit(1);
        }
        else if (header == 'B') { // get the board
            showBoard(msg);
        }
        else if (header == 'U') { // your turn
            showMsgFromServer(msg);
            postMoveCommand(sock);
        }
        else if (header == 'P') { // promotion
            showPromotion(sock);
        }
        else if (header == 'R') { // invalid move command
            showMsgFromServer(msg);
            postMoveCommand(sock);
        }
        else if (header == 'S') { // from the server
            showMsgFromServer(msg);
        }
        else if (header == 'W') { // opponent's trun
            showMsgFromServer(msg);
        }
        else if (header == 'G') { // gameover
            showMsgFromServer(msg);
            sleep(3);
            removeWindows();
            endwin();
            exit(1);
        }
    }
}

void postMoveCommand(int sock) {
    char message[BUFFER_SIZE] = {0, };
    wchar_t from, to; // selected
    int y, x, from_y, from_x, to_y, to_x; // locations

    from = selectPiece(&y, &x, &from_y, &from_x);
    to = selectPiece(&y, &x, &to_y, &to_x);

    sprintf(message, "C %d%d%d%d", from_y, from_x, to_y, to_x);
    post(sock, message); 
}

wchar_t selectPiece(int* y, int* x, int* selected_y, int* selected_x) {
    bool is_running = true;
    wchar_t selected_piece;
    
    *y = 3;
    *x = 6;

    move(*y, *x);

    curs_set(CURSOR_VISIBLE);
    
    while (is_running) {
        switch (getch()) {
            case KEY_LEFT:
                *x = *x > BOARD_MIN_X ? *x - HORIZONTAL_MOVE : *x;
                break;
            case KEY_RIGHT:
                *x = *x + HORIZONTAL_MOVE > BOARD_MAX_X ? *x : *x + HORIZONTAL_MOVE;
                break;
            case KEY_UP:
                *y = *y > BOARD_MIN_Y ? *y - VERTICAL_MOVE : *y;
                break;
            case KEY_DOWN:
                *y = *y + VERTICAL_MOVE > BOARD_MAX_Y ? *y : *y + VERTICAL_MOVE;
                break;
            case ENTER:
                selected_piece = mvinch(*y, *x);

                *selected_x = ((*x - 2) / 4) - 1;
                *selected_y = ((*y - 1) / 2) - 1;

                is_running = false;
                break;
            case ESC:
                curs_set(CURSOR_HIDE);
                showMenu();
                if (quit_flag) {
                    removeWindows();
                    endwin();
                    exit(1);
                }
                curs_set(CURSOR_VISIBLE);
                break;
        }
        move(*y, *x);
        refresh();
    }

    curs_set(CURSOR_HIDE);

    return selected_piece;
}

void showPromotion(int sock) {
    const int queen_y = 0, rook_y = 1, bishop_y = 2, knight_y = 3;
    int arrow_x = 0, arrow_y = 0, prev_y;
    bool is_running = true;
    char buffer[BUFFER_SIZE] = {0, };

    werase(PROMOTION_WIN);

    mvwprintw(PROMOTION_WIN, queen_y, arrow_x + 4, "Queen");
    mvwprintw(PROMOTION_WIN, rook_y, arrow_x + 4, "Rook");
    mvwprintw(PROMOTION_WIN, bishop_y, arrow_x + 4, "Bishop");
    mvwprintw(PROMOTION_WIN, knight_y, arrow_x + 4, "Knight");
    mvwprintw(PROMOTION_WIN, arrow_y, arrow_x, "%lc", ARROW);
    wrefresh(PROMOTION_WIN);

    while (is_running) {
        switch (getch()) {
            case KEY_UP:
                prev_y = arrow_y;
                arrow_y = arrow_y - 1 < 0 ? arrow_y : arrow_y - 1;
                break;
            case KEY_DOWN:
                prev_y = arrow_y;
                arrow_y = arrow_y + 1 > 3 ? arrow_y : arrow_y + 1;
                break;
            case ENTER:
                is_running = false;
                break;
        }
        mvwprintw(PROMOTION_WIN, prev_y, arrow_x, " ");
        mvwprintw(PROMOTION_WIN, arrow_y, arrow_x, "%lc", ARROW);
        wrefresh(PROMOTION_WIN);
    }

    if (arrow_y == queen_y) 
        strcpy(buffer, "P queen");
    else if (arrow_y == rook_y) 
        strcpy(buffer, "P rook");
    else if (arrow_y == bishop_y) 
        strcpy(buffer, "P bishop");
    else 
        strcpy(buffer, "P knight");
    
    post(sock, buffer);

    werase(PROMOTION_WIN);
    wrefresh(PROMOTION_WIN);
}

void showMsgFromServer(char* msg) {
    werase(SERVER_WIN);
    wborder(SERVER_WIN, '|','|','-','-','+','+','+','+');
    wattron(SERVER_WIN, COLOR_PAIR(2));
    mvwprintw(SERVER_WIN, 1, 2, msg);
    wattroff(SERVER_WIN, COLOR_PAIR(2));
    wrefresh(SERVER_WIN);
}

void showBoard(char* msg) {
    werase(BOARD_WIN);
    mvwprintw(BOARD_WIN, 0, 0, msg);
    wrefresh(BOARD_WIN);
}

void showMenu() {
    const int continue_y = 0, quit_y = 1;
    int arrow_x = 0, arrow_y = 0, prev_y;
    bool is_running = true;

    werase(MENU_WIN);
    mvwprintw(MENU_WIN, continue_y, arrow_x + 4, "Continue");
    mvwprintw(MENU_WIN, quit_y, arrow_x + 4, "Quit");
    mvwprintw(MENU_WIN, arrow_y, arrow_x, "%lc", ARROW);
    wrefresh(MENU_WIN);

    while (is_running) {

        switch (getch()) {
            case KEY_UP:
                if (arrow_y == quit_y) {
                    mvwprintw(MENU_WIN, arrow_y--, arrow_x, " ");
                    mvwprintw(MENU_WIN, arrow_y, arrow_x, "%lc", ARROW);
                }
                break;
            case KEY_DOWN:
                if (arrow_y == continue_y) {
                    mvwprintw(MENU_WIN, arrow_y++, arrow_x, " ");
                    mvwprintw(MENU_WIN, arrow_y, arrow_x, "%lc", ARROW);
                }
                break;
            case ENTER:
                is_running = false;
                break;
        }
        wrefresh(MENU_WIN);
    }

    werase(MENU_WIN);
    wrefresh(MENU_WIN);

    quit_flag = arrow_y == continue_y ? false : true;
}

void post(int sock, char* msg) {
    send(sock, msg, strlen(msg), 0);
}
