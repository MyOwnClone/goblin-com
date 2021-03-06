#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "map.h"
#include "rand.h"

#define WORK_SIZE 4097
#define NOISE_SCALE 4.0f

static size_t
grow(const float *map, size_t size, float *out, uint64_t *seed)
{
    size_t osize = (size - 1) * 2 + 1;
    /* Copy */
    for (size_t y = 0; y < size; y++) {
        for (size_t x = 0; x < size; x++) {
            out[y * 2 * osize + x * 2] = map[y * size + x];
        }
    }
    /* Diamond */
    for (size_t y = 1; y < osize; y += 2) {
        for (size_t x = 1; x < osize; x += 2) {
            int count = 0;
            float sum = 0;
            for (int dy = -1; dy <= 1; dy += 2) {
                for (int dx = -1; dx <= 1; dx += 2) {
                    long i = (y + dy) * osize + (x + dx);
                    if (i >= 0 && (size_t)i < osize * osize) {
                        sum += out[i];
                        count++;
                    }
                }
            }
            float u = rand_uniform_s(seed, -1, 1);
            out[y * osize + x] = sum / count + u / osize * NOISE_SCALE;
        }
    }
    /* Square */
    struct {
        int x, y;
    } pos[] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (size_t y = 1; y < osize; y++) {
        for (size_t x = (y + 1) % 2; x < osize; x += 2) {
            int count = 0;
            float sum = 0;
            for (int p = 0; p < 4; p++) {
                long i = (y + pos[p].y) * osize + (x + pos[p].x);
                if (i >= 0 && (size_t)i < osize * osize) {
                    sum += out[i];
                    count++;
                }
            }
            float u = rand_uniform_s(seed, -1, 1);
            out[y * osize + x] = sum / count + u / osize  * NOISE_SCALE;
        }
    }
    return osize;
}

static void
summarize(map_t *map, uint64_t *seed)
{
    for (size_t y = 0; y < MAP_HEIGHT; y++) {
        for (size_t x = 0; x < MAP_WIDTH; x++) {
            float mean = 0;
            for (size_t sy = 0; sy < MAP_HEIGHT; sy++) {
                for (size_t sx = 0; sx < MAP_WIDTH; sx++) {
                    size_t ix = x * MAP_WIDTH + sx;
                    size_t iy = y * MAP_HEIGHT + sy;
                    mean += map->low[ix][iy].height;
                }
            }
            mean /= (MAP_WIDTH * MAP_HEIGHT);
            float std = 0;
            for (size_t sy = 0; sy < MAP_HEIGHT; sy++) {
                for (size_t sx = 0; sx < MAP_WIDTH; sx++) {
                    size_t ix = x * MAP_WIDTH + sx;
                    size_t iy = y * MAP_HEIGHT + sy;
                    float diff = mean - map->low[ix][iy].height;
                    std += diff * diff;
                }
            }
            std = sqrt(std / (MAP_HEIGHT * MAP_WIDTH));
            enum map_base base;
            if (mean < -0.8)
                base = BASE_OCEAN;
            else if (mean < -0.6)
                base = BASE_COAST;
            else if (mean < -0.5)
                base = BASE_SAND;
            else if (std > 0.05)
                base = BASE_MOUNTAIN;
            else if (std > 0.04)
                base = BASE_HILL;
            else if (rand_uniform_s(seed, -1, 1) > -0.2)
                base = BASE_GRASSLAND;
            else
                base = BASE_FOREST;
            map->high[x][y].base = base;
            map->high[x][y].building = 0;
        }
    }
}

map_t *
map_generate(uint64_t seed)
{
    map_t *map = malloc(sizeof(*map));
    size_t alloc_size = WORK_SIZE * WORK_SIZE * sizeof(float);
    float *buf_a = calloc(alloc_size, 1);
    float *buf_b = calloc(alloc_size, 1);
    float *heightmap = buf_a;
    for (int i = 0; i < 4; i++)
        heightmap[i] = rand_uniform_s(&seed, -1, 1);
    size_t size = 3;
    while (size < WORK_SIZE) {
        size = grow(buf_a, size, buf_b, &seed);
        heightmap = buf_b;
        buf_b = buf_a;
        buf_a = heightmap;
    }
    for (size_t y = 0; y < MAP_HEIGHT * MAP_HEIGHT; y++) {
        for (size_t x = 0; x < MAP_WIDTH * MAP_WIDTH; x++) {
            float height = heightmap[y * WORK_SIZE + x];
            float sx = x / (float)(MAP_WIDTH * MAP_WIDTH) - 0.5;
            float sy = y / (float)(MAP_HEIGHT * MAP_HEIGHT) - 0.5;
            float s = sqrt(sx * sx + sy * sy) * 3 - 0.45f;
            map->low[x][y].height = height - s;
        }
    }
    free(buf_a);
    free(buf_b);
    summarize(map, &seed);
    return map;
}

void
map_free(map_t *map)
{
    free(map);
}

static font_t
base_font(enum map_base base, int x, int y)
{
    font_t font;
    switch (base) {
    case BASE_OCEAN:
        font = FONT(B, b);
        break;
    case BASE_COAST: {
        font = FONT(w, b);
        float dx = (x / (float)MAP_WIDTH) - 0.5;
        float dy = (y / (float)MAP_HEIGHT) - 0.5;
        dx *= 1.3;
        float dist = sqrt(dx * dx + dy * dy) * 100;
        float offset = fmod(device_uepoch() / 500000.0, PI * 2);
        font.fore_bright = sinf(dist + offset) < 0 ? true : false;
    } break;
    case BASE_GRASSLAND:
        font = FONT(G, g);
        break;
    case BASE_FOREST:
        font = FONT(G, g);
        break;
    case BASE_HILL:
        font = FONT(K, g);
        break;
    case BASE_MOUNTAIN:
        font = FONT(w, g);
        break;
    case BASE_SAND:
        font = FONT(Y, Y);
        break;
    }
    return font;
}

void
map_draw_terrain(map_t *map, panel_t *p)
{
    for (size_t y = 0; y < MAP_HEIGHT; y++) {
        for (size_t x = 0; x < MAP_WIDTH; x++) {
            uint16_t c = map->high[x][y].base;
            font_t font = base_font(c, x, y);
            panel_putc(p, x, y, font, c);
        }
    }
}

void
map_draw_buildings(map_t *map, panel_t *p)
{
    for (size_t y = 0; y < MAP_HEIGHT; y++) {
        for (size_t x = 0; x < MAP_WIDTH; x++) {
            enum building building = map->high[x][y].building;
            if (building != C_NONE) {
                uint16_t c = building;
                font_t font = FONT(Y, k);
                if (map->high[x][y].building_age < 0) {
                    font.fore = COLOR_CYAN;
                    c = tolower(c);
                }
                panel_putc(p, x, y, font, c);
            }
        }
    }
}

static inline bool
is_valid_xy(int x, int y)
{
    return x >= 0 && x < MAP_WIDTH && y >= 0 && y < MAP_HEIGHT;
}

uint16_t
map_base(map_t *map, int x, int y)
{
    return is_valid_xy(x, y) ? map->high[x][y].base : BASE_OCEAN;
}

uint16_t
map_building(map_t *map, int x, int y)
{
    return is_valid_xy(x, y) ? map->high[x][y].building : C_NONE;
}
