/*
 * portal.h - Skylanders Portal of Power protocol engine (no PS4 code in here, so it can be tested on a PC).
 *
 * Protocol reference: RPCS3 (Emu/Io/Skylander.cpp) and Dolphin (IOS/USB/Emulated/Skylanders).
 * Packets are 32 bytes. The game sends commands with a HID SET_REPORT control transfer; the portal answers on
 * interrupt endpoint 0x81. When it has nothing else to say it sends a status ('S') report.
 */
#ifndef PORTAL_H
#define PORTAL_H
#include <stdint.h>

#define SKY_SLOTS 8          /* a wired portal tracks 8 figures */
#define SKY_SIZE 1024        /* 64 blocks x 16 bytes */
#define SKY_QMAX 8

/* status values (2 bits per figure in the 'S' report) */
#define ST_NONE 0
#define ST_READY 1
#define ST_REMOVING 2
#define ST_ADDED 3

typedef struct {
    uint8_t status;
    uint8_t q[SKY_QMAX];
    uint8_t qn;
    uint8_t dirty;
    uint32_t dirty_tick;
    uint32_t last_uid;
    uint8_t data[SKY_SIZE];
} SkySlot;

typedef struct {
    SkySlot slot[SKY_SLOTS];
    uint8_t activated;
    uint8_t counter;
    uint8_t r, g, b;          /* LED colour the game asked for (shown in the web UI) */
    uint8_t portal_id[2];     /* bytes 1..2 of the 'R' reply */
    uint32_t tick;
    uint32_t cmds;            /* number of commands seen (0 = game has not talked to us yet) */
    /* experimental switches (tweaks.txt), all 0 = behaviour of v0.1.6 */
    uint8_t opt_a_arg;        /* 'A' 00 turns the portal off instead of on */
    uint8_t opt_status_active;/* byte 6 of every 'S' report is 1, even before 'A' */
} Portal;

void portal_init(Portal *p);

/* game -> portal: one SET_REPORT payload (len 1..32). If a reply must go out on the interrupt endpoint it is
 * written to reply[32] and 1 is returned, else 0. */
int portal_command(Portal *p, const uint8_t *buf, int len, uint8_t *reply);

/* builds the 'S' status report (used when no other reply is waiting) */
void portal_status(Portal *p, uint8_t *reply);

/* figure management. place: slot -1 = first free (prefers the slot this figure used last).
 * Returns the slot used or -1. remove: returns 1 if a figure was there. */
int portal_place(Portal *p, int slot, const uint8_t *data);
int portal_remove(Portal *p, int slot);
int portal_present(const Portal *p, int slot);

/* figure info from block 1 */
uint16_t sky_figure_id(const uint8_t *data);
uint16_t sky_variant(const uint8_t *data);

#endif
