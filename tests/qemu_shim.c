/* Test-only LD_PRELOAD shim for qemu-arm runs of classic_os (NOT shipped to the console).
 *
 *  COS_TEST_EVENTS  space-separated script, one event per frame, e.g. "J9 J2"
 *                     Jn  raw joystick button n        (PSC map: 2=Cross 9=Start ...)
 *                     Cn  SDL game-controller button n (SDL numbering)
 *                     Hn  raw joystick hat value n     (1=up 2=right 4=down 8=left)
 *                     Ax  raw joystick axis motion: A0- A0+ A1- A1+ (A1+ = down, A1- = up); A10 / A00 = release
 *  COS_TEST_DUMP    path: write a BMP of frame COS_TEST_DUMP_FRAME (default 20) via SDL_RenderReadPixels
 *  COS_TEST_LOG     path: log every injected event and the frame count
 * Events are injected only after 10 frames, one per frame, so the UI has been drawn first.
 */
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int frames = 0, polls_in_frame = 0, next_ev = 0, nev = 0;
static char evs[64][8];

static void load_script(void) {
    static int done = 0; if (done) return; done = 1;
    const char* s = getenv("COS_TEST_EVENTS"); if (!s) return;
    char buf[1024]; strncpy(buf, s, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    for (char* t = strtok(buf, " "); t && nev < 64; t = strtok(NULL, " ")) { strncpy(evs[nev], t, 7); evs[nev][7] = 0; nev++; }
}
static void tlog(const char* m) { const char* p = getenv("COS_TEST_LOG"); if (!p) return; FILE* f = fopen(p, "a"); if (f) { fputs(m, f); fputc('\n', f); fclose(f); } }

int SDL_PollEvent(SDL_Event* e) {
    static int (*real)(SDL_Event*) = 0; if (!real) real = dlsym(RTLD_NEXT, "SDL_PollEvent");
    load_script();
    if (frames >= 10 && next_ev < nev && polls_in_frame == 0) {
        const char* t = evs[next_ev++]; memset(e, 0, sizeof *e);
        char m[64]; snprintf(m, sizeof m, "inject frame=%d %s", frames, t); tlog(m);
        int n = atoi(t + 1);
        switch (t[0]) {
            case 'J': e->type = SDL_JOYBUTTONDOWN; e->jbutton.which = 999; e->jbutton.button = (Uint8)n; e->jbutton.state = 1; break;
            case 'C': e->type = SDL_CONTROLLERBUTTONDOWN; e->cbutton.which = 999; e->cbutton.button = (Uint8)n; e->cbutton.state = 1; break;
            case 'H': e->type = SDL_JOYHATMOTION; e->jhat.which = 999; e->jhat.value = (Uint8)n; break;
            case 'A': e->type = SDL_JOYAXISMOTION; e->jaxis.which = 999; e->jaxis.axis = (Uint8)(t[1] - '0'); e->jaxis.value = (t[2] == '+') ? 30000 : ((t[2] == '-') ? -30000 : 0); break;
            default: return real(e);
        }
        polls_in_frame = 1; return 1;
    }
    polls_in_frame = 1;
    return real(e);
}

void SDL_RenderPresent(SDL_Renderer* r) {
    static void (*real)(SDL_Renderer*) = 0; if (!real) real = dlsym(RTLD_NEXT, "SDL_RenderPresent");
    const char* dump = getenv("COS_TEST_DUMP"); const char* fr = getenv("COS_TEST_DUMP_FRAME");
    if (dump && frames == (fr ? atoi(fr) : 20)) {
        int w = 0, h = 0; SDL_GetRendererOutputSize(r, &w, &h);
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (s && SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
            SDL_SaveBMP(s, dump);
            char m[96]; snprintf(m, sizeof m, "dump frame=%d %dx%d -> %s", frames, w, h, dump); tlog(m);
        } else tlog("dump FAILED");
        if (s) SDL_FreeSurface(s);
    }
    real(r); frames++; polls_in_frame = 0;
}
