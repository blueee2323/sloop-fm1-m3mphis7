/* SPDX-License-Identifier: GPL-3.0-only */
/* M3mphis7 sound pack: every PHONK preset on its own, through the firmware's sequencer, voices and FX, one WAV each.
 * Bells and chimes: the D minor phonk riff at 90 in 1/32. 808s: a sliding D minor line at 90.
 *   tests/pack_render.c -> build/host/pack_render OUTDIR   (OUTDIR/NN-NAME.wav, a list on stdout) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>

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
    const char *dir = argc > 1 ? argv[1] : "build/host/pack";
    const engine_t *e = ENGINES[ENGI_PHONK];
    uint32_t pi, k, bad = 0;
    for (pi = 0; pi < e->npresets; pi++) {
        const preset_t *pr = &e->presets[pi];
        int bell = pr->e[0] == PW_BELL || pr->e[0] == PW_GLASS, bass = pr->e[0] == PW_808, i;
        char path[512], name[32];
        int32_t out[CTL * 2], peak = 0;
        uint32_t frames, total, clips = 0;
        FILE *f;
        host_tracks_init();
        for (i = 0; i < (int)NTRK; i++) steps_clear(&trk[i]);
        trk[0].p[P_SLEN] = bell ? 64 : 32;   /* (808: 32 x 1/16, two bars) */
        trk[0].p[P_SDIV] = bell ? 3 : 2;
        trk[0].p[P_AMODE] = 0;
        trk[0].p[P_LEVEL] = 110;
        song.g[G_BPM] = bell || bass ? 90 : 140;
        song.g[G_DRLVL] = 0;
        host_preset(&trk[0], ENGI_PHONK, pi);
        if (!strcmp(pr->name, "SLIDE COW")) {          /* D Phrygian, sliding: D Eb F G A Bb C */
            static const uint8_t sc[] = {74, 75, 74, 72, 74, 75, 77, 75, 74, 70, 72, 74, 75, 74, 72, 70};
            line(&trk[0], "n-s-n-s-n---s---n-s-n-s-n---s---n-s-n-s-n---s---n-s-n-s-n---s---", sc, 1);
        } else if (bell) {
            static const uint8_t cb[] = {
                74, 74, 73, 74, 75, 74, 70, 74, 73, 86, 74, 73, 74, 69, 70, 73, 74, 74, 73,
                74, 74, 73, 74, 75, 77, 75, 74, 73, 86, 86, 74, 70, 69, 70, 73, 74, 74, 73};
            uint8_t lo[sizeof cb];
            for (k = 0; k < sizeof cb; k++)              /* LOW COW: TRN -12 moves the keys; the steps hold the note */
                lo[k] = (uint8_t)(cb[k] - (pr->x[0] == P_TRANS + 1 ? 12 : 0));
            line(&trk[0], "n.n.n.n.n.n.n.nnn.n.n.n.n.n.n.nnn.n.n.n.n.n.n.nnn.n.n.n.n.n.n.nn", lo, 1);
        } else if (bass) {
            static const uint8_t bn[] = {38, 38, 41, 34, 34, 33, 33};   /* D2 .. A1: steps hold the played note (TRN moves the keys) */
            line(&trk[0], "n-------n--n--s-n-------n-----s-", bn, 1);
        }
        for (i = 0, k = 0; pr->name[i] && k < sizeof name - 1; i++)
            name[k++] = pr->name[i] == ' ' ? '-' : pr->name[i];
        name[k] = 0;
        snprintf(path, sizeof path, "%s/%02u-%s.wav", dir, pi + 1, name);
        f = fopen(path, "wb");
        frames = (2u * 4u * 60u * FS / (uint32_t)song.g[G_BPM]) * 2u + FS;     /* the riff twice, then the tail */
        frames = (frames / CTL) * CTL;
        wav_hdr(f, frames);
        transport_req = 1;
        for (total = 0; total < frames; total += CTL) {
            if (total + FS >= frames && song.playing) transport_req = 2;
            mix_block(out, CTL);
            for (k = 0; k < CTL * 2; k++) {
                int32_t a = out[k] < 0 ? -out[k] : out[k];
                if (a > peak) peak = a;
                if (a >= 32767) clips++;
            }
            for (k = 0; k < CTL; k++) wav_put(f, out[k * 2], out[k * 2 + 1]);
        }
        fclose(f);
        printf("%02u\t%s\t%d\t%s\t%.1f\t%u\n", pi + 1, pr->name, song.g[G_BPM], N_PH_WAVE[pr->e[0] % 3],
               20.0 * log10((peak + 1) / 32768.0), clips);
        if (clips || peak < 2000) bad = 1;
    }
    return bad;
}
