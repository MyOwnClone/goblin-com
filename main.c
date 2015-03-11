#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include "display.h"
#include "rand.h"
#include "map.h"
#include "game.h"
#include "utf.h"

#define FPS 30
#define PERIOD (1000000 / FPS)
#define SPEED_MAX 3125
#define SPEED_FACTOR 5
#define PERSIST_FILE "persist.gcom"

static bool
is_exit_key(int key)
{
    return key == 'q' || key == 27 || key == 3;
}

static int
game_getch(game_t *game, panel_t *terrain)
{
    for (;;) {
        map_draw_terrain(game->map, terrain);
        display_refresh();
        uint64_t wait = device_uepoch() % PERIOD;
        if (device_kbhit(wait))
            return device_getch();
    }
}

static void
popup_error(char *format, ...)
{
    font_t font = {COLOR_YELLOW, COLOR_BLACK, true, false};
    va_list ap;
    va_start(ap, format);
    int length = vsnprintf(NULL, 0, format, ap);
    va_end(ap);
    va_start(ap, format);
    char buffer[length + 1];
    vsprintf(buffer, format, ap);
    va_end(ap);
    int width = length + 2;
    int height = 3;
    panel_t popup;
    panel_center_init(&popup, width, height);
    panel_puts(&popup, 1, 1, font, buffer);
    display_push(&popup);
    for (;;) {
        int key = display_getch();
        if (is_exit_key(key) || key == 13 || key == ' ')
            break;
    }
    display_pop();
    panel_free(&popup);
    display_refresh();
}

static bool
popup_quit(bool saving)
{
    panel_t popup;
    char *message;
    if (saving)
        message = "Really save and quit? (Rk{y}/Rk{n})";
    else
        message = "Really quit Yk{without saving}? (Rk{y}/Rk{n})";
    size_t length = strlen(message) - 8;
    panel_center_init(&popup, length + 2, 3);
    panel_printf(&popup, 1, 1, message);
    display_push(&popup);
    display_refresh();
    int input = device_getch();
    display_pop();
    panel_free(&popup);
    display_refresh();
    if (input == 'y' || input == 'Y')
        return true;
    return false;
}

static void
popup_unknown_key(int key)
{
#ifndef NDEBUG
    popup_error("Unknown input: %d", key);
#else
    (void) key;
#endif
}

#define SIDEMENU_WIDTH (DISPLAY_WIDTH - MAP_WIDTH)

static void
sidemenu_draw(panel_t *p, game_t *game, yield_t diff)
{
    font_t font_title = {COLOR_WHITE, COLOR_BLACK, false, false};
    panel_fill(p, font_title, ' ');
    panel_border(p, font_title);
    panel_puts(p, 5, 1, font_title, "Goblin-COM");

    font_t font_totals = {COLOR_WHITE, COLOR_BLACK, true, false};
    int ty = 3;
    panel_printf(p, 2, ty++, "Gold: %ldwk{%+d}",
                 (long)game->gold, (int)diff.gold);
    panel_printf(p, 2, ty++, "Food: %ldwk{%+d}",
                 (long)game->food, (int)diff.food);
    panel_printf(p, 2, ty++, "Wood: %ldwk{%+d}",
                 (long)game->wood, (int)diff.wood);
    panel_printf(p, 2, ty++, "Pop.: %ld", (long)game->population);

    int x = 2;
    int y = 8;
    panel_printf(p, x, y++, "wk{Create Rk{B}uilding}");
    panel_printf(p, x, y++, "wk{View Rk{H}eroes}");

    char date[128];
    game_date(game, date);
    panel_puts(p, 2, 20, font_totals, date);

    font_t base = {COLOR_WHITE, COLOR_BLACK, false, false};
    panel_puts(p, 2, 21, base, "Speed: ");
    for (int x = 0, i = 1; i <= game->speed; i *= SPEED_FACTOR, x++)
        panel_puts(p, 9 + x, 21, font_totals, ">");
}

/* Sloppy, but it works! */
static inline char *
u8encode(uint16_t c)
{
    static int i = 0;
    static char buf[8][7];
    buf[i][utf32_to_8(c, (uint8_t *)buf[i % 8])] = '\0';
    return buf[i++ % 8];
}

static uint16_t
popup_build_select(game_t *game, panel_t *terrain)
{
    int width = 56;
    int height = 20;
    panel_t build;
    panel_center_init(&build, width, height);
    display_push(&build);
    font_t border = {COLOR_WHITE, COLOR_BLACK, false, false};
    panel_border(&build, border);

    int input;
    uint16_t result = 0;
    panel_t *p = &build;
    char cost[128];
    char yield[128];

    int y = 1;
    yield_string(cost, COST_LUMBERYARD, false);
    yield_string(yield, YIELD_LUMBERYARD, true);
    panel_printf(p,   1, y++, "(Rk{w}) Yk{Lumberyard} [%s]", cost);
    panel_printf(p, 5, y++, "wk{Yield: %s}", yield);
    panel_printf(p, 5, y++, "wk{Target: forest (%s)}",
                 u8encode(BASE_FOREST));

    yield_string(cost, COST_FARM, false);
    yield_string(yield, YIELD_FARM, true);
    panel_printf(p, 1, y++, "(Rk{f}) Yk{Farm} [%s]", cost);
    panel_printf(p, 5, y++, "wk{Yield: %s}", yield);
    panel_printf(p, 5, y++, "wk{Target: grassland (%s), forest (%s)}",
                 u8encode(BASE_GRASSLAND), u8encode(BASE_FOREST));

    yield_string(cost, COST_STABLE, false);
    yield_string(yield, YIELD_STABLE, true);
    panel_printf(p, 1, y++, "(Rk{s}) Yk{Stable} [%s]", cost);
    panel_printf(p, 5, y++, "wk{Yield: %s}", yield);
    panel_printf(p, 5, y++, "wk{Target: grassland (%s)}",
                 u8encode(BASE_GRASSLAND));

    yield_string(cost, COST_MINE, false);
    yield_string(yield, YIELD_MINE, true);
    panel_printf(p, 1, y++, "(Rk{m}) Yk{Mine} [%s]", cost);
    panel_printf(p, 5, y++, "wk{Yield: %s}", yield);
    panel_printf(p, 5, y++, "wk{Target: hill (%s)}",
                 u8encode(BASE_HILL));

    yield_string(cost, COST_HAMLET, false);
    yield_string(yield, YIELD_HAMLET, true);
    panel_printf(p, 1, y++, "(Rk{h}) Yk{Hamlet} [%s]", cost);
    panel_printf(p, 5, y++, "wk{Yield: %s}", yield);
    panel_printf(p, 5, y++,
                 "wk{Target: grassland (%s), forest (%s), hill (%s)}",
                 u8encode(BASE_GRASSLAND),
                 u8encode(BASE_FOREST),
                 u8encode(BASE_HILL));

    yield_string(cost, COST_ROAD, false);
    yield_string(yield, YIELD_ROAD, true);
    panel_printf(p, 1, y++, "(Rk{r}) Yk{Road} [%s]", cost);
    panel_printf(p, 5, y++, "wk{Yield: %s}", yield);
    panel_printf(p, 5, y++, "wk{Target: (any land)}");

    while (result == 0 && !is_exit_key(input = game_getch(game, terrain)))
        if (strchr("wfshmr", input))
            result = toupper(input);
    if (result == 'R')
        result = '+';
    display_pop();
    panel_free(&build);
    return result;
}

static bool
arrow_adjust(int input, int *x, int *y)
{
    switch (input) {
    case ARROW_U:
        (*y)--;
        return true;
    case ARROW_D:
        (*y)++;
        return true;
    case ARROW_L:
        (*x)--;
        return true;
    case ARROW_R:
        (*x)++;
        return true;
    case ARROW_UL:
        (*x)--;
        (*y)--;
        return true;
    case ARROW_UR:
        (*x)++;
        (*y)--;
        return true;
    case ARROW_DL:
        (*x)--;
        (*y)++;
        return true;
    case ARROW_DR:
        (*x)++;
        (*y)++;
        return true;
    }
    return false;
}

static bool
select_position(game_t *game, panel_t *world, int *x, int *y)
{
    font_t highlight = {COLOR_WHITE, COLOR_RED, true, false};
    *x = MAP_WIDTH / 2;
    *y = MAP_HEIGHT / 2;
    bool selected = false;
    panel_t overlay;
    panel_init(&overlay, 0, 0, MAP_WIDTH, MAP_HEIGHT);
    panel_putc(&overlay, *x, *y, highlight, panel_getc(world, *x, *y));
    display_push(&overlay);
    int input;
    while (!selected && !is_exit_key(input = game_getch(game, world))) {
        panel_erase(&overlay, *x, *y);
        arrow_adjust(input, x, y);
        panel_putc(&overlay, *x, *y, highlight, panel_getc(world, *x, *y));
        if (input == 13)
            selected = true;
    }
    display_pop();
    panel_free(&overlay);
    return selected;
}

static bool
can_afford(game_t *game, yield_t yield)
{
    return
        (yield.food == 0 || game->food >= yield.food) &&
        (yield.wood == 0 || game->wood >= yield.wood) &&
        (yield.gold == 0 || game->gold >= yield.gold);
}

static bool
persist(game_t *game)
{
    FILE *save = fopen(PERSIST_FILE, "wb");
    if (save) {
        game_save(game, save);
        return fclose(save) == 0;
    }
    return false;
}

static game_t *atexit_save_game;
static void
atexit_save(void)
{
    if (atexit_save_game)
        persist(atexit_save_game);
}

int
main(void)
{
    int w, h;
    device_terminal_size(&w, &h);
    if (w < DISPLAY_WIDTH || h < DISPLAY_HEIGHT) {
        printf("Goblin-COM requires a terminal of at least %dx%d characters!\n"
               "Press enter to exit ...", DISPLAY_WIDTH, DISPLAY_HEIGHT);
        fflush(stdout);
        getchar();
        exit(EXIT_FAILURE);
    }
    printf("Initializing ...\n");
    device_entropy(&rand_state, sizeof(rand_state));
    display_init();
    device_title("Goblin-COM");

    panel_t loading;
    uint8_t loading_message[] = "Initializing world ...";
    panel_center_init(&loading, sizeof(loading_message), 1);
    display_push(&loading);
    panel_puts(&loading, 0, 0, FONT_DEFAULT, (char *)loading_message);
    display_refresh();
    game_t game;
    FILE *save = fopen(PERSIST_FILE, "rb");
    if (save) {
        game_load(&game, save);
        fclose(save);
        unlink(PERSIST_FILE);
    } else {
        game_init(&game, xorshift(&rand_state));
        game.speed = SPEED_FACTOR;
    }
    atexit_save_game = &game;
    atexit(atexit_save);
    display_pop();
    panel_free(&loading);

    panel_t sidemenu;
    panel_init(&sidemenu, DISPLAY_WIDTH - SIDEMENU_WIDTH, 0,
               SIDEMENU_WIDTH, DISPLAY_HEIGHT);
    display_push(&sidemenu);

    panel_t terrain;
    panel_init(&terrain, 0, 0, MAP_WIDTH, MAP_HEIGHT);
    display_push(&terrain);

    panel_t buildings;
    panel_init(&buildings, 0, 0, MAP_WIDTH, MAP_HEIGHT);
    display_push(&buildings);

    panel_t units;
    panel_init(&units, 0, 0, MAP_WIDTH, MAP_HEIGHT);
    display_push(&units);

    /* Main Loop */
    bool running = true;
    while (running) {
        yield_t diff;
        for (int i = 0; i < game.speed; i++)
            diff = game_step(&game);
        sidemenu_draw(&sidemenu, &game, diff);
        map_draw_terrain(game.map, &terrain);
        map_draw_buildings(game.map, &buildings);
        panel_clear(&units);
        game_draw_units(&game, &units);
        display_refresh();
        uint64_t wait = device_uepoch() % PERIOD;
        if (device_kbhit(wait)) {
            int key = device_getch();
            switch (key) {
            case 'b': {
                uint16_t building = popup_build_select(&game, &terrain);
                if (building) {
                    yield_t cost = building_cost(building);
                    if (!can_afford(&game, cost)) {
                        popup_error("Not enough funding/materials!");
                    } else {
                        int x, y;
                        if (select_position(&game, &terrain, &x, &y))
                            if (!game_build(&game, building, x, y))
                                popup_error("Invalid building location!");
                    }
                }
            } break;
            case '>':
            case '.':
                if (game.speed < SPEED_MAX)
                    game.speed *= SPEED_FACTOR;
                break;
            case '<':
            case ',':
                game.speed /= SPEED_FACTOR;
                if (game.speed == 0)
                    game.speed = 1;
                break;
            case 'R':
                display_invalidate();
                break;
            case 'q':
                running = !popup_quit(true);
                break;
            case 'Q':
                running = !popup_quit(false);
                if (!running)
                    atexit_save_game = NULL;
                break;
            default:
                popup_unknown_key(key);
                break;
            }
        }
    };

    atexit_save();
    atexit_save_game = NULL;
    game_free(&game);

    display_pop(); // units
    display_pop(); // buildings
    display_pop(); // terrain
    display_pop(); // sidemenu
    panel_free(&units);
    panel_free(&buildings);
    panel_free(&terrain);
    panel_free(&sidemenu);
    display_free();
    return 0;
}
