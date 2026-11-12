/* PC test for portal.c: plays the part of the game talking to the portal. Run: make test [FIG=path.bin] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portal.h"

static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static uint32_t status_bits(const uint8_t *r) { return r[1] | (r[2] << 8) | (r[3] << 16) | ((uint32_t)r[4] << 24); }
static int slot_st(const uint8_t *r, int s) { return (status_bits(r) >> (2 * s)) & 3; }

static void cmd(Portal *p, const char *bytes, int len, uint8_t *rep, int *has)
{
    *has = portal_command(p, (const uint8_t *)bytes, len, rep);
}

int main(int argc, char **argv)
{
    static Portal p;
    static uint8_t fig[SKY_SIZE], fig2[SKY_SIZE];
    uint8_t rep[32];
    int has, i;
    const char *path = argc > 1 ? argv[1] : getenv("FIG");

    /* a figure: from a real dump if given, else a made-up one */
    if (path) {
        FILE *f = fopen(path, "rb");
        if (!f || fread(fig, 1, SKY_SIZE, f) != SKY_SIZE) { printf("cannot read %s\n", path); return 2; }
        fclose(f);
        printf("using %s: figure id %u, variant %04x\n", path, sky_figure_id(fig), sky_variant(fig));
    } else {
        for (i = 0; i < SKY_SIZE; i++) fig[i] = (uint8_t)(i * 7);
    }
    memcpy(fig2, fig, SKY_SIZE);
    fig2[0] ^= 0x55; /* different UID */

    portal_init(&p);

    /* game start-up: reset, activate, colour */
    cmd(&p, "R", 2, rep, &has);
    CHECK(has && rep[0] == 'R' && rep[1] == 0x02 && rep[2] == 0x18, "R reply");
    cmd(&p, "A\x01", 2, rep, &has);
    CHECK(has && rep[0] == 'A' && rep[1] == 1 && rep[2] == 0xFF && rep[3] == 0x77, "A reply");
    cmd(&p, "C\x10\x20\x30", 4, rep, &has);
    CHECK(!has && p.r == 0x10 && p.g == 0x20 && p.b == 0x30, "C sets LED");
    cmd(&p, "M\x01", 2, rep, &has);
    CHECK(has && rep[0] == 'M' && rep[3] == 0x19, "M reply");

    portal_status(&p, rep);
    CHECK(rep[0] == 'S' && status_bits(rep) == 0 && rep[6] == 1, "empty status, active");

    /* place a figure: one 'added' report, then 'ready' */
    CHECK(portal_place(&p, -1, fig) == 0, "placed in slot 0");
    portal_status(&p, rep);
    CHECK(slot_st(rep, 0) == ST_ADDED, "status shows ADDED");
    portal_status(&p, rep);
    CHECK(slot_st(rep, 0) == ST_READY, "then READY");
    portal_status(&p, rep);
    CHECK(slot_st(rep, 0) == ST_READY, "stays READY");

    /* read all 64 blocks */
    for (i = 0; i < 64; i++) {
        char q[3] = {'Q', 0x10, (char)i};
        cmd(&p, q, 3, rep, &has);
        CHECK(has && rep[0] == 'Q' && rep[1] == 0x10 && rep[2] == i && memcmp(rep + 3, fig + 16 * i, 16) == 0, "Q block data");
    }
    /* query an empty slot -> error */
    {
        char q[3] = {'Q', 0x13, 0};
        cmd(&p, q, 3, rep, &has);
        CHECK(has && rep[1] == 0x01, "Q empty slot errors");
    }

    /* write block 8 and read it back; slot marked dirty for saving */
    {
        char w[19] = {'W', 0x10, 8};
        for (i = 0; i < 16; i++) w[3 + i] = (char)(0xA0 + i);
        cmd(&p, w, 19, rep, &has);
        CHECK(has && rep[0] == 'W' && rep[1] == 0x10 && rep[2] == 8, "W reply");
        CHECK(p.slot[0].dirty == 1, "write marks dirty");
        CHECK(memcmp(p.slot[0].data + 128, w + 3, 16) == 0, "W stored");
        cmd(&p, "Q\x10\x08", 3, rep, &has);
        CHECK(memcmp(rep + 3, w + 3, 16) == 0, "read back written block");
    }

    /* second figure goes to slot 1; status shows both */
    CHECK(portal_place(&p, -1, fig2) == 1, "second figure slot 1");
    portal_status(&p, rep);
    CHECK(slot_st(rep, 0) == ST_READY && slot_st(rep, 1) == ST_ADDED, "both reported");
    portal_status(&p, rep);

    /* remove slot 0: REMOVING then NONE */
    CHECK(portal_remove(&p, 0) == 1, "remove ok");
    CHECK(portal_remove(&p, 0) == 0, "remove twice is a no-op");
    portal_status(&p, rep);
    CHECK(slot_st(rep, 0) == ST_REMOVING, "REMOVING reported");
    portal_status(&p, rep);
    CHECK(slot_st(rep, 0) == ST_NONE && slot_st(rep, 1) == ST_READY, "then gone, slot 1 still there");

    /* placing the same figure again returns to its old spot */
    CHECK(portal_place(&p, -1, fig) == 0, "same figure back to slot 0");
    CHECK(portal_place(&p, 1, fig) == -1, "cannot place on an occupied slot");

    /* fill the portal */
    for (i = 2; i < SKY_SLOTS; i++) {
        fig2[1] = (uint8_t)i;
        CHECK(portal_place(&p, -1, fig2) == i, "fill slot");
    }
    CHECK(portal_place(&p, -1, fig2) == -1, "portal full");

    /* reset + activate again re-announces figures (what a game does when it restarts the portal) */
    cmd(&p, "R", 2, rep, &has);
    portal_status(&p, rep);
    CHECK(rep[6] == 0, "inactive after R");
    cmd(&p, "A\x01", 2, rep, &has);
    portal_status(&p, rep);
    CHECK(slot_st(rep, 0) == ST_ADDED && slot_st(rep, 7) == ST_ADDED, "re-announced as ADDED");
    portal_status(&p, rep);
    CHECK(slot_st(rep, 0) == ST_READY, "then READY");

    /* status counter increments */
    {
        uint8_t a[32], b[32];
        portal_status(&p, a);
        portal_status(&p, b);
        CHECK((uint8_t)(a[5] + 1) == b[5], "counter increments");
    }

    /* short / junk commands do not crash */
    cmd(&p, "Q", 1, rep, &has);
    CHECK(!has, "short Q ignored");
    cmd(&p, "W\x10", 2, rep, &has);
    CHECK(!has, "short W ignored");
    cmd(&p, "Z", 1, rep, &has);
    CHECK(!has, "unknown ignored");
    {
        char q[3] = {'Q', 0x10, (char)200};
        cmd(&p, q, 3, rep, &has);
        CHECK(has && rep[1] == 0x01, "bad block number errors");
    }

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
