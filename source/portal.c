/*
 * portal.c - Skylanders Portal of Power protocol engine. See portal.h.
 * The caller is responsible for locking (every function here assumes it owns the Portal).
 */
#include "portal.h"
#include <string.h>

static void qpush(SkySlot *s, uint8_t v)
{
    if (s->qn < SKY_QMAX)
        s->q[s->qn++] = v;
}

static uint8_t qpop(SkySlot *s)
{
    uint8_t v = s->q[0];
    memmove(s->q, s->q + 1, SKY_QMAX - 1);
    s->qn--;
    return v;
}

static uint32_t rd32(const uint8_t *d) { return d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24); }

uint16_t sky_figure_id(const uint8_t *d) { return (uint16_t)(d[0x10] | (d[0x11] << 8)); }
uint16_t sky_variant(const uint8_t *d) { return (uint16_t)(d[0x1C] | (d[0x1D] << 8)); }

void portal_init(Portal *p)
{
    memset(p, 0, sizeof(*p));
    p->portal_id[0] = 0x02; /* 'R' reply used by shadPS4 (PS4 emulator) and Dolphin */
    p->portal_id[1] = 0x1b;
}

int portal_present(const Portal *p, int slot)
{
    return slot >= 0 && slot < SKY_SLOTS && (p->slot[slot].status & 1);
}

/* 'A': the game turns the portal on. Figures already standing on it are announced again. */
static void activate(Portal *p)
{
    int i;
    if (p->activated)
        return;
    for (i = 0; i < SKY_SLOTS; i++)
        if (p->slot[i].status & 1) {
            qpush(&p->slot[i], ST_ADDED);
            qpush(&p->slot[i], ST_READY);
        }
    p->activated = 1;
}

/* 'R': reset. Pending status changes collapse to their end result. */
static void deactivate(Portal *p)
{
    int i;
    for (i = 0; i < SKY_SLOTS; i++) {
        SkySlot *s = &p->slot[i];
        if (s->qn) {
            s->status = s->q[s->qn - 1];
            s->qn = 0;
        }
        s->status &= 1;
    }
    p->activated = 0;
}

void portal_status(Portal *p, uint8_t *reply)
{
    uint32_t st = 0;
    int i;
    for (i = SKY_SLOTS - 1; i >= 0; i--) {
        SkySlot *s = &p->slot[i];
        if (s->qn)
            s->status = qpop(s);
        st = (st << 2) | (s->status & 3);
    }
    memset(reply, 0, 32);
    reply[0] = 'S';
    reply[1] = (uint8_t)st;
    reply[2] = (uint8_t)(st >> 8);
    reply[3] = (uint8_t)(st >> 16);
    reply[4] = (uint8_t)(st >> 24);
    reply[5] = p->counter++;
    reply[6] = (p->activated || p->opt_status_active) ? 1 : 0;
}

int portal_command(Portal *p, const uint8_t *buf, int len, uint8_t *reply)
{
    uint8_t n, block;
    if (len < 1)
        return 0;
    memset(reply, 0, 32);
    p->cmds++;
    switch (buf[0]) {
    case 'A': /* activate */
        reply[0] = 'A';
        reply[1] = len > 1 ? buf[1] : 1;
        reply[2] = 0xFF;
        reply[3] = 0x77;
        if (p->opt_a_arg && len > 1 && buf[1] == 0)
            p->activated = 0; /* 'A' 00 = off */
        else
            activate(p);
        return 1;
    case 'C': /* LED colour */
        if (len >= 4) {
            p->r = buf[1];
            p->g = buf[2];
            p->b = buf[3];
        }
        return 0;
    case 'J': /* side-light fade (Trap Team portal) */
        reply[0] = 'J';
        return 1;
    case 'L': /* Trap Team side lights - nothing to do */
        return 0;
    case 'M': /* audio firmware version: 0 means "no speaker", so the game will not stream audio */
        reply[0] = 'M';
        reply[1] = len > 1 ? buf[1] : 0;
        reply[2] = 0x00;
        reply[3] = 0x19;
        return 1;
    case 'Q': /* read a block */
        if (len < 3)
            return 0;
        n = buf[1] & 0x0F;
        block = buf[2];
        reply[0] = 'Q';
        reply[2] = block;
        if (n < SKY_SLOTS && block < 64 && (p->slot[n].status & 1)) {
            reply[1] = (uint8_t)(0x10 | n);
            memcpy(reply + 3, p->slot[n].data + 16 * block, 16);
        } else {
            reply[1] = 0x01; /* error: no figure there */
        }
        return 1;
    case 'R': /* reset */
        reply[0] = 'R';
        reply[1] = p->portal_id[0];
        reply[2] = p->portal_id[1];
        deactivate(p);
        return 1;
    case 'S': /* status request - the status reports are sent anyway */
        return 0;
    case 'V': /* unknown, no reply */
        return 0;
    case 'W': /* write a block */
        if (len < 19)
            return 0;
        n = buf[1] & 0x0F;
        block = buf[2];
        reply[0] = 'W';
        reply[2] = block;
        if (n < SKY_SLOTS && block < 64 && (p->slot[n].status & 1)) {
            reply[1] = (uint8_t)(0x10 | n);
            memcpy(p->slot[n].data + 16 * block, buf + 3, 16);
            p->slot[n].dirty = 1;
            p->slot[n].dirty_tick = p->tick;
        } else {
            reply[1] = 0x01;
        }
        return 1;
    default:
        return 0;
    }
}

int portal_place(Portal *p, int slot, const uint8_t *data)
{
    uint32_t uid = rd32(data);
    SkySlot *s;
    int i;
    if (slot >= SKY_SLOTS)
        return -1;
    if (slot >= 0) {
        if (p->slot[slot].status & 1)
            return -1;
    } else {
        for (i = 0; i < SKY_SLOTS && slot < 0; i++) /* the spot this figure had before */
            if (!(p->slot[i].status & 1) && p->slot[i].last_uid == uid && uid)
                slot = i;
        for (i = 0; i < SKY_SLOTS && slot < 0; i++) /* a free spot whose "removed" news was delivered */
            if (!(p->slot[i].status & 1) && p->slot[i].qn == 0)
                slot = i;
        for (i = 0; i < SKY_SLOTS && slot < 0; i++)
            if (!(p->slot[i].status & 1))
                slot = i;
        if (slot < 0)
            return -1;
    }
    s = &p->slot[slot];
    memcpy(s->data, data, SKY_SIZE);
    s->status = ST_ADDED;
    s->qn = 0;
    qpush(s, ST_ADDED);
    qpush(s, ST_READY);
    s->last_uid = uid;
    s->dirty = 0;
    return slot;
}

int portal_remove(Portal *p, int slot)
{
    SkySlot *s;
    if (slot < 0 || slot >= SKY_SLOTS)
        return 0;
    s = &p->slot[slot];
    if (!(s->status & 1))
        return 0;
    s->status = ST_REMOVING;
    s->qn = 0;
    qpush(s, ST_REMOVING);
    qpush(s, ST_NONE);
    return 1;
}
