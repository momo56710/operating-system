#include "libc.h"

/* life [generations]: Conway's Game of Life on a small wrap-around grid, starting with a glider. */
#define W 24
#define H 10

static unsigned char g[2][H][W];

static int neighbours(int cur, int y, int x) {
    int n = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dy || dx) n += g[cur][(y + dy + H) % H][(x + dx + W) % W];
    return n;
}

int main(int argc, char **argv) {
    int gens = argc > 1 ? atoi(argv[1]) : 6;
    int cur = 0;
    g[0][0][1] = g[0][1][2] = g[0][2][0] = g[0][2][1] = g[0][2][2] = 1;     /* glider */
    for (int gen = 0; gen < gens; gen++) {
        int alive = 0;
        printf("generation %d\n", gen);
        for (int y = 0; y < H; y++) {
            char row[W + 1];
            for (int x = 0; x < W; x++) { row[x] = g[cur][y][x] ? '#' : '.'; alive += g[cur][y][x]; }
            row[W] = 0;
            puts(row);
        }
        printf("alive: %d\n", alive);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int n = neighbours(cur, y, x);
                g[1 - cur][y][x] = g[cur][y][x] ? (n == 2 || n == 3) : (n == 3);
            }
        cur = 1 - cur;
        sleep_ms(300);
    }
    return 0;
}
