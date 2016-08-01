#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "luisavm.h"
#include <SDL2/SDL.h>

#define WIDTH  318
#define HEIGHT 234
#define BORDER  10
#define ZOOM     3

static SDL_Renderer* ren = NULL;
static SDL_Color pal[256] = { 0 };

typedef struct Sprites {
    uint32_t n_sprites;
    SDL_Texture** sprite;
} Sprites;
static Sprites sprites = { 0, NULL };


//
// CALLBACKS
//

static void setpal(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    pal[idx].r = r;
    pal[idx].g = g;
    pal[idx].b = b;
}


static void clrscr(uint8_t color)
{
    SDL_SetRenderDrawColor(ren, pal[color].r, pal[color].g, pal[color].b, SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(ren, &(SDL_Rect) { BORDER * ZOOM, BORDER * ZOOM, WIDTH * ZOOM, HEIGHT * ZOOM });
}


static uint32_t upload_sprite(uint16_t w, uint16_t h, uint8_t* data)
{
    Uint32 rmask = 0xff000000;
    Uint32 gmask = 0x00ff0000;
    Uint32 bmask = 0x0000ff00;
    Uint32 amask = 0x000000ff;

    SDL_Surface* sf = SDL_CreateRGBSurface(0, w * ZOOM, h * ZOOM, 32, rmask, gmask, bmask, amask);
    for(size_t x=0; x<w; ++x) {
        for(size_t y=0; y<h; ++y) {
            // TODO - draw points directly
            uint8_t idx = data[x+(y*h)];
            SDL_FillRect(sf, 
                    &(SDL_Rect) { (int)x, (int)y, 1, 1 },
                    ((Uint32)pal[idx].r << 24) | ((Uint32)pal[idx].g << 16) | ((Uint32)pal[idx].b << 8) | 0xFF);
        }
    }

    SDL_Texture* tx = SDL_CreateTextureFromSurface(ren, sf);

    sprites.sprite = realloc(sprites.sprite, sizeof(SDL_Texture*) * (sprites.n_sprites + 1));
    sprites.sprite[sprites.n_sprites] = tx;
    return sprites.n_sprites++;
}


static void draw_sprite(uint32_t sprite_idx, uint16_t pos_x, uint16_t pos_y)
{
    assert(sprites.sprite[sprite_idx]);

    Uint32 format;
    int access, w, h;
    SDL_QueryTexture(sprites.sprite[sprite_idx], &format, &access, &w, &h);
    SDL_RenderCopy(ren, sprites.sprite[sprite_idx], NULL, 
            &(SDL_Rect) { (pos_x+BORDER) * ZOOM, (pos_y+BORDER) * ZOOM, w * ZOOM, h * ZOOM });
}

// 
// MAIN
//

static bool get_events(LVM_Computer* comp)
{
    (void) comp;

    SDL_Event e;
    while(SDL_PollEvent(&e)) {
        switch(e.type) {
            case SDL_QUIT:
                return false;
        }
    }
    return true;
}


int main()
{
    // 
    // initialization
    //

    LVM_Computer* computer = lvm_computercreate(128 * 1024);

    if(SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    SDL_Window* window = SDL_CreateWindow("luisavm " VERSION, 
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            (WIDTH + (2*BORDER)) * ZOOM, (HEIGHT + (2 * BORDER)) * ZOOM, 0);
    if(!window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_TARGETTEXTURE);
    if(!ren) {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0xFF);
    SDL_RenderClear(ren);
    SDL_RenderPresent(ren);

    //
    // setup video device
    //
#define CB(name) .name = name
    lvm_setupvideo(computer, (VideoCallbacks) {
        CB(setpal),
        CB(clrscr),
        CB(upload_sprite),
        CB(draw_sprite),
    });
#undef CB

    // 
    // main loop
    //

    Uint32 last_frame = SDL_GetTicks();
    while(1) {
        lvm_step(computer, 0);
        if(SDL_GetTicks() >= last_frame + 16) {
            last_frame = SDL_GetTicks();
            SDL_RenderPresent(ren);
            if(get_events(computer) == 0) {
                break;
            }
        }
    }

    // 
    // finalization
    //
    
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(window);
    SDL_Quit();

    lvm_computerdestroy(computer);
}
