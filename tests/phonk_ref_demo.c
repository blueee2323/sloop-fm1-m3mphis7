/* SPDX-License-Identifier: GPL-3.0-only */
/* PHONK37, matched to the user's reference clip: ~90 BPM, the overdriven 808 cowbell (PHONK BELL wave, MEMPH COW) in 16ths
 * with 32nd flurries around D (C#, Eb, Bb: D minor, dark neighbours), the octave pops up to D7, a distorted 808 on D,
 * the PHONK kit, the master's DUST and TAPE. tests/phonk_ref_demo.c -> build/host/phonk_ref_demo OUT.wav */
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
/* 64 steps of 1/16: "n" a note (the next of notes[], or a chord of 3 with chords), "s" a note that slides in,
 * "-" a tie, "." a rest */
static void line(track_t *t, const char *pat, const uint8_t *notes, uint32_t per)
{
    uint32_t i, ni = 0;
    for (i = 0; i < 64u && pat[i]; i++) {
        if (pat[i] == 'n' || pat[i] == 's') {
            put_step(t, i, per, &notes[ni], ST_NOTE, pat[i] == 's' ? SF_SLIDE : 0);
            ni += per;
        } else if (pat[i] == '-')
            put_step(t, i, 0, 0, ST_TIE, 0);
    }
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "build/host/phonk-ref-90.wav", "wb");
    int32_t out[CTL * 2], peak = 0;
    uint32_t i, k, total, clips = 0, bar;
    track_t *bell = &trk[0], *b808 = &trk[1], *piano = &trk[2];
    host_tracks_init();
    song.g[G_BPM] = 90;
    for (i = 0; i < NTRK; i++) {
        steps_clear(&trk[i]);
        trk[i].p[P_SLEN] = 64;
        trk[i].p[P_SDIV] = 2;                         /* 1/16: four bars */
        trk[i].p[P_AMODE] = 0;
    }
    bell->p[P_SDIV] = 3;                              /* the chime in 1/32: two bars, room for the flurries */
    host_preset(bell, ENGI_PHONK, find_preset(ENGI_PHONK, "MEMPH COW"));
    host_preset(b808, ENGI_PHONK, find_preset(ENGI_PHONK, "BLOWN 808"));
    host_preset(piano, 4, find_preset(4, "DUSTY PNO"));
    piano->p[P_LD_PIT] = 1;  piano->p[P_LRATE] = 18;  piano->p[P_REV] = 44;

    TDRUM->p[P_E0] = KIT_PHONK;
    for (bar = 0; bar < 4; bar++) {
        static const char *K = "x......x..x.....", *S = "....x.......x...";
        const char *H = bar & 1 ? "x.xxx.x.x.xxx.xx" : "x.x.x.x.x.x.x.x.";
        for (i = 0; i < 16; i++) {
            uint8_t n[3];
            uint32_t c = 0;
            if (K[i] == 'x') n[c++] = 36;
            if (S[i] == 'x') n[c++] = 39;
            if (H[i] == 'x') n[c++] = 42;
            if (c) put_step(TDRUM, bar * 16 + i, c, n, ST_NOTE, 0);
        }
    }
    {   /* the chime, two bars of 1/32: "n." a 16th, "nn" a 32nd flurry */
        static const uint8_t cb[] = {
            74, 74, 73, 74, 75, 74, 70, 74, 73, 86, 74, 73, 74, 69, 70, 73, 74, 74, 73,
            74, 74, 73, 74, 75, 77, 75, 74, 73, 86, 86, 74, 70, 69, 70, 73, 74, 74, 73};   /* D5, octave pops to D6 */
        line(bell, "n.n.n.n.n.n.n.nnn.n.n.n.n.n.n.nnn.n.n.n.n.n.n.nnn.n.n.n.n.n.n.nn", cb, 1);
        static const uint8_t bn[] = {38, 38, 34, 34, 38, 38, 33, 33};   /* D2, Bb1, A1 */
        line(b808, "n-------n--n----n-------n--s----n-------n--n----n-------n--s----", bn, 1);
        static const uint8_t pn[] = {62, 58, 62, 57};
        line(piano, "n---------------n---------------n---------------n---------------", pn, 1);
    }
    song.g[G_DUST] = 84;
    song.g[G_TXSRC] = 2;  song.g[G_TXLVL] = 40;  song.g[G_TXWHEN] = 1;
    song.g[G_DUCK] = 24;
    bell->p[P_LEVEL] = 127;
    {
        uint32_t barlen = 4u * 60u * FS / 90u, frames = 11u * barlen + FS;
        wav_hdr(f, frames);
        transport_req = 1;
        for (total = 0; total < frames; total += CTL) {
            uint32_t b = total / barlen;
            song.g[G_FILT] = (int16_t)(b < 1 ? -30 : -12);
            song.g[G_DRLVL] = b >= 1 && b < 11 ? 100 : 0;
            b808->p[P_LEVEL] = b >= 1 && b < 11 ? 80 : 0;
            piano->p[P_LEVEL] = b >= 3 && b < 11 ? 40 : 0;
            if (getenv("SOLO_BELL")) {             /* (analysis: the chime alone) */
                song.g[G_DRLVL] = 0; b808->p[P_LEVEL] = 0; piano->p[P_LEVEL] = 0;
                if (getenv("NOFX")) { bell->p[P_REV] = bell->p[P_DLY] = bell->p[P_CHOR] = 0; song.g[G_DUST] = 0;
                                      song.g[G_TXSRC] = 0; song.g[G_FILT] = 0; bell->p[P_LD_PIT] = 0; }
            }
            if (b >= 11 && song.playing) transport_req = 2;
            mix_block(out, CTL);
            for (k = 0; k < CTL * 2; k++) {
                int32_t a = out[k] < 0 ? -out[k] : out[k];
                if (a > peak) peak = a;
                if (a >= 32767) clips++;
            }
            for (k = 0; k < CTL; k++) wav_put(f, out[k * 2], out[k * 2 + 1]);
        }
        fclose(f);
        printf("phonk ref demo: %.1f s, peak %.1f dBFS, clipped %u %s\n", frames / (double)FS,
               20.0 * log10((peak + 1) / 32768.0), clips, clips ? "FAIL" : "PASS");
    }
    return clips != 0;
}
