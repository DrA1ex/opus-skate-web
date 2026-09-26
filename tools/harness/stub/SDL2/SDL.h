// Adapted from adeism/OSkate arena/01a0cbce-oskate (commit 4bdcb144).
#pragma once
#include <cstdint>
#include <cstddef>
typedef uint8_t Uint8; typedef uint32_t Uint32; typedef uint64_t Uint64;
typedef int32_t Sint32;
typedef struct SDL_Window SDL_Window;
typedef void* SDL_GLContext;
typedef uint32_t SDL_AudioDeviceID;
typedef enum {
 SDL_SCANCODE_UNKNOWN=0, SDL_SCANCODE_A=4,SDL_SCANCODE_B,SDL_SCANCODE_C,SDL_SCANCODE_D,SDL_SCANCODE_E,SDL_SCANCODE_F,SDL_SCANCODE_G,
 SDL_SCANCODE_H,SDL_SCANCODE_I,SDL_SCANCODE_J,SDL_SCANCODE_K,SDL_SCANCODE_L,SDL_SCANCODE_M,SDL_SCANCODE_N,SDL_SCANCODE_O,SDL_SCANCODE_P,
 SDL_SCANCODE_Q,SDL_SCANCODE_R,SDL_SCANCODE_S,SDL_SCANCODE_T,SDL_SCANCODE_U,SDL_SCANCODE_V,SDL_SCANCODE_W,SDL_SCANCODE_X,SDL_SCANCODE_Y,SDL_SCANCODE_Z,
 SDL_SCANCODE_1=30,SDL_SCANCODE_RETURN=40,SDL_SCANCODE_ESCAPE=41,SDL_SCANCODE_SPACE=44,
 SDL_SCANCODE_LEFT=80,SDL_SCANCODE_RIGHT,SDL_SCANCODE_DOWN,SDL_SCANCODE_UP,
 SDL_SCANCODE_F1=58,SDL_SCANCODE_F3,SDL_SCANCODE_F11,
 SDL_SCANCODE_LSHIFT=225,SDL_SCANCODE_RSHIFT,SDL_SCANCODE_KP_ENTER=88
} SDL_Scancode;
typedef enum { SDL_KEYDOWN=0x300, SDL_KEYUP=0x301, SDL_QUIT=0x100 } SDL_EventType;
typedef struct { SDL_Scancode scancode; } SDL_Keysym;
typedef struct { Uint32 type; Uint8 repeat; SDL_Keysym keysym; } SDL_KeyboardEvent;
typedef union { Uint32 type; SDL_KeyboardEvent key; } SDL_Event;
typedef void (*SDL_AudioCallback)(void*, Uint8*, int);
typedef struct { int freq; uint16_t format; uint8_t channels; uint16_t samples; SDL_AudioCallback callback; void* userdata; } SDL_AudioSpec;
#define SDL_INIT_VIDEO 0x20u
#define SDL_INIT_AUDIO 0x10u
#define SDL_INIT_EVENTS 0x4000u
#define AUDIO_F32SYS 0x8120
#define SDL_WINDOWPOS_CENTERED 0x2FFF0000u
#define SDL_WINDOW_OPENGL 0x2u
#define SDL_WINDOW_RESIZABLE 0x20u
#define SDL_WINDOW_HIDDEN 0x8u
#define SDL_WINDOW_FULLSCREEN_DESKTOP 0x1001u
#define SDL_GL_CONTEXT_MAJOR_VERSION 17
#define SDL_GL_CONTEXT_MINOR_VERSION 18
#define SDL_GL_CONTEXT_PROFILE_MASK 21
#define SDL_GL_CONTEXT_PROFILE_CORE 1
#define SDL_GL_CONTEXT_FLAGS 22
#define SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG 2
#define SDL_GL_DOUBLEBUFFER 5
#define SDL_GL_DEPTH_SIZE 6
#define SDL_GL_MULTISAMPLEBUFFERS 13
#define SDL_GL_MULTISAMPLESAMPLES 14
#define SDL_zero(x) __builtin_memset(&(x), 0, sizeof(x))
static inline int SDL_Init(Uint32) { return 0; }
static inline void SDL_Quit(void) {}
static inline const char* SDL_GetError(void) { return ""; }
static inline SDL_Window* SDL_CreateWindow(const char*, int, int, int, int, Uint32) { return nullptr; }
static inline void SDL_DestroyWindow(SDL_Window*) {}
static inline SDL_GLContext SDL_GL_CreateContext(SDL_Window*) { return nullptr; }
static inline void SDL_GL_DeleteContext(SDL_GLContext) {}
static inline int SDL_GL_SetAttribute(int, int) { return 0; }
static inline void* SDL_GL_GetProcAddress(const char*) { return nullptr; }
static inline int SDL_GL_SetSwapInterval(int) { return 0; }
static inline void SDL_GL_SwapWindow(SDL_Window*) {}
static inline void SDL_GL_GetDrawableSize(SDL_Window*, int*, int*) {}
static inline int SDL_PollEvent(SDL_Event*) { return 0; }
static inline const Uint8* SDL_GetKeyboardState(int*) { return nullptr; }
static inline Uint64 SDL_GetPerformanceCounter(void) { return 0; }
static inline Uint64 SDL_GetPerformanceFrequency(void) { return 1; }
static inline int SDL_SetWindowFullscreen(SDL_Window*, Uint32) { return 0; }
static inline SDL_AudioDeviceID SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec*, SDL_AudioSpec*, int) { return 0; }
static inline void SDL_PauseAudioDevice(SDL_AudioDeviceID, int) {}
static inline void SDL_LockAudioDevice(SDL_AudioDeviceID) {}
static inline void SDL_UnlockAudioDevice(SDL_AudioDeviceID) {}
static inline void SDL_CloseAudioDevice(SDL_AudioDeviceID) {}
