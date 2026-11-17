#ifndef CLASSIC_OS_SDL_SQLITE_ABI_HPP
#define CLASSIC_OS_SDL_SQLITE_ABI_HPP

#include <cstdint>
#include <cstddef>

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
extern "C" {

using Uint8  = uint8_t;
using Uint16 = uint16_t;
using Uint32 = uint32_t;
using Sint16 = int16_t;
using Sint32 = int32_t;
using SDL_JoystickID = Sint32;
using SDL_Keycode = Sint32;

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_RWops;
struct _SDL_Joystick;
using SDL_Joystick = _SDL_Joystick;
struct _SDL_GameController;
using SDL_GameController = _SDL_GameController;

struct SDL_Rect {
    int x, y;
    int w, h;
};

struct SDL_Color {
    Uint8 r, g, b, a;
};

struct SDL_PixelFormat;

struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat* format;
    int w, h;
    int pitch;
    void* pixels;
    void* userdata;
    int locked;
    void* lock_data;
    SDL_Rect clip_rect;
    void* map;
    int refcount;
};

struct SDL_RendererInfo {
    const char* name;
    Uint32 flags;
    Uint32 num_texture_formats;
    Uint32 texture_formats[16];
    int max_texture_width;
    int max_texture_height;
};

struct SDL_DisplayMode {
    Uint32 format;
    int w;
    int h;
    int refresh_rate;
    void* driverdata;
};

// Verified SDL_Init flags from game_manager @ 0x6d80 (0x2220)
constexpr Uint32 SDL_INIT_VIDEO          = 0x00000020u;
constexpr Uint32 SDL_INIT_JOYSTICK       = 0x00000200u;
constexpr Uint32 SDL_INIT_GAMECONTROLLER = 0x00002000u;

// Verified SDL_CreateWindow constants from game_manager @ 0x6d98..0x6da4
constexpr int    SDL_WINDOWPOS_UNDEFINED = 0x1FFF0000;
constexpr Uint32 SDL_WINDOW_FULLSCREEN   = 0x00000001u;
constexpr Uint32 SDL_WINDOW_FULLSCREEN_DESKTOP = 0x00000001u | 0x00001000u;
constexpr Uint32 SDL_WINDOW_SHOWN        = 0x00000004u;

// SDL_RendererFlags
constexpr Uint32 SDL_RENDERER_SOFTWARE      = 0x00000001u;
constexpr Uint32 SDL_RENDERER_ACCELERATED   = 0x00000002u;
constexpr Uint32 SDL_RENDERER_PRESENTVSYNC  = 0x00000004u;
constexpr Uint32 SDL_RENDERER_TARGETTEXTURE = 0x00000008u;

// BlendMode
enum SDL_BlendMode {
    SDL_BLENDMODE_NONE  = 0x00000000,
    SDL_BLENDMODE_BLEND = 0x00000001,
    SDL_BLENDMODE_ADD   = 0x00000002,
    SDL_BLENDMODE_MOD   = 0x00000004
};

enum SDL_RendererFlip {
    SDL_FLIP_NONE       = 0x00000000,
    SDL_FLIP_HORIZONTAL = 0x00000001,
    SDL_FLIP_VERTICAL   = 0x00000002
};

// SDL_GameController enums matching sdl_remap_arm.so .rodata @ 0x2f18
enum SDL_GameControllerButton {
    SDL_CONTROLLER_BUTTON_INVALID = -1,
    SDL_CONTROLLER_BUTTON_A = 0,
    SDL_CONTROLLER_BUTTON_B = 1,
    SDL_CONTROLLER_BUTTON_X = 2,
    SDL_CONTROLLER_BUTTON_Y = 3,
    SDL_CONTROLLER_BUTTON_BACK = 4,
    SDL_CONTROLLER_BUTTON_GUIDE = 5,
    SDL_CONTROLLER_BUTTON_START = 6,
    SDL_CONTROLLER_BUTTON_LEFTSTICK = 7,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK = 8,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER = 9,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER = 10,
    SDL_CONTROLLER_BUTTON_DPAD_UP = 11,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN = 12,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT = 13,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT = 14,
    SDL_CONTROLLER_BUTTON_MAX = 15
};

enum SDL_GameControllerAxis {
    SDL_CONTROLLER_AXIS_INVALID = -1,
    SDL_CONTROLLER_AXIS_LEFTX = 0,
    SDL_CONTROLLER_AXIS_LEFTY = 1,
    SDL_CONTROLLER_AXIS_RIGHTX = 2,
    SDL_CONTROLLER_AXIS_RIGHTY = 3,
    SDL_CONTROLLER_AXIS_TRIGGERLEFT = 4,
    SDL_CONTROLLER_AXIS_TRIGGERRIGHT = 5,
    SDL_CONTROLLER_AXIS_MAX = 6
};

// SDL_Event types & layout (56 bytes on 32-bit/64-bit SDL2)
enum SDL_EventType {
    SDL_QUIT                   = 0x100,
    SDL_KEYDOWN                = 0x300,
    SDL_KEYUP                  = 0x301,
    SDL_JOYAXISMOTION          = 0x600,
    SDL_JOYHATMOTION           = 0x602,
    SDL_JOYBUTTONDOWN          = 0x603,
    SDL_JOYBUTTONUP            = 0x604,
    SDL_JOYDEVICEADDED         = 0x605,
    SDL_JOYDEVICEREMOVED       = 0x606,
    SDL_CONTROLLERAXISMOTION   = 0x650,
    SDL_CONTROLLERBUTTONDOWN   = 0x651,
    SDL_CONTROLLERBUTTONUP     = 0x652,
    SDL_CONTROLLERDEVICEADDED  = 0x653,
    SDL_CONTROLLERDEVICEREMOVED= 0x654
};

constexpr Uint8 SDL_HAT_CENTERED = 0x00;
constexpr Uint8 SDL_HAT_UP       = 0x01;
constexpr Uint8 SDL_HAT_RIGHT    = 0x02;
constexpr Uint8 SDL_HAT_DOWN     = 0x04;
constexpr Uint8 SDL_HAT_LEFT     = 0x08;

// Keycodes for desktop testing & PSC GPIO keys
constexpr SDL_Keycode SDLK_RETURN    = '\r';
constexpr SDL_Keycode SDLK_ESCAPE    = 27;
constexpr SDL_Keycode SDLK_BACKSPACE = '\b';
constexpr SDL_Keycode SDLK_TAB       = '\t';
constexpr SDL_Keycode SDLK_SPACE     = ' ';
constexpr SDL_Keycode SDLK_z         = 'z';
constexpr SDL_Keycode SDLK_x         = 'x';
constexpr SDL_Keycode SDLK_a         = 'a';
constexpr SDL_Keycode SDLK_s         = 's';
constexpr SDL_Keycode SDLK_q         = 'q';
constexpr SDL_Keycode SDLK_w         = 'w';
constexpr SDL_Keycode SDLK_RIGHT     = (1 << 30) | 79;
constexpr SDL_Keycode SDLK_LEFT      = (1 << 30) | 80;
constexpr SDL_Keycode SDLK_DOWN      = (1 << 30) | 81;
constexpr SDL_Keycode SDLK_UP        = (1 << 30) | 82;

struct SDL_Keysym {
    Sint32 scancode;
    SDL_Keycode sym;
    Uint16 mod;
    Uint32 unused;
};

struct SDL_KeyboardEvent {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 state;
    Uint8 repeat;
    Uint8 padding2;
    Uint8 padding3;
    SDL_Keysym keysym;
};

struct SDL_JoyAxisEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 axis;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    Sint16 value;
    Uint16 padding4;
};

struct SDL_JoyHatEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 hat;
    Uint8 value;
    Uint8 padding1;
    Uint8 padding2;
};

struct SDL_JoyButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 button;
    Uint8 state;
    Uint8 padding1;
    Uint8 padding2;
};

struct SDL_JoyDeviceEvent {
    Uint32 type;
    Uint32 timestamp;
    Sint32 which;
};

struct SDL_ControllerAxisEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 axis;
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    Sint16 value;
    Uint16 padding4;
};

struct SDL_ControllerButtonEvent {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 button;
    Uint8 state;
    Uint8 padding1;
    Uint8 padding2;
};

struct SDL_ControllerDeviceEvent {
    Uint32 type;
    Uint32 timestamp;
    Sint32 which;
};

union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_JoyAxisEvent jaxis;
    SDL_JoyHatEvent jhat;
    SDL_JoyButtonEvent jbutton;
    SDL_JoyDeviceEvent jdevice;
    SDL_ControllerAxisEvent caxis;
    SDL_ControllerButtonEvent cbutton;
    SDL_ControllerDeviceEvent cdevice;
    Uint8 padding[56];
};

// Verified SDL2 function imports in Project Eris game_manager
int SDL_Init(Uint32 flags);
void SDL_Quit(void);
const char* SDL_GetError(void);
int SDL_SetHint(const char* name, const char* value);
const char* SDL_GetCurrentVideoDriver(void);
int SDL_GetCurrentDisplayMode(int displayIndex, SDL_DisplayMode* mode);
int SDL_GetNumVideoDrivers(void);
const char* SDL_GetVideoDriver(int index);
void SDL_ClearError(void);

SDL_Window* SDL_CreateWindow(const char* title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window* window);
void SDL_GetWindowSize(SDL_Window* window, int* w, int* h);

SDL_Renderer* SDL_CreateRenderer(SDL_Window* window, int index, Uint32 flags);
void SDL_DestroyRenderer(SDL_Renderer* renderer);
int SDL_GetRendererInfo(SDL_Renderer* renderer, SDL_RendererInfo* info);
int SDL_GetRendererOutputSize(SDL_Renderer* renderer, int* w, int* h);
int SDL_RenderSetLogicalSize(SDL_Renderer* renderer, int w, int h);
int SDL_SetRenderDrawBlendMode(SDL_Renderer* renderer, SDL_BlendMode blendMode);
int SDL_SetRenderDrawColor(SDL_Renderer* renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int SDL_RenderClear(SDL_Renderer* renderer);
int SDL_RenderFillRect(SDL_Renderer* renderer, const SDL_Rect* rect);
int SDL_RenderDrawRect(SDL_Renderer* renderer, const SDL_Rect* rect);
int SDL_RenderDrawLine(SDL_Renderer* renderer, int x1, int y1, int x2, int y2);
int SDL_RenderCopy(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect);
int SDL_RenderCopyEx(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect, const double angle, const void* center, const SDL_RendererFlip flip);
int SDL_RenderSetClipRect(SDL_Renderer* renderer, const SDL_Rect* rect);
void SDL_RenderPresent(SDL_Renderer* renderer);
int SDL_SetRenderTarget(SDL_Renderer* renderer, SDL_Texture* texture);

SDL_Surface* SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
void SDL_FreeSurface(SDL_Surface* surface);
int SDL_FillRect(SDL_Surface* dst, const SDL_Rect* rect, Uint32 color);

SDL_Texture* SDL_CreateTexture(SDL_Renderer* renderer, Uint32 format, int access, int w, int h);
SDL_Texture* SDL_CreateTextureFromSurface(SDL_Renderer* renderer, SDL_Surface* surface);
void SDL_DestroyTexture(SDL_Texture* texture);
int SDL_QueryTexture(SDL_Texture* texture, Uint32* format, int* access, int* w, int* h);
int SDL_SetTextureBlendMode(SDL_Texture* texture, SDL_BlendMode blendMode);
int SDL_SetTextureColorMod(SDL_Texture* texture, Uint8 r, Uint8 g, Uint8 b);
int SDL_SetTextureAlphaMod(SDL_Texture* texture, Uint8 alpha);

SDL_RWops* SDL_RWFromFile(const char* file, const char* mode);
int SDL_PollEvent(SDL_Event* event);
void SDL_PumpEvents(void);
void SDL_FlushEvents(Uint32 minType, Uint32 maxType);

int SDL_NumJoysticks(void);
int SDL_IsGameController(int joystick_index);
const char* SDL_JoystickNameForIndex(int device_index);
SDL_GameController* SDL_GameControllerOpen(int joystick_index);
void SDL_GameControllerClose(SDL_GameController* gamecontroller);
SDL_GameController* SDL_GameControllerFromInstanceID(SDL_JoystickID joyid);
Uint8 SDL_GameControllerGetButton(SDL_GameController* gamecontroller, SDL_GameControllerButton button);
Sint16 SDL_GameControllerGetAxis(SDL_GameController* gamecontroller, SDL_GameControllerAxis axis);
int SDL_GameControllerAddMappingsFromRW(SDL_RWops* rw, int freerw);

SDL_Joystick* SDL_JoystickOpen(int device_index);
void SDL_JoystickClose(SDL_Joystick* joystick);
const char* SDL_JoystickName(SDL_Joystick* joystick);
int SDL_JoystickNumButtons(SDL_Joystick* joystick);
int SDL_JoystickNumAxes(SDL_Joystick* joystick);
int SDL_JoystickNumHats(SDL_Joystick* joystick);
Uint8 SDL_JoystickGetButton(SDL_Joystick* joystick, int button);
Sint16 SDL_JoystickGetAxis(SDL_Joystick* joystick, int axis);
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick* joystick);

} // extern "C"
#endif

#if __has_include(<SDL2/SDL_image.h>)
#include <SDL2/SDL_image.h>
#else
extern "C" {
SDL_Surface* IMG_Load(const char* file);
}
#endif

#if __has_include(<SDL2/SDL_ttf.h>)
#include <SDL2/SDL_ttf.h>
#else
extern "C" {
struct _TTF_Font;
using TTF_Font = _TTF_Font;
int TTF_Init(void);
void TTF_Quit(void);
TTF_Font* TTF_OpenFont(const char* file, int ptsize);
void TTF_CloseFont(TTF_Font* font);
SDL_Surface* TTF_RenderUTF8_Blended(TTF_Font* font, const char* text, SDL_Color fg);
SDL_Surface* TTF_RenderUTF8_Blended_Wrapped(TTF_Font* font, const char* text, SDL_Color fg, Uint32 wrapLength);
}
#endif

#if __has_include(<sqlite3.h>)
#include <sqlite3.h>
#else
extern "C" {
struct sqlite3;
struct sqlite3_stmt;
constexpr int SQLITE_OK            = 0;
constexpr int SQLITE_ROW           = 100;
constexpr int SQLITE_DONE          = 101;
constexpr int SQLITE_OPEN_READONLY = 0x00000001;

int sqlite3_open_v2(const char* filename, sqlite3** ppDb, int flags, const char* zVfs);
int sqlite3_close(sqlite3* db);
int sqlite3_prepare_v2(sqlite3* db, const char* zSql, int nByte, sqlite3_stmt** ppStmt, const char** pzTail);
int sqlite3_step(sqlite3_stmt* pStmt);
int sqlite3_finalize(sqlite3_stmt* pStmt);
int sqlite3_column_int(sqlite3_stmt* pStmt, int iCol);
const unsigned char* sqlite3_column_text(sqlite3_stmt* pStmt, int iCol);
int sqlite3_column_bytes(sqlite3_stmt* pStmt, int iCol);
}
#endif

#endif // CLASSIC_OS_SDL_SQLITE_ABI_HPP
