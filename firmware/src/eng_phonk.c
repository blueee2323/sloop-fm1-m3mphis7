/* SPDX-License-Identifier: GPL-3.0-only
 * M3mphis7 fork addition, on SLOOP / Felucca by Leo Kuroshita (Hügelton Instruments) and isod89.
 * PHONK: the Memphis phonk voice. One oscillator through a resonant low-pass, then grit after the filter so the dirt
 * stays in, then a crusher. The waves:
 *   BELL   the TR-808 cowbell played as a melody: two square waves 1 : 1.48 apart (the 808's 540 / 800 Hz);
 *          TONE fades them into two saws
 *   GLASS  a glassy FM chime: a sine carrier, a modulator at RTIO x the note, its brightness falling with the envelope,
 *          a second carrier ~7 cents up for the detune; TONE is how metallic
 *   808    a sine bass; the pitch drop and slides come from the voice (ENV > PIT, GLIDE); SUB is a clean sine layer
 *          at the same pitch under the distorted one
 * DRV drives the oscillator before the filter and clips again after it, a little lopsided like an overdriven tape.
 * CRSH holds each value for up to 10 samples and drops up to 7 bits (an old sampler). SUB: a clean sine an octave
 * down for BELL, at the note for 808 (GLASS uses that oscillator as its modulator).
 * Voice state: ph[0] the oscillator, ph[1] the second partner, ph[2] the sub or the GLASS modulator; s[0..1] the
 * low-pass, s[2] the crusher's held value, s[3] its countdown. */
enum { PW_BELL, PW_GLASS, PW_808 };
static const char *const N_PH_WAVE[] = {"BELL", "GLASS", "808"};
/* GLASS's modulator ratio: 2 and 3 sound like a tuned chime, 3.5 / 2.76 / 5.4 like struck metal */
static const char *const N_PH_GLASS_RT[] = {"2", "2.5", "3", "3.5", "4", "2.76", "5.4", "7"};
static const uint16_t PH_GLASS_RT_Q8[8] = {512, 640, 768, 896, 1024, 707, 1382, 1792};
static const char *const N_PH_NONE[] = {"-", "-", "-", "-", "-", "-", "-", "-"};
static const param_desc_t PH_RTIO_NONE = {"-", F_ENUM, 0, 7, 3, N_PH_NONE, 0};    /* (RTIO's range: BELL / 808 ignore it) */
static const param_desc_t PH_SAW = {"SAW", F_PCT, 0, 127, 0, 0, 0};
static const param_desc_t PH_METL = {"METL", F_PCT, 0, 127, 0, 0, 0};
static const param_desc_t PH_TONE_NONE = {"-", F_PCT, 0, 127, 0, 0, 0};
static const param_desc_t *phonk_desc(const track_t *t, uint32_t k)
{
    int32_t w = t->p[P_E0];
    if (k == 1u && w != PW_GLASS)
        return &PH_RTIO_NONE;
    if (k == 5u)
        return w == PW_BELL ? &PH_SAW : w == PW_GLASS ? &PH_METL : &PH_TONE_NONE;
    return 0;
}

static void phonk_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env && !v->env_out) {                     /* a fresh note (not a legato glide): from the start */
        v->ph[0] = 0;
        v->ph[2] = 0;
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;
    }
    v->ph[1] = v->ph[0] + 0x40000000u;
}

static void phonk_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0] % 3u, j;
    int32_t crush = p[P_E2], res = p[P_E4], tone = p[P_E5] * 258, drvp = p[P_E6], sub = p[P_E7] * 160;
    int32_t hold = 1 + crush / 14;                                /* 1 .. 10 samples: 44 kHz down to ~4.4 kHz */
    int32_t qmask = ~((1 << (crush / 16 + 5)) - 1);               /* 15 bits down to ~8 */
    int32_t drive = 32768 + drvp * 1024;                          /* 1x .. 5x into the soft clip */
    int32_t dw = drvp * 258;
    uint32_t inc = m->inc, inc2 = inc, minc = 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t a1 = v->s[0], a2 = v->s[1], held = v->s[2], hcnt = v->s[3];
    tsvf_t lp;
    if (wave == PW_BELL)
        inc2 = inc + (inc >> 1) - ((inc >> 8) * 5u);              /* the 808's 540 : 800 Hz, x 1.48 */
    if (wave == PW_GLASS) {
        inc2 = inc + (inc >> 8);                                  /* the shimmer partner, ~+7 cents */
        minc = (inc >> 8) * PH_GLASS_RT_Q8[(uint32_t)p[P_E1] & 7u];
        sub = 0;                                                  /* ph[2] is the modulator */
    }
    tsvf_coef(&lp, (p[P_E3] << 8) + m->cutoff, res);
    for (j = 0; j < n; j++) {
        int32_t s, y;
        switch (wave) {
        case PW_BELL: {     /* the two 808 square waves; SAW (TONE) fades them into two saws */
            int32_t sq = (osc_pulse(ph0, inc, 0x80000000u) + osc_pulse(ph1, inc2, 0x80000000u)) >> 1;
            s = tone ? sq + mulq15(((osc_saw(ph0, inc) + osc_saw(ph1, inc2)) >> 1) - sq, tone) : sq;
            break;
        }
        case PW_GLASS: {    /* the metal: index = METL (TONE) x the envelope, up to ~1 cycle of phase */
            int32_t idx = mulq15(m->envq15, tone), mod = mulq15(sine_i(ph2), idx);
            s = (sine_i(ph0 + ((uint32_t)mod << 17)) + sine_i(ph1 + ((uint32_t)mod << 17))) >> 1;
            ph2 += minc;
            break;
        }
        default:            /* 808: a plain sine; the grit comes after the filter */
            s = sine_i(ph0);
            break;
        }
        ph0 += inc;
        ph1 += inc2;
        if (dw)
            s += mulq15((softclip(((s >> 2) * (drive >> 2)) >> 11) - s) >> 1, dw) << 1;   /* halved: no overflow */
        y = soft_knee(tsvf_lp(&lp, s >> 1, &a1, &a2) << 1, 22000);
        if (dw) {           /* the grit after the filter, so it stays: up to ~30x into the clip, a little lopsided
                             * (even harmonics too, like an overdriven tape) */
            int32_t bias = drvp * 60;
            int32_t z = softclip(y + y * drvp / 4 + bias) - softclip(bias);
            y += mulq15((z - y) >> 1, dw) << 1;
        }
        if (crush) {        /* CRSH: hold each value, fewer bits: an old sampler */
            if (--hcnt <= 0) {
                held = y & qmask;
                hcnt = hold;
            }
            y = held;
        }
        if (sub) {
            y += mulq15(sine_i(ph2), sub);
            ph2 += wave == PW_808 ? inc : inc >> 1;   /* 808: the clean layer at the same pitch; BELL: an octave down */
        }
        out[j] += mulq15(mulq15(soft_knee(y, 26000), amp_at(m, j)), VOICE_FS) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = a1;
    v->s[1] = a2;
    v->s[2] = held;
    v->s[3] = hcnt;
}

static const preset_t PHONK_PRESETS[] = {
    /* {WAVE, RTIO, CRSH, CUT, RES, TONE, DRV, SUB}, {A D S R}, fenv, mono, FX(dist, chorus, delay, reverb), extras */
    /* the cowbell, the signature lead: TONE is SAW for the cowbell. Overdriven and crushed; from saws;
     * crushed hard; sliding (mono, legato glide); an octave down on the keys; clean; driven with a ring; tape-wobbled */
    {"MEMPH COW", {0, 0, 30, 104, 40, 0, 112, 0}, {0, 52, 0, 30}, 0, 0, FX(60, 0, 14, 22), XP(P_LD_PIT + 1, 1, P_LRATE + 1, 24)},
    {"SAW COW", {0, 0, 24, 100, 36, 127, 110, 0}, {0, 52, 0, 30}, 0, 0, FX(60, 0, 14, 22), XP(P_LD_PIT + 1, 1, P_LRATE + 1, 24)},
    {"CRUSH COW", {0, 0, 96, 108, 30, 40, 100, 0}, {0, 50, 0, 28}, 0, 0, FX(50, 0, 12, 20)},
    {"SLIDE COW", {0, 0, 30, 104, 40, 20, 112, 0}, {0, 60, 40, 30}, 0, 1, FX(60, 0, 14, 22), XP(P_GLIDE + 1, 34, P_GLMODE + 1, 1)},
    {"LOW COW", {0, 0, 40, 96, 44, 30, 118, 0}, {0, 58, 0, 34}, 0, 0, FX(70, 0, 12, 20), XP(P_TRANS + 1, -12)},
    {"PHONK BELL", {0, 0, 0, 80, 46, 0, 30, 0}, {0, 52, 0, 34}, 0, 0, FX(10, 0, 18, 24)},
    {"DRIFT BELL", {0, 0, 0, 86, 60, 0, 80, 0}, {0, 64, 0, 44}, 0, 0, FX(30, 0, 24, 30)},
    {"TAPE COW", {0, 0, 20, 84, 30, 0, 96, 0}, {0, 58, 0, 34}, 0, 0, FX(40, 20, 20, 30), XP(P_LD_PIT + 1, 2, P_LRATE + 1, 18)},
    /* the chimes: a hard-driven one matched to a reference clip; glassy; a haunted music box; a harsh arcade one */
    {"MEMPH DING", {1, 0, 0, 110, 10, 64, 127, 0}, {0, 66, 0, 34}, 0, 0, FX(70, 34, 14, 26), XP(P_LD_PIT + 1, 1, P_LRATE + 1, 24)},
    {"GLASS DING", {1, 3, 0, 110, 10, 72, 20, 0}, {0, 50, 0, 36}, 0, 0, FX(8, 14, 20, 30)},
    {"HAUNT BOX", {1, 3, 0, 100, 10, 44, 10, 0}, {0, 58, 0, 44}, 0, 0, FX(0, 24, 26, 46), XP(P_LD_PIT + 1, 1, P_LRATE + 1, 22)},
    {"ARCADE BEL", {1, 3, 0, 116, 20, 104, 50, 0}, {0, 44, 0, 30}, 0, 0, FX(20, 0, 24, 24)},
    /* the 808s: a sine with a pitch drop, sliding where notes overlap, dirtied after the filter, with SUB as the clean
     * layer under it (the usual phonk trick: a clean sub for the low end, the distorted copy for the mids) */
    {"MEMPH 808", {2, 0, 0, 96, 10, 0, 90, 70}, {0, 110, 0, 50}, 0, 1, FX(40, 0, 0, 0), XP(P_GLIDE + 1, 56, P_GLMODE + 1, 1, P_ED_PIT + 1, 20, P_TRANS + 1, -24)},
    {"BLOWN 808", {2, 0, 0, 100, 10, 0, 127, 80}, {0, 108, 0, 46}, 0, 1, FX(96, 0, 0, 0), XP(P_GLIDE + 1, 56, P_GLMODE + 1, 1, P_ED_PIT + 1, 24, P_TRANS + 1, -24)},
    {"TAPE 808", {2, 0, 0, 70, 20, 0, 100, 60}, {0, 112, 0, 52}, 0, 1, FX(50, 0, 0, 6), XP(P_GLIDE + 1, 60, P_GLMODE + 1, 1, P_ED_PIT + 1, 16, P_TRANS + 1, -24)},
    {"SLIDE 808", {2, 0, 0, 96, 10, 0, 80, 70}, {0, 118, 0, 60}, 0, 1, FX(36, 0, 0, 0), XP(P_GLIDE + 1, 84, P_GLMODE + 1, 1, P_ED_PIT + 1, 12, P_TRANS + 1, -24)},
};

static const engine_t ENG_PHONK = {
    "PHONK", {"OSC", "TONE"},
    {
        {"WAVE", F_ENUM, 0, 2, 0, N_PH_WAVE, 0},
        {"RTIO", F_ENUM, 0, 7, 3, N_PH_GLASS_RT, 0},
        {"CRSH", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 104, 0, 0},
        {"RES", F_PCT, 0, 127, 40, 0, 0},
        {"TONE", F_PCT, 0, 127, 0, 0, 0},
        {"DRV", F_PCT, 0, 127, 100, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
    },
    PHONK_PRESETS, sizeof(PHONK_PRESETS) / sizeof(PHONK_PRESETS[0]), 1, phonk_note_on, phonk_render,
    0x881F, {P_E3, P_E6, P_E2, P_E5}, 0, 0, phonk_desc,   /* the knobs: CUT, DRV, CRSH, TONE (SAW / METL) */
};
