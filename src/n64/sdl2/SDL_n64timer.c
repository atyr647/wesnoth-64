/*
 * SDL_n64timer.c -- SDL timer subsystem for the Nintendo 64.
 *
 * Replaces src/timer/SDL_timer.c and the per-platform SDL_systimer.c.
 *
 * Ticks come from the CPU count register (libdragon's get_ticks(), 46.875 MHz).
 *
 * SDL normally runs SDL_AddTimer callbacks on a timer thread. The console has
 * no threads, so due callbacks are run from SDL_PumpEvents() (the N64 video
 * driver's PumpEvents calls SDL_N64_RunTimers()), i.e. from the thread that
 * polls events -- the same thread the program's event handling runs on.
 * Wesnoth's timers (gui2 timers, tooltips, scrolling repeat) only push events
 * from their callbacks, which is exactly what they would do from a thread.
 */
#include "SDL_internal.h"
#include "SDL_timer.h"
#include "timer/SDL_timer_c.h"

#include <n64sys.h>

/* ---------------------------------------------------------------- ticks */

static SDL_bool ticks_started = SDL_FALSE;
static uint64_t start_ticks;

void SDL_TicksInit(void)
{
    if (ticks_started) {
        return;
    }
    ticks_started = SDL_TRUE;
    start_ticks = get_ticks();
}

void SDL_TicksQuit(void)
{
    ticks_started = SDL_FALSE;
}

Uint64 SDL_GetTicks64(void)
{
    if (!ticks_started) {
        SDL_TicksInit();
    }
    return (Uint64)TICKS_TO_MS(get_ticks() - start_ticks);
}

Uint32 SDL_GetTicks(void)
{
    return (Uint32)(SDL_GetTicks64() & 0xFFFFFFFF);
}

Uint64 SDL_GetPerformanceCounter(void)
{
    return (Uint64)get_ticks();
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return (Uint64)TICKS_PER_SECOND;
}

void SDL_N64_RunTimers(void);
void SDL_N64_AudioPump(void);

void SDL_Delay(Uint32 ms)
{
    const uint64_t end = get_ticks() + TICKS_FROM_MS((uint64_t)ms);
    while (get_ticks() < end) {
        SDL_N64_RunTimers();
        SDL_N64_AudioPump();
    }
}

/* --------------------------------------------------------------- timers */

typedef struct SDL_N64Timer
{
    SDL_TimerID id;
    SDL_TimerCallback callback;
    void *param;
    Uint32 interval;
    Uint64 due;                 /* in SDL_GetTicks64() milliseconds */
    SDL_bool removed;
    struct SDL_N64Timer *next;
} SDL_N64Timer;

static SDL_N64Timer *timers;
static SDL_TimerID next_id;
static int running;             /* re-entrancy guard */

int SDL_TimerInit(void)
{
    return 0;
}

void SDL_TimerQuit(void)
{
    while (timers) {
        SDL_N64Timer *t = timers;
        timers = t->next;
        SDL_free(t);
    }
}

SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *param)
{
    SDL_N64Timer *t = (SDL_N64Timer *)SDL_calloc(1, sizeof(*t));
    if (!t) {
        SDL_OutOfMemory();
        return 0;
    }
    t->id = ++next_id;
    t->callback = callback;
    t->param = param;
    t->interval = interval;
    t->due = SDL_GetTicks64() + interval;
    t->next = timers;
    timers = t;
    return t->id;
}

SDL_bool SDL_RemoveTimer(SDL_TimerID id)
{
    SDL_N64Timer *t;
    for (t = timers; t; t = t->next) {
        if (t->id == id && !t->removed) {
            t->removed = SDL_TRUE;      /* freed by the next SDL_N64_RunTimers() */
            return SDL_TRUE;
        }
    }
    return SDL_FALSE;
}

void SDL_N64_RunTimers(void)
{
    SDL_N64Timer **link, *t;
    Uint64 now;

    if (running) {
        return;
    }
    running = 1;
    now = SDL_GetTicks64();
    for (t = timers; t; t = t->next) {
        if (!t->removed && now >= t->due) {
            Uint32 next = t->callback(t->interval, t->param);
            if (t->removed) {
                continue;               /* removed itself from the callback */
            }
            if (next == 0) {
                t->removed = SDL_TRUE;
            } else {
                t->interval = next;
                t->due = now + next;
            }
        }
    }
    link = &timers;
    while ((t = *link) != NULL) {
        if (t->removed) {
            *link = t->next;
            SDL_free(t);
        } else {
            link = &t->next;
        }
    }
    running = 0;
}
