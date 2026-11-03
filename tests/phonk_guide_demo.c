/* SPDX-License-Identifier: GPL-3.0-only */
/* M3mphis7 demos written to a phonk production guide (melodics.com, "producer's guide to phonk music"), through the
 * firmware's own sequencer, voices, drum kit and master effects:
 *   drift  150 BPM, E Phrygian. The cowbell leads on the off-beats and mirrors the hats; four-on-the-floor kick with a
 *          push, snare on 2 and 4 with ghost notes, 16th hats with 32nd and 64th rolls, light swing; an 808 that follows
 *          the kick and slides; in the drop a lower cowbell layer in triplets for tension.
 *   og     135 BPM, G minor, laid back. A dusty horror-piano loop, the cowbell sparse on the off-beats, a tape 808,
 *          a syncopated kick, more swing, more vinyl.
 * Both: intro, main, breakdown, drop, outro (the guide's sections, shortened), DUST (old sampler), a texture bed, the
 * DJ filter for the intro / breakdown. No vocals: that is the user-sample slot's job.
 *   tests/phonk_guide_demo.c -> build/host/phonk_guide_demo drift|og OUT.wav */
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
/* "n" a note (the next of notes[]), "a" an accented one, "s" one that slides in, "-" a tie, "." a rest */
static void line(track_t *t, const char *pat, const uint8_t *notes)
{
    uint32_t i, ni = 0;
    for (i = 0; i < NSTEP && pat[i]; i++) {
        if (pat[i] == 'n' || pat[i] == 's' || pat[i] == 'a') {
            put_step(t, i, 1, &notes[ni++], ST_NOTE, pat[i] == 's' ? SF_SLIDE : 0);
            t->step[i].vel = pat[i] == 'a' ? 127 : 92 + (i * 7u) % 17u;   /* a little uneven, as played */
        } else if (pat[i] == '-')
            put_step(t, i, 0, 0, ST_TIE, 0);
    }
}
/* one drum lane over 16ths: "x" normal, "X" hard, "g" ghost, "s" soft, "2" a 32nd roll, "4" a 64th roll */
static void lane(uint32_t note, const char *pat)
{
    uint32_t i, l = lane_of_note(note);
    for (i = 0; i < NSTEP && pat[i]; i++) {
        char c = pat[i];
        if (c == '.') continue;
        dstep_set(&TDRUM->dstep[i], l, c == 'X' ? LV_HARD : c == 'g' ? LV_GHOST : c == 's' ? LV_SOFT : LV_NORM,
                  c == '2' ? 1 : c == '4' ? 3 : 0);
    }
}

typedef struct { uint32_t intro, main, brk, drop, outro; } arr_t;

int main(int argc, char **argv)
{
    int drift = argc > 1 && !strcmp(argv[1], "drift");
    FILE *f = fopen(argc > 2 ? argv[2] : "build/host/phonk-guide.wav", "wb");
    int32_t out[CTL * 2], peak = 0;
    uint32_t i, k, total, clips = 0;
    track_t *lead = &trk[0], *b808 = &trk[1], *third = &trk[2];
    arr_t a = drift ? (arr_t){4, 8, 4, 8, 2} : (arr_t){4, 8, 4, 8, 2};
    uint32_t bars = a.intro + a.main + a.brk + a.drop + a.outro, bpm = drift ? 150 : 135;
    host_tracks_init();
    song.g[G_BPM] = (int16_t)bpm;
    song.g[G_SWING] = drift ? 24 : 40;           /* ~56 % and ~60 %: the guide's 10-20 % off the grid */
    for (i = 0; i < NTRK; i++) {
        steps_clear(&trk[i]);
        trk[i].p[P_SLEN] = 64;                   /* four bars of 1/16 */
        trk[i].p[P_SDIV] = 2;
        trk[i].p[P_AMODE] = 0;
    }
    TDRUM->p[P_E0] = KIT_PHONK;
    TDRUM->p[P_SLEN] = 32;                       /* two bars */
    if (drift) {
        host_preset(lead, ENGI_PHONK, find_preset(ENGI_PHONK, "MEMPH COW"));
        host_preset(b808, ENGI_PHONK, find_preset(ENGI_PHONK, "MEMPH 808"));
        host_preset(third, ENGI_PHONK, find_preset(ENGI_PHONK, "LOW COW"));
        third->p[P_SDIV] = 5;                    /* 1/16 triplets */
        third->p[P_SLEN] = 48;                   /* two bars of them */
        lane(36, "X...x...x...x...X...x...x...x.x.");   /* four on the floor, a push at the end */
        lane(38, "....X..g.g..X..g....X..g..g.X...");   /* 2 and 4, ghosts between */
        lane(39, "............x...............x...");   /* a clap on 4 */
        lane(42, "xsxsxsxsxsxsxs2sxsxsxsxsxsxsxs44");   /* 16ths, uneven, 32nd and 64th rolls */
        {   /* E Phrygian (E F G A B C D), a four-bar loop on the off-beats, mirroring the hats */
            static const uint8_t m[] = {76, 76, 79, 76, 77, 76, 74, 72,   76, 76, 79, 81, 79, 77, 76, 77,
                                        76, 76, 79, 76, 77, 76, 74, 72,   84, 83, 81, 79, 77, 79, 77, 76};
            line(lead, ".a.nn.n..a.nn.n..a.nn.n..a.nn.n..a.nn.n..a.nn.n..a.nn.n..a.nn.n.", m);
            static const uint8_t bn[] = {28, 28, 31, 28, 28, 29, 28, 28, 31, 28, 26, 28};
            line(b808, "n-------n-----s-n-------n---s---n-------n-----s-n-------n---s---", bn);
            static const uint8_t tn[] = {64, 67, 65, 64, 67, 69, 67, 65, 64, 67, 65, 64, 64, 67, 65, 64, 67, 69, 71, 69, 67, 65, 64, 62};
            line(third, "n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.", tn);   /* triplets, every other */
        }
        song.g[G_DUST] = 56;
        song.g[G_TXSRC] = 2;                     /* TAPE */
        song.g[G_TXLVL] = 34;
    } else {
        host_preset(lead, 4, find_preset(4, "DUSTY PNO"));
        host_preset(b808, ENGI_PHONK, find_preset(ENGI_PHONK, "TAPE 808"));
        host_preset(third, ENGI_PHONK, find_preset(ENGI_PHONK, "TAPE COW"));
        lead->p[P_LD_PIT] = 1; lead->p[P_LRATE] = 16; lead->p[P_REV] = 46;   /* a warped tape */
        lane(36, "x......x..x.....x......x..x..x..");   /* laid back, syncopated */
        lane(38, "....x..g....x......g..g.x.....g.");
        lane(42, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.2.x.");   /* 8ths, one roll */
        lane(46, "..............x...............x.");
        {   /* G minor, a dusty horror loop (with the tritone, C#) */
            static const uint8_t pn[] = {67, 70, 74, 70, 73, 70, 67, 66,   67, 70, 74, 75, 74, 70, 69, 66,
                                         67, 70, 74, 70, 73, 70, 67, 66,   63, 67, 70, 67, 66, 62, 63, 62};
            line(lead, "n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.n.", pn);
            static const uint8_t bn[] = {31, 31, 34, 31, 30, 31, 31, 27, 26, 27};
            line(b808, "n---------n-----n-----s---n-----n---------n-----n-----s---n-----", bn);
            static const uint8_t cn[] = {79, 82, 79, 81, 79, 82, 84, 82};   /* sparse, on the off-beats */
            line(third, "......a.......n.......a.......n.......a.......n.......a.......n.", cn);
        }
        song.g[G_DUST] = 84;
        song.g[G_TXSRC] = 1;                     /* VINYL */
        song.g[G_TXLVL] = 52;
    }
    song.g[G_TXWHEN] = 1;
    song.g[G_DUCK] = 20;
    {
        uint32_t barlen = 4u * 60u * FS / bpm, frames = bars * barlen + FS;
        uint32_t b1 = a.intro, b2 = b1 + a.main, b3 = b2 + a.brk, b4 = b3 + a.drop, b5 = b4 + a.outro;
        wav_hdr(f, frames);
        transport_req = 1;
        for (total = 0; total < frames; total += CTL) {
            uint32_t b = total / barlen;
            int32_t into = (int32_t)((total % barlen) * 64u / barlen);      /* 0..63 through the bar */
            int drums = (b >= b1 && b < b2) || (b >= b3 && b < b4);
            /* the DJ filter: the intro thin (high-pass) opening up; the breakdown dark (low-pass); the outro closing */
            int32_t filt = 0;
            if (b < b1) filt = 34 - (int32_t)((b * 64 + into) * 34 / (b1 * 64));
            else if (b >= b2 && b < b3) filt = -34;
            else if (b >= b4) filt = -10 - (int32_t)(((b - b4) * 64 + into) * 40 / (a.outro * 64));
            song.g[G_FILT] = (int16_t)filt;
            song.g[G_DRLVL] = drums ? 100 : 0;
            b808->p[P_LEVEL] = drums ? 92 : 0;
            if (drift) {
                lead->p[P_LEVEL] = b < b5 ? 120 : 0;
                third->p[P_LEVEL] = b >= b3 && b < b4 ? 70 : 0;                 /* the triplet layer: the drop */
            } else {
                lead->p[P_LEVEL] = b < b5 ? 96 : 0;
                third->p[P_LEVEL] = b >= b1 && b < b4 ? 100 : 0;
            }
            if (b >= b5 && song.playing) transport_req = 2;
            mix_block(out, CTL);
            for (k = 0; k < CTL * 2; k++) {
                int32_t v = out[k] < 0 ? -out[k] : out[k];
                if (v > peak) peak = v;
                if (v >= 32767) clips++;
            }
            for (k = 0; k < CTL; k++) wav_put(f, out[k * 2], out[k * 2 + 1]);
        }
        fclose(f);
        printf("phonk guide demo (%s): %u bars at %u BPM, %.1f s, peak %.1f dBFS, clipped %u %s\n", drift ? "drift" : "og",
               bars, bpm, frames / (double)FS, 20.0 * log10((peak + 1) / 32768.0), clips, clips ? "FAIL" : "PASS");
    }
    return clips != 0;
}
