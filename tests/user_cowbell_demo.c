/* SPDX-License-Identifier: GPL-3.0-only */
/* M3mphis7: the user's own cowbell samples in a user sample slot (USR1), played from the keys by the SAMPLE engine.
 * The slot is built by tools/fm1_sample_upload.py build (a header + IMA ADPCM data, as the web editor writes it) and
 * placed in a RAM image of the flash, read by smp_user_scan as at boot. The samples themselves are not part of this
 * repository: pass the .hdr / .bin.
 *   build/host/user_cowbell_demo SLOT.hdr SLOT.bin zones|groove OUT.wav
 * zones: each key range's cowbell playing a short phonk line in its range, one after the other (150 BPM)
 * groove: a drift-phonk groove with the sampled cowbell as the lead (150 BPM, E Phrygian), MEMPH 808, the phonk kit */
#include <stdint.h>
static uint32_t host_slots[4u * 0x14000u / 4u];
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

#define KIT_PHONK 14u

static uint32_t find_preset(uint32_t e, const char *name)
{
    uint32_t i;
    for (i = 0; i < ENGINES[e]->npresets; i++)
        if (!strcmp(ENGINES[e]->presets[i].name, name)) return i;
    fprintf(stderr, "no preset %s\n", name);
    exit(2);
}
static void line(track_t *t, const char *pat, const uint8_t *notes)
{
    uint32_t i, ni = 0;
    for (i = 0; i < NSTEP && pat[i]; i++) {
        if (pat[i] == 'n' || pat[i] == 'a' || pat[i] == 's') {
            put_step(t, i, 1, &notes[ni++], ST_NOTE, pat[i] == 's' ? SF_SLIDE : 0);
            t->step[i].vel = pat[i] == 'a' ? 127 : 96 + (i * 7u) % 17u;
        } else if (pat[i] == '-')
            put_step(t, i, 0, 0, ST_TIE, 0);
    }
}
static void lane(uint32_t note, const char *pat)
{
    uint32_t i, l = lane_of_note(note);
    for (i = 0; i < NSTEP && pat[i]; i++)
        if (pat[i] != '.')
            dstep_set(&TDRUM->dstep[i], l, pat[i] == 'X' ? LV_HARD : pat[i] == 'g' ? LV_GHOST : pat[i] == 's' ? LV_SOFT : LV_NORM,
                      pat[i] == '2' ? 1 : pat[i] == '4' ? 3 : 0);
}
static size_t slurp(const char *path, uint8_t *dst, size_t max)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    n = fread(dst, 1, max, f);
    fclose(f);
    return n;
}

static FILE *wf;
static int32_t peak;
static uint32_t clips, frames_out;
static void render(uint32_t frames)
{
    int32_t out[CTL * 2];
    uint32_t t, k;
    for (t = 0; t < frames; t += CTL) {
        mix_block(out, CTL);
        for (k = 0; k < CTL * 2; k++) {
            int32_t v = out[k] < 0 ? -out[k] : out[k];
            if (v > peak) peak = v;
            if (v >= 32767) clips++;
        }
        for (k = 0; k < CTL; k++) wav_put(wf, out[k * 2], out[k * 2 + 1]);
        frames_out += CTL;
    }
}

int main(int argc, char **argv)
{
    uint8_t *img = (uint8_t *)host_slots;
    uint32_t i, bar;
    int groove;
    if (argc < 5) { fprintf(stderr, "usage: %s SLOT.hdr SLOT.bin zones|groove OUT.wav\n", argv[0]); return 2; }
    groove = !strcmp(argv[3], "groove");
    memset(host_slots, 0xFF, sizeof host_slots);
    slurp(argv[1], img, SMP_USER_DATA);
    slurp(argv[2], img + SMP_USER_DATA, SMP_USER_SIZE - SMP_USER_DATA);
    host_tracks_init();
    smp_user_scan(0);
    song.g[G_BPM] = 150;
    for (i = 0; i < NTRK; i++) {
        steps_clear(&trk[i]);
        trk[i].p[P_SLEN] = 64;
        trk[i].p[P_SDIV] = 2;
        trk[i].p[P_AMODE] = 0;
    }
    host_preset(&trk[0], 4, 0);
    trk[0].p[P_E0] = (int16_t)SMP_NSETS;               /* SET: USR1 */
    trk[0].p[P_E3] = 0;                                /* LOOP off: one-shots */
    trk[0].p[P_ATK] = 0; trk[0].p[P_DEC] = 127; trk[0].p[P_SUS] = 127; trk[0].p[P_REL] = 30;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_LEVEL] = 112;
    wf = fopen(argv[4], "wb");
    wav_hdr(wf, 0);
    if (!groove) {
        /* each key range once: reverb (C3..G#3), 808 original (A3..F4), laser (F#4..D5), thin (D#5..C6) */
        static const uint8_t lo[4] = {48, 57, 66, 75};
        uint32_t z, barlen = 4u * 60u * FS / 150u;
        song.g[G_DRLVL] = 0;
        for (z = 0; z < 4; z++) {
            uint8_t b = lo[z], m[8] = {b + 4, b + 4, b + 7, b + 4, b + 5, b + 4, b + 2, b};   /* E-F-G..: Phrygian-ish */
            steps_clear(&trk[0]);
            trk[0].p[P_SLEN] = 16;
            line(&trk[0], ".a.nn.n..a.nn.n.", m);
            transport_req = 1;
            render(2u * barlen);
            transport_req = 2;
            render(FS / 2u);
            printf("zone %u: keys %u..%u\n", z + 1, b, b + 8u);
        }
    } else {
        static const uint8_t m[] = {64, 64, 62, 64, 65, 64, 60, 62,   64, 64, 62, 64, 65, 64, 60, 59,
                                    64, 64, 62, 64, 65, 64, 60, 62,   65, 64, 62, 60, 62, 64, 62, 64};
        static const uint8_t bn[] = {28, 28, 31, 28, 28, 29, 28, 28, 31, 28, 26, 28};
        uint32_t barlen = 4u * 60u * FS / 150u;
        host_preset(&trk[1], ENGI_PHONK, find_preset(ENGI_PHONK, "MEMPH 808"));
        TDRUM->p[P_E0] = KIT_PHONK;
        TDRUM->p[P_SLEN] = 32;
        song.g[G_SWING] = 24;
        lane(36, "X...x...x...x...X...x...x...x.x.");
        lane(38, "....X..g.g..X..g....X..g..g.X...");
        lane(42, "xsxsxsxsxsxsxs2sxsxsxsxsxsxsxs44");
        /* the sampled cowbell: the 808 original's range (A3..F4) plays it ~an octave below its own pitch */
        line(&trk[0], ".a.nn.n..a.nn.n..a.nn.n..a.nn.n..a.nn.n..a.nn.n..a.nn.n..a.nn.n.", m);
        line(&trk[1], "n-------n-----s-n-------n---s---n-------n-----s-n-------n---s---", bn);
        trk[1].p[P_LEVEL] = 92;
        song.g[G_DUST] = 56;
        song.g[G_TXSRC] = 2;
        song.g[G_TXLVL] = 30;
        song.g[G_TXWHEN] = 1;
        song.g[G_DUCK] = 20;
        transport_req = 1;
        for (bar = 0; bar < 12; bar++) {
            song.g[G_DRLVL] = bar >= 2 ? 100 : 0;          /* two bars of cowbell, then the beat */
            trk[1].p[P_LEVEL] = bar >= 2 ? 92 : 0;
            render(barlen);
        }
        transport_req = 2;
        render(FS);
    }
    fseek(wf, 0, SEEK_SET);
    wav_hdr(wf, frames_out);
    fclose(wf);
    printf("user cowbells (%s): %.1f s, peak %.1f dBFS, clipped %u %s\n", argv[3], frames_out / (double)FS,
           20.0 * log10((peak + 1) / 32768.0), clips, clips ? "FAIL" : "PASS");
    return clips != 0;
}
