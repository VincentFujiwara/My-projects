#include <stdio.h>
#include <conio.h>
#include <windows.h>

#define W 20
#define H 20
#define CELL_COUNT (W * H)

int x, y, fx, fy, score, run, len, speed = 150;
int tx[CELL_COUNT], ty[CELL_COUNT];
char dir;

int map[H][W];

void InitGame() {
    x = W / 2; y = H / 2; fx = 5; fy = 5; score = 0; len = 0;
    dir = 'd'; run = 1;
}

void PlayGame() {
    InitGame();
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO c = { 100, 0 };
    SetConsoleCursorInfo(h, &c);

    char buf[(W + 1) * H + 1];
    COORD coord = { 0, 0 };

    while (run) {
        if (_kbhit()) {
            int k = _getch();
            if (k == 224) {
                k = _getch();
                if (k == 72 && dir != 's') dir = 'w';
                if (k == 80 && dir != 'w') dir = 's';
                if (k == 75 && dir != 'd') dir = 'a';
                if (k == 77 && dir != 'a') dir = 'd';
            }
            else {
                if ((k == 'w' || k == 'W' || k == 246 || k == 134 || k == 204 || k == 172) && dir != 's') dir = 'w';
                if ((k == 's' || k == 'S' || k == 251 || k == 155 || k == 219 || k == 187) && dir != 'w') dir = 's';
                if ((k == 'a' || k == 'A' || k == 244 || k == 132 || k == 212 || k == 180) && dir != 'd') dir = 'a';
                if ((k == 'd' || k == 'D' || k == 226 || k == 130 || k == 194 || k == 162) && dir != 'a') dir = 'd';
                if (k == 27) run = 0;
            }
        }

        memset(map, 0, sizeof(map));

        int px = tx[0], py = ty[0];
        tx[0] = x; ty[0] = y;
        if (len > 0) {
            map[y][x] = 1;
        }

        for (int i = 1; i < len; i++) {
            int nx = tx[i], ny = ty[i];
            tx[i] = px; ty[i] = py;
            map[py][px] = 1;
            px = nx; py = ny;
        }

        if (dir == 'w') y--;
        else if (dir == 's') y++;
        else if (dir == 'a') x--;
        else if (dir == 'd') x++;

        if (x < 0 || x >= W || y < 0 || y >= H || map[y][x]) run = 0;

        if (x == fx && y == fy) {
            score++;
            len++;
            fx = rand() % W;
            fy = rand() % H;
        }

        SetConsoleCursorPosition(h, coord);

        int idx = 0;
        for (int i = 0; i < H; i++) {
            for (int j = 0; j < W; j++) {
                if (i == y && j == x) buf[idx++] = 'O';
                else if (i == fy && j == fx) buf[idx++] = 'F';
                else buf[idx++] = map[i][j] ? 'o' : '.';
            }
            buf[idx++] = '\n';
        }
        buf[idx] = '\0';

        printf("%sScore: %d  (ESC to Exit)\n", buf, score);
        Sleep(speed);
    }

    SetConsoleCursorPosition(h, coord);
    for (int i = 0; i < H + 2; i++) printf("                      \n");
    SetConsoleCursorPosition(h, coord);
    printf("\n  GAME OVER!\n  Score: %d\n\n  Press any key...", score);
    _getch();
}

void SettingsMenu() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD coord = { 0, 0 };
    while (1) {
        SetConsoleCursorPosition(h, coord);
        printf("=== SPEED SETTINGS ===\n\n");
        printf(" %s 1. Fast  \n", speed == 70 ? "->" : "  ");
        printf(" %s 2. Normal\n", speed == 150 ? "->" : "  ");
        printf(" %s 3. Slow  \n", speed == 250 ? "->" : "  ");
        printf("\n Select (1-3) or 0 to back:   \n");

        char ch = _getch();
        if (ch == '1') speed = 70;
        else if (ch == '2') speed = 150;
        else if (ch == '3') speed = 250;
        else if (ch == '0') { system("cls"); break; }
    }
}

int main() {
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    system("cls");

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD coord = { 0, 0 };

    while (1) {
        SetConsoleCursorPosition(h, coord);
        printf("=======================\n");
        printf("    ZNAKE LAUNCHER     \n");
        printf("=======================\n\n");
        printf("  1. Play Game \n");
        printf("  2. Settings \n");
        printf("  3. Exit     \n\n");
        printf("=======================\n");
        printf(" Choice: ");

        char menu = _getch();
        if (menu == '1') { system("cls"); PlayGame(); system("cls"); }
        else if (menu == '2') { SettingsMenu(); }
        else if (menu == '3') break;
    }
    system("cls");
    return 0;
}