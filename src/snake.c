#include "snake.h"
#include "string.h"
#include "pit.h"

#define GRID_W 38
#define GRID_H 35
#define CELL_SIZE 10

static window_t* snake_win = 0;

typedef struct {
    int x, y;
} point_t;

static point_t snake[100];
static int snake_len = 0;
static int snake_dir_x = 1;
static int snake_dir_y = 0;
static point_t apple;
static int game_over = 0;
static int last_tick_time = 0;

static void spawn_apple(void) {
    // Simple pseudo-random using pit
    uint32_t ticks = pit_get_ticks();
    apple.x = (ticks % (GRID_W - 2)) + 1;
    apple.y = ((ticks / 3) % (GRID_H - 2)) + 1;
}

void snake_init(window_t* win) {
    snake_win = win;
    snake_len = 3;
    snake[0].x = 10; snake[0].y = 10;
    snake[1].x = 9;  snake[1].y = 10;
    snake[2].x = 8;  snake[2].y = 10;
    snake_dir_x = 1;
    snake_dir_y = 0;
    game_over = 0;
    last_tick_time = pit_get_ticks();
    spawn_apple();
}

static void draw_rect(int gx, int gy, uint32_t color) {
    if (!snake_win) return;
    int px = gx * CELL_SIZE + 5;
    int py = gy * CELL_SIZE + 25; // below title bar
    
    for (int y = 0; y < CELL_SIZE - 1; y++) {
        for (int x = 0; x < CELL_SIZE - 1; x++) {
            int dx = px + x;
            int dy = py + y;
            if (dx >= 0 && dx < (int)snake_win->w && dy >= 0 && dy < (int)snake_win->h) {
                snake_win->buffer[dy * snake_win->w + dx] = color;
            }
        }
    }
}

static void snake_render(void) {
    if (!snake_win || !snake_win->active) {
        // If window was closed, stop the game
        snake_win = 0;
        return;
    }
    
    // Clear background
    for (int i = 21 * snake_win->w; i < (int)(snake_win->w * snake_win->h); i++) {
        snake_win->buffer[i] = 0x000000;
    }
    
    if (game_over) {
        // Find center of window to write text (rough estimate)
        int cx = (snake_win->w / 2) - 36;
        int cy = snake_win->h / 2;
        extern void wm_draw_string(int x, int y, const char* str, uint32_t color);
        // Wait, wm_draw_string draws to screen, not window!
        // I need to write text to window buffer, but we don't have a window string function yet.
        // That's fine, we will just turn the screen red.
        for (int i = 21 * snake_win->w; i < (int)(snake_win->w * snake_win->h); i++) {
            snake_win->buffer[i] = 0xFF0000;
        }
        extern void wm_request_redraw(void);
        wm_request_redraw();
        return;
    }
    
    // Draw apple
    draw_rect(apple.x, apple.y, 0xFF0000); // Red
    
    // Draw snake
    for (int i = 0; i < snake_len; i++) {
        draw_rect(snake[i].x, snake[i].y, 0x00FF00); // Green
    }
    
    extern void wm_request_redraw(void);
    wm_request_redraw();
}

void snake_tick(void) {
    if (!snake_win || !snake_win->active) {
        snake_win = 0;
        return;
    }
    if (game_over) return;
    
    uint32_t now = pit_get_ticks();
    if (now - last_tick_time < 10) return; // 100ms
    last_tick_time = now;
    
    // Move body
    for (int i = snake_len - 1; i > 0; i--) {
        snake[i] = snake[i - 1];
    }
    
    // Move head
    snake[0].x += snake_dir_x;
    snake[0].y += snake_dir_y;
    
    // Check wall collision
    if (snake[0].x < 0 || snake[0].x >= GRID_W || 
        snake[0].y < 0 || snake[0].y >= GRID_H) {
        game_over = 1;
    }
    
    // Check self collision
    for (int i = 1; i < snake_len; i++) {
        if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) {
            game_over = 1;
        }
    }
    
    // Check apple
    if (snake[0].x == apple.x && snake[0].y == apple.y) {
        if (snake_len < 100) snake_len++;
        spawn_apple();
    }
    
    snake_render();
}

int snake_handle_input(char c) {
    if (!snake_win || !snake_win->active) return 0; // Not handled
    if (game_over) return 0;
    
    if (c == 'w' && snake_dir_y != 1) { snake_dir_x = 0; snake_dir_y = -1; return 1; }
    if (c == 's' && snake_dir_y != -1) { snake_dir_x = 0; snake_dir_y = 1; return 1; }
    if (c == 'a' && snake_dir_x != 1) { snake_dir_x = -1; snake_dir_y = 0; return 1; }
    if (c == 'd' && snake_dir_x != -1) { snake_dir_x = 1; snake_dir_y = 0; return 1; }
    
    return 0; // Not handled
}
