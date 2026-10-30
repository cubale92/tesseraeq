/* Tessera EQ, MPC OS edition: 8-band parametric EQ with mid/side/left/right routing per band and dynamic EQ,
 * as an mpc_engine() effect for the mpc-vst-plugins wrapper (44.1 kHz, interleaved int16 stereo, 128-frame blocks).
 * Same filter maths as the desktop plugin (Source/FilterDesign.h): RBJ biquads, Butterworth cut filters up to
 * 48 dB/oct, minimum phase. The desktop version's linear-phase mode and curve display are not part of this edition. */
#include "engine.h"
#include "params.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 44100.0
#define NB 8
#define SUB 32
#define MAXSEC 4

enum { T_BELL, T_LOWSHELF, T_HIGHSHELF, T_LOWCUT, T_HIGHCUT, T_NOTCH, T_BANDPASS, T_TILT, T_COUNT };
enum { C_STEREO, C_MID, C_SIDE, C_LEFT, C_RIGHT };

static const char *TYPE_NAMES[T_COUNT] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt" };
static const char *CH_NAMES[5] = { "Stereo", "Mid", "Side", "Left", "Right" };
static const char *SLOPE_NAMES[6] = { "-6 dB", "-12 dB", "-18 dB", "-24 dB", "-36 dB", "-48 dB" };   /* no leading digit: that reads as an index */
static const int SLOPE_ORDER[6] = { 1, 2, 3, 4, 6, 8 };

/* frequency and Q knobs are 0..1 positions on a log scale (the wrapper maps knobs linearly) */
#define FMIN 20.0
#define FMAX 20000.0
#define QMIN 0.1
#define QMAX 18.0
static double pos_to_freq (double p) { return FMIN * pow (FMAX / FMIN, p); }
static double pos_to_q (double p)    { return QMIN * pow (QMAX / QMIN, p); }

typedef struct { double b0, b1, b2, a1, a2; } bq_t;

typedef struct {
    /* parameters */
    int on, type, slope, ch, dyn;
    double fpos, gain, qpos, thr, rng;
    /* smoothed */
    double f, g, q;
    int init;
    /* applied coefficients */
    bq_t sec[MAXSEC];
    int nsec;
    double af, ag, aq; int atype, aslope;
    double z[2][MAXSEC][2];
    /* dynamic detector */
    bq_t det; int ndet; double df, dq; int dtype;
    double dz[2][2];
    double env;
} band_t;

typedef struct {
    band_t b[NB];
    double out_db, out_gain, attack_ms, release_ms;
    float l[128], r[128], m[SUB], s[SUB];
} eq_t;

/* ---------- filter design (RBJ cookbook, bilinear with pre-warping) ---------- */
static double clampf (double f) { return f < 5.0 ? 5.0 : f > SR * 0.49 ? SR * 0.49 : f; }

static bq_t peaking (double f, double g, double q) {
    double A = pow (10.0, g / 40.0), w = 2 * M_PI * clampf (f) / SR, al = sin (w) / (2 * q), c = cos (w), a0 = 1 + al / A;
    bq_t r = { (1 + al * A) / a0, -2 * c / a0, (1 - al * A) / a0, -2 * c / a0, (1 - al / A) / a0 }; return r;
}
static bq_t lowshelf (double f, double g, double q) {
    double A = pow (10.0, g / 40.0), w = 2 * M_PI * clampf (f) / SR, c = cos (w), al = sin (w) / (2 * q), sA = 2 * sqrt (A) * al;
    double a0 = (A + 1) + (A - 1) * c + sA;
    bq_t r = { A * ((A + 1) - (A - 1) * c + sA) / a0, 2 * A * ((A - 1) - (A + 1) * c) / a0, A * ((A + 1) - (A - 1) * c - sA) / a0,
               -2 * ((A - 1) + (A + 1) * c) / a0, ((A + 1) + (A - 1) * c - sA) / a0 }; return r;
}
static bq_t highshelf (double f, double g, double q) {
    double A = pow (10.0, g / 40.0), w = 2 * M_PI * clampf (f) / SR, c = cos (w), al = sin (w) / (2 * q), sA = 2 * sqrt (A) * al;
    double a0 = (A + 1) - (A - 1) * c + sA;
    bq_t r = { A * ((A + 1) + (A - 1) * c + sA) / a0, -2 * A * ((A - 1) + (A + 1) * c) / a0, A * ((A + 1) + (A - 1) * c - sA) / a0,
               2 * ((A - 1) - (A + 1) * c) / a0, ((A + 1) - (A - 1) * c - sA) / a0 }; return r;
}
static bq_t lowpass (double f, double q) {
    double w = 2 * M_PI * clampf (f) / SR, c = cos (w), al = sin (w) / (2 * q), a0 = 1 + al;
    bq_t r = { (1 - c) * 0.5 / a0, (1 - c) / a0, (1 - c) * 0.5 / a0, -2 * c / a0, (1 - al) / a0 }; return r;
}
static bq_t highpass (double f, double q) {
    double w = 2 * M_PI * clampf (f) / SR, c = cos (w), al = sin (w) / (2 * q), a0 = 1 + al;
    bq_t r = { (1 + c) * 0.5 / a0, -(1 + c) / a0, (1 + c) * 0.5 / a0, -2 * c / a0, (1 - al) / a0 }; return r;
}
static bq_t bandpass (double f, double q) {
    double w = 2 * M_PI * clampf (f) / SR, c = cos (w), al = sin (w) / (2 * q), a0 = 1 + al;
    bq_t r = { al / a0, 0, -al / a0, -2 * c / a0, (1 - al) / a0 }; return r;
}
static bq_t notch (double f, double q) {
    double w = 2 * M_PI * clampf (f) / SR, c = cos (w), al = sin (w) / (2 * q), a0 = 1 + al;
    bq_t r = { 1 / a0, -2 * c / a0, 1 / a0, -2 * c / a0, (1 - al) / a0 }; return r;
}
static bq_t lp1 (double f) { double K = tan (M_PI * clampf (f) / SR), b0 = K / (1 + K); bq_t r = { b0, b0, 0, (K - 1) / (K + 1), 0 }; return r; }
static bq_t hp1 (double f) { double K = tan (M_PI * clampf (f) / SR), b0 = 1 / (1 + K); bq_t r = { b0, -b0, 0, (K - 1) / (K + 1), 0 }; return r; }

static int design_cut (bq_t *s, int high, double f, double q, int order) {
    int n = 0, odd = order & 1, pairs = order / 2;
    double res = q * M_SQRT2;
    if (odd) s[n++] = high ? hp1 (f) : lp1 (f);
    for (int k = 0; k < pairs && n < MAXSEC; k++) {
        double sq = odd ? 1.0 / (2 * cos (M_PI * (k + 1.0) / order)) : 1.0 / (2 * cos (M_PI * (2.0 * k + 1.0) / (2.0 * order)));
        if (k == pairs - 1) sq *= res;
        if (sq < 0.1) sq = 0.1; if (sq > 60) sq = 60;
        s[n++] = high ? highpass (f, sq) : lowpass (f, sq);
    }
    return n;
}

/* returns the number of sections */
static int design_band (bq_t *s, int type, double f, double g, double q, int slope) {
    if (q < 0.05) q = 0.05;
    switch (type) {
    case T_BELL:      s[0] = peaking (f, g, q); return 1;
    case T_LOWSHELF:  s[0] = lowshelf (f, g, q > 4 ? 4 : q); return 1;
    case T_HIGHSHELF: s[0] = highshelf (f, g, q > 4 ? 4 : q); return 1;
    case T_NOTCH:     s[0] = notch (f, q); return 1;
    case T_BANDPASS:  s[0] = bandpass (f, q); return 1;
    case T_LOWCUT:    return design_cut (s, 1, f, q, SLOPE_ORDER[slope]);
    case T_HIGHCUT:   return design_cut (s, 0, f, q, SLOPE_ORDER[slope]);
    case T_TILT: {
        bq_t b = highshelf (f, g, 0.5); double k = pow (10.0, -g / 40.0);
        b.b0 *= k; b.b1 *= k; b.b2 *= k; s[0] = b; return 1;
    }
    }
    return 0;
}

static int has_gain (int t) { return t == T_BELL || t == T_LOWSHELF || t == T_HIGHSHELF || t == T_TILT; }

static void run_sections (const bq_t *s, int n, double (*z)[2], float *x, int len) {
    for (int k = 0; k < n; k++) {
        const bq_t q = s[k];
        double z1 = z[k][0], z2 = z[k][1];
        for (int i = 0; i < len; i++) {
            double in = x[i], y = q.b0 * in + z1;
            z1 = q.b1 * in - q.a1 * y + z2;
            z2 = q.b2 * in - q.a2 * y;
            x[i] = (float) y;
        }
        /* flush denormals */
        z[k][0] = fabs (z1) < 1e-20 ? 0 : z1;
        z[k][1] = fabs (z2) < 1e-20 ? 0 : z2;
    }
}

static double run_det (const bq_t *b, int n, double *z, double x) {
    if (!n) return x;
    double y = b->b0 * x + z[0];
    z[0] = b->b1 * x - b->a1 * y + z[1];
    z[1] = b->b2 * x - b->a2 * y;
    return y;
}

/* ---------- engine ---------- */
static void band_defaults (band_t *b, int i) {
    memset (b, 0, sizeof *b);
    b->fpos = (double) i / (NB - 1) * 0.9 + 0.05;
    b->qpos = log (1.0 / QMIN) / log (QMAX / QMIN);    /* Q = 1 */
    b->slope = 3;
    b->thr = -24; b->rng = -6;
    b->atype = b->aslope = -1; b->dtype = -1;
}

static void *create (const char *dir) {
    (void) dir;
    eq_t *e = calloc (1, sizeof *e);
    if (!e) return NULL;
    for (int i = 0; i < NB; i++) band_defaults (&e->b[i], i);
    e->out_db = 0; e->out_gain = 1; e->attack_ms = 5; e->release_ms = 120;
    return e;
}
static void destroy (void *p) { free (p); }
static void midi (void *p, const uint8_t *m, int n) { (void) p; (void) m; (void) n; }

static int opt_index (const char *v, const char *const *names, int n) {
    if (v[0] >= '0' && v[0] <= '9') { int k = atoi (v); return k < 0 ? 0 : k >= n ? n - 1 : k; }
    for (int i = 0; i < n; i++) if (!strcasecmp (v, names[i])) return i;
    return 0;
}

static void load_state (eq_t *e, const char *s);
static void set_param (void *p, const char *key, const char *val) {
    eq_t *e = p;
    if (!strcmp (key, "state"))   { load_state (e, val); return; }
    if (!strcmp (key, "out"))     { e->out_db = atof (val); e->out_gain = pow (10.0, e->out_db / 20.0); return; }
    if (!strcmp (key, "attack"))  { e->attack_ms = atof (val); return; }
    if (!strcmp (key, "release")) { e->release_ms = atof (val); return; }
    int bi; char name[16];
    if (sscanf (key, "b%d_%15s", &bi, name) != 2 || bi < 1 || bi > NB) return;
    band_t *b = &e->b[bi - 1];
    static const char *OFFON[2] = { "Off", "On" };
    if      (!strcmp (name, "on"))    { int on = opt_index (val, OFFON, 2); if (on && !b->on) { memset (b->z, 0, sizeof b->z); memset (b->dz, 0, sizeof b->dz); b->env = 0; b->init = 0; } b->on = on; }
    else if (!strcmp (name, "type"))  b->type = opt_index (val, TYPE_NAMES, T_COUNT);
    else if (!strcmp (name, "freq"))  b->fpos = atof (val);
    else if (!strcmp (name, "gain"))  b->gain = atof (val);
    else if (!strcmp (name, "q"))     b->qpos = atof (val);
    else if (!strcmp (name, "slope")) b->slope = opt_index (val, SLOPE_NAMES, 6);
    else if (!strcmp (name, "ch"))    b->ch = opt_index (val, CH_NAMES, 5);
    else if (!strcmp (name, "dyn"))   b->dyn = opt_index (val, OFFON, 2);
    else if (!strcmp (name, "thr"))   b->thr = atof (val);
    else if (!strcmp (name, "rng"))   b->rng = atof (val);
}

static int fmt_freq (char *buf, int len, double f) {
    return f >= 1000 ? snprintf (buf, len, "%.*f kHz", f >= 10000 ? 1 : 2, f / 1000) : snprintf (buf, len, "%.0f Hz", f);
}

static const char *BAND_KEYS[] = { "on", "type", "freq", "gain", "q", "slope", "ch", "dyn", "thr", "rng" };
static int get_param (void *p, const char *key, char *buf, int len);

/* project save/restore: every parameter as "key=value;" text (the wrapper stores it as the VST chunk) */
static int save_state (eq_t *e, char *buf, int len) {
    int n = snprintf (buf, len, "v1;"), w;
    char k[24], v[48];
    const char *glob[] = { "out", "attack", "release" };
    for (int i = 0; i < 3 + NB * 10 && n < len; i++) {
        if (i < 3) snprintf (k, sizeof k, "%s", glob[i]);
        else snprintf (k, sizeof k, "b%d_%s", (i - 3) / 10 + 1, BAND_KEYS[(i - 3) % 10]);
        if (get_param (e, k, v, sizeof v) <= 0) continue;
        w = snprintf (buf + n, len - n, "%s=%s;", k, v);
        if (w < 0 || w >= len - n) return 0;
        n += w;
    }
    return n;
}

static void load_state (eq_t *e, const char *s) {
    if (strncmp (s, "v1;", 3)) return;
    s += 3;
    char k[24], v[48];
    while (*s) {
        const char *eq = strchr (s, '='), *end = eq ? strchr (eq, ';') : NULL;
        if (!eq || !end || eq - s >= (int) sizeof k || end - eq - 1 >= (int) sizeof v) return;
        memcpy (k, s, eq - s); k[eq - s] = 0;
        memcpy (v, eq + 1, end - eq - 1); v[end - eq - 1] = 0;
        if (strcmp (k, "state")) set_param (e, k, v);
        s = end + 1;
    }
}

static int get_param (void *p, const char *key, char *buf, int len) {
    eq_t *e = p;
    if (!strcmp (key, "state"))   return save_state (e, buf, len);
    if (!strcmp (key, "out"))     return snprintf (buf, len, "%g", e->out_db);
    if (!strcmp (key, "attack"))  return snprintf (buf, len, "%g", e->attack_ms);
    if (!strcmp (key, "release")) return snprintf (buf, len, "%g", e->release_ms);
    int bi; char name[24];
    if (sscanf (key, "b%d_%23s", &bi, name) != 2 || bi < 1 || bi > NB) return 0;
    band_t *b = &e->b[bi - 1];
    if (!strcmp (name, "on"))            return snprintf (buf, len, "%d", b->on);
    if (!strcmp (name, "type"))          return snprintf (buf, len, "%d", b->type);
    if (!strcmp (name, "freq"))          return snprintf (buf, len, "%g", b->fpos);
    if (!strcmp (name, "freq_display"))  return fmt_freq (buf, len, pos_to_freq (b->fpos));
    if (!strcmp (name, "gain"))          return snprintf (buf, len, "%g", b->gain);
    if (!strcmp (name, "q"))             return snprintf (buf, len, "%g", b->qpos);
    if (!strcmp (name, "q_display"))     return snprintf (buf, len, "%.2f", pos_to_q (b->qpos));
    if (!strcmp (name, "slope"))         return snprintf (buf, len, "%d", b->slope);
    if (!strcmp (name, "ch"))            return snprintf (buf, len, "%d", b->ch);
    if (!strcmp (name, "dyn"))           return snprintf (buf, len, "%d", b->dyn);
    if (!strcmp (name, "thr"))           return snprintf (buf, len, "%g", b->thr);
    if (!strcmp (name, "rng"))           return snprintf (buf, len, "%g", b->rng);
    return 0;
}

static void apply (band_t *b, eq_t *e, int start, int n, double gain_db) {
    float *L = e->l + start, *R = e->r + start;
    if (b->ch == C_LEFT)       run_sections (b->sec, b->nsec, b->z[0], L, n);
    else if (b->ch == C_RIGHT) run_sections (b->sec, b->nsec, b->z[1], R, n);
    else if (b->ch == C_MID || b->ch == C_SIDE) {
        for (int i = 0; i < n; i++) { e->m[i] = 0.5f * (L[i] + R[i]); e->s[i] = 0.5f * (L[i] - R[i]); }
        run_sections (b->sec, b->nsec, b->z[0], b->ch == C_MID ? e->m : e->s, n);
        for (int i = 0; i < n; i++) { L[i] = e->m[i] + e->s[i]; R[i] = e->m[i] - e->s[i]; }
    } else {
        run_sections (b->sec, b->nsec, b->z[0], L, n);
        run_sections (b->sec, b->nsec, b->z[1], R, n);
    }
    (void) gain_db;
}

static double detect (band_t *b, const float *inL, const float *inR, int n, double att, double rel) {
    if (b->type != b->dtype || fabs (b->f - b->df) > 1e-3 * b->f || fabs (b->q - b->dq) > 1e-3) {
        double q = b->q < 0.3 ? 0.3 : b->q > 8 ? 8 : b->q;
        b->ndet = 1;
        if (b->type == T_LOWSHELF)       b->det = lowpass (b->f, 0.707);
        else if (b->type == T_HIGHSHELF) b->det = highpass (b->f, 0.707);
        else if (b->type == T_TILT)      b->ndet = 0;
        else                             b->det = bandpass (b->f, q);
        if (b->type != b->dtype) memset (b->dz, 0, sizeof b->dz);
        b->dtype = b->type; b->df = b->f; b->dq = b->q;
    }
    double env = b->env;
    for (int i = 0; i < n; i++) {
        double l = inL[i], r = inR[i], lv;
        switch (b->ch) {
        case C_LEFT:  lv = fabs (run_det (&b->det, b->ndet, b->dz[0], l)); break;
        case C_RIGHT: lv = fabs (run_det (&b->det, b->ndet, b->dz[1], r)); break;
        case C_MID:   lv = fabs (run_det (&b->det, b->ndet, b->dz[0], 0.5 * (l + r))); break;
        case C_SIDE:  lv = fabs (run_det (&b->det, b->ndet, b->dz[0], 0.5 * (l - r))); break;
        default: {
            double a = fabs (run_det (&b->det, b->ndet, b->dz[0], l)), c = fabs (run_det (&b->det, b->ndet, b->dz[1], r));
            lv = a > c ? a : c;
        }
        }
        double k = lv > env ? att : rel;
        env = lv + k * (env - lv);
    }
    b->env = env < 1e-12 ? 0 : env;
    double envdb = 20 * log10 (env + 1e-9), over = envdb - b->thr;
    if (over <= 0) return 0;
    return b->rng < 0 ? (-over < b->rng ? b->rng : -over) : (over > b->rng ? b->rng : over);
}

static void process (void *p, const int16_t *in, int16_t *out, int frames) {
    eq_t *e = p;
    if (frames > 128) frames = 128;
    float inL[128], inR[128];
    for (int i = 0; i < frames; i++) {
        e->l[i] = inL[i] = in[2 * i] * (1.0f / 32768.0f);
        e->r[i] = inR[i] = in[2 * i + 1] * (1.0f / 32768.0f);
    }
    const double att = exp (-1.0 / (e->attack_ms * 0.001 * SR));
    const double rel = exp (-1.0 / (e->release_ms * 0.001 * SR));
    const double k = 1.0 - exp (-(double) SUB / (0.02 * SR));   /* ~20 ms parameter glide */

    for (int start = 0; start < frames; start += SUB) {
        int n = frames - start < SUB ? frames - start : SUB;
        for (int bi = 0; bi < NB; bi++) {
            band_t *b = &e->b[bi];
            if (!b->on) continue;
            double tf = pos_to_freq (b->fpos), tq = pos_to_q (b->qpos), tg = b->gain;
            if (!b->init) { b->f = tf; b->q = tq; b->g = tg; b->init = 1; }
            else {
                b->f = exp (log (b->f) + k * (log (tf) - log (b->f)));
                b->q = exp (log (b->q) + k * (log (tq) - log (b->q)));
                b->g += k * (tg - b->g);
                if (fabs (b->f - tf) < 1e-4 * tf) b->f = tf;
                if (fabs (b->q - tq) < 1e-4 * tq) b->q = tq;
                if (fabs (b->g - tg) < 1e-4) b->g = tg;
            }
            double delta = (b->dyn && has_gain (b->type)) ? detect (b, inL + start, inR + start, n, att, rel) : 0;
            double eg = b->g + delta;
            if (b->type != b->atype || b->slope != b->aslope || fabs (b->f - b->af) > 1e-5 * b->f
                || fabs (eg - b->ag) > 1e-3 || fabs (b->q - b->aq) > 1e-5 * b->q) {
                if (b->type != b->atype || b->slope != b->aslope) memset (b->z, 0, sizeof b->z);
                b->nsec = design_band (b->sec, b->type, b->f, eg, b->q, b->slope);
                b->atype = b->type; b->aslope = b->slope; b->af = b->f; b->ag = eg; b->aq = b->q;
            }
            apply (b, e, start, n, eg);
        }
    }

    for (int i = 0; i < frames; i++) {
        float l = (float) (e->l[i] * e->out_gain), r = (float) (e->r[i] * e->out_gain);
        if (!isfinite (l)) l = 0;
        if (!isfinite (r)) r = 0;
        l *= 32768.0f; r *= 32768.0f;
        out[2 * i]     = l >= 32767.0f ? 32767 : l <= -32768.0f ? -32768 : (int16_t) lrintf (l);
        out[2 * i + 1] = r >= 32767.0f ? 32767 : r <= -32768.0f ? -32768 : (int16_t) lrintf (r);
    }
}

static void render (void *p, int16_t *out, int frames) { (void) p; memset (out, 0, sizeof (int16_t) * 2 * frames); }

static const mpc_engine_t API = { create, destroy, midi, set_param, get_param, render, process, NULL };
__attribute__((visibility("default"))) const mpc_engine_t *mpc_engine (void) { return &API; }
