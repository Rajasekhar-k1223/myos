#include "minesweeper.h"
#include "kheap.h"
#include "string.h"
#include "pit.h"
#include "speaker.h"

#define BOARD_W 10
#define BOARD_H 10
#define NUM_MINES 10
#define CELL_SIZE 20

typedef struct {
    int is_mine;
    int is_revealed;
    int is_flagged;
    int neighbors;
} cell_t;

typedef struct {
    cell_t board[BOARD_W][BOARD_H];
    int game_over;
    int win;
    int cells_revealed;
} minesweeper_state_t;

static window_t* my_win = 0;
static minesweeper_state_t* state = 0;

static void minesweeper_render(void) {
    if (!my_win || !state) return;
    
    // Fill background
    for (uint32_t i = 0; i < my_win->w * my_win->h; i++) {
        my_win->buffer[i] = 0xAAAAAA; // Light gray GUI background
    }
    
    // Draw cells
    for (int y = 0; y < BOARD_H; y++) {
        for (int x = 0; x < BOARD_W; x++) {
            int cx = 10 + x * CELL_SIZE;
            int cy = 10 + y * CELL_SIZE;
            cell_t* c = &state->board[x][y];
            
            if (!c->is_revealed) {
                // Draw 3D bevel button
                for (int cyy = 0; cyy < CELL_SIZE; cyy++) {
                    for (int cxx = 0; cxx < CELL_SIZE; cxx++) {
                        uint32_t color = 0xCCCCCC;
                        if (cxx == 0 || cyy == 0) color = 0xFFFFFF; // Top/Left highlight
                        if (cxx == CELL_SIZE-1 || cyy == CELL_SIZE-1) color = 0x888888; // Bottom/Right shadow
                        my_win->buffer[(cy + cyy) * my_win->w + (cx + cxx)] = color;
                    }
                }
                
                if (c->is_flagged) {
                    // Draw Red Flag
                    for (int cyy = 4; cyy < 16; cyy++) {
                        for (int cxx = 8; cxx < 12; cxx++) {
                            my_win->buffer[(cy + cyy) * my_win->w + (cx + cxx)] = 0xFF0000;
                        }
                    }
                }
            } else {
                // Revealed cell
                for (int cyy = 0; cyy < CELL_SIZE; cyy++) {
                    for (int cxx = 0; cxx < CELL_SIZE; cxx++) {
                        uint32_t color = 0x888888; // Border
                        if (cxx > 0 && cyy > 0 && cxx < CELL_SIZE-1 && cyy < CELL_SIZE-1) color = 0xDDDDDD; // Inner
                        my_win->buffer[(cy + cyy) * my_win->w + (cx + cxx)] = color;
                    }
                }
                
                if (c->is_mine) {
                    // Draw Black Bomb
                    for (int cyy = 5; cyy < 15; cyy++) {
                        for (int cxx = 5; cxx < 15; cxx++) {
                            my_win->buffer[(cy + cyy) * my_win->w + (cx + cxx)] = 0x000000;
                        }
                    }
                } else if (c->neighbors > 0) {
                    // Draw Number (crudely)
                    uint32_t num_colors[] = {0, 0x0000FF, 0x008000, 0xFF0000, 0x000080, 0x800000, 0x008080, 0x000000, 0x808080};
                    uint32_t nc = num_colors[c->neighbors];
                    
                    // Tiny 3x5 font approx
                    extern void wm_putchar(window_t* win, char c);
                    // Instead of full font, just draw a little block of color to represent number
                    // We can just use the color since we don't have font drawing into buffer easily 
                    // Actually, let's just make a small dot block
                    for (int i=0; i<c->neighbors; i++) {
                        int dx = 4 + (i%3)*4;
                        int dy = 4 + (i/3)*4;
                        for (int by=0; by<2; by++)
                            for (int bx=0; bx<2; bx++)
                                my_win->buffer[(cy + dy + by) * my_win->w + (cx + dx + bx)] = nc;
                    }
                }
            }
        }
    }
    
    // Draw Game Over or Win message
    if (state->game_over) {
        // We can't use wm_draw_string easily here into the buffer without the font,
        // so we'll just draw a big red banner
        for (int y = 0; y < 20; y++) {
            for (int x = 0; x < my_win->w; x++) {
                my_win->buffer[y * my_win->w + x] = 0xFF0000;
            }
        }
    } else if (state->win) {
        for (int y = 0; y < 20; y++) {
            for (int x = 0; x < my_win->w; x++) {
                my_win->buffer[y * my_win->w + x] = 0x00FF00;
            }
        }
    }
    
    extern void wm_request_redraw(void);
    wm_request_redraw();
}

static void reveal_cell(int x, int y) {
    if (x < 0 || x >= BOARD_W || y < 0 || y >= BOARD_H) return;
    cell_t* c = &state->board[x][y];
    if (c->is_revealed || c->is_flagged) return;
    
    c->is_revealed = 1;
    state->cells_revealed++;
    
    if (c->is_mine) {
        state->game_over = 1;
        speaker_beep(100, 50); // Explosion sound
        // Reveal all mines
        for (int j = 0; j < BOARD_H; j++) {
            for (int i = 0; i < BOARD_W; i++) {
                if (state->board[i][j].is_mine) state->board[i][j].is_revealed = 1;
            }
        }
        return;
    }
    
    if (c->neighbors == 0) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx != 0 || dy != 0) reveal_cell(x + dx, y + dy);
            }
        }
    }
    
    if (state->cells_revealed == (BOARD_W * BOARD_H - NUM_MINES)) {
        state->win = 1;
        speaker_beep(1000, 20);
        speaker_beep(1500, 20);
    }
}

void minesweeper_init(window_t* win) {
    my_win = win;
    if (!state) state = (minesweeper_state_t*)kmalloc(sizeof(minesweeper_state_t));
    memset(state, 0, sizeof(minesweeper_state_t));
    
    // Randomize mines
    int mines_placed = 0;
    uint32_t seed = pit_get_ticks();
    while (mines_placed < NUM_MINES) {
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
        int x = (seed >> 16) % BOARD_W;
        int y = (seed >> 8) % BOARD_H;
        if (!state->board[x][y].is_mine) {
            state->board[x][y].is_mine = 1;
            mines_placed++;
        }
    }
    
    // Calculate neighbors
    for (int y = 0; y < BOARD_H; y++) {
        for (int x = 0; x < BOARD_W; x++) {
            if (state->board[x][y].is_mine) continue;
            int count = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx;
                    int ny = y + dy;
                    if (nx >= 0 && nx < BOARD_W && ny >= 0 && ny < BOARD_H) {
                        if (state->board[nx][ny].is_mine) count++;
                    }
                }
            }
            state->board[x][y].neighbors = count;
        }
    }
    
    minesweeper_render();
}

void minesweeper_handle_click(window_t* win, int mx, int my, int right_click) {
    if (win != my_win || !state || state->game_over || state->win) return;
    
    // Convert mouse coords to board coords
    int bx = mx - win->x - 10;
    int by = my - win->y - 20 - 10;
    
    if (bx < 0 || by < 0) return;
    int cx = bx / CELL_SIZE;
    int cy = by / CELL_SIZE;
    
    if (cx < 0 || cx >= BOARD_W || cy < 0 || cy >= BOARD_H) return;
    
    cell_t* c = &state->board[cx][cy];
    
    if (right_click) {
        if (!c->is_revealed) {
            c->is_flagged = !c->is_flagged;
            speaker_beep(500, 5);
        }
    } else {
        if (!c->is_flagged) {
            reveal_cell(cx, cy);
            speaker_beep(1500, 5);
        }
    }
    
    minesweeper_render();
}
