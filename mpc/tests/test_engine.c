/* Offline DSP checks for the MPC engine: feed sines through process() and compare gains with the design. */
#include "engine.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int fails = 0;
static void check (int ok, const char *what, double got) { printf ("%s  %-55s %.2f\n", ok ? "PASS" : "FAIL", what, got); if (!ok) fails++; }
static double fpos (double f) { return log (f / 20.0) / log (1000.0); }
static double qpos (double q) { return log (q / 0.1) / log (180.0); }

/* returns gain in dB on L and R for a sine of amplitude a (lamp/ramp scale each channel) */
static void measure (const mpc_engine_t *api, void *e, double f, double adb, double lamp, double ramp, double *gl, double *gr) {
    int16_t in[256], out[256];
    double ph = 0, a = pow (10, adb / 20), il = 0, ol = 0, orr = 0, ir = 0;
    for (int blk = 0; blk < 400; blk++) {
        for (int i = 0; i < 128; i++) { double s = a * sin (ph); ph += 2 * M_PI * f / 44100;
            in[2*i] = (int16_t) lrint (s * lamp * 32767); in[2*i+1] = (int16_t) lrint (s * ramp * 32767); }
        api->process (e, in, out, 128);
        if (blk >= 300) for (int i = 0; i < 128; i++) {
            il += (double) in[2*i]*in[2*i]; ir += (double) in[2*i+1]*in[2*i+1];
            ol += (double) out[2*i]*out[2*i]; orr += (double) out[2*i+1]*out[2*i+1]; }
    }
    *gl = 10 * log10 ((ol + 1e-9) / (il + 1e-9)); *gr = 10 * log10 ((orr + 1e-9) / (ir + 1e-9));
}
static void set (const mpc_engine_t *api, void *e, const char *k, double v) { char b[32]; snprintf (b, sizeof b, "%g", v); api->set_param (e, k, b); }

int main (void) {
    const mpc_engine_t *api = mpc_engine ();
    void *e = api->create (NULL);
    double l, r;
    measure (api, e, 1000, -20, 1, 1, &l, &r); check (fabs (l) < 0.05, "all bands off = bypass", l);

    set (api, e, "b1_freq", fpos (1000)); set (api, e, "b1_gain", 6); set (api, e, "b1_q", qpos (1)); api->set_param (e, "b1_on", "On");
    measure (api, e, 1000, -20, 1, 1, &l, &r); check (fabs (l - 6) < 0.15 && fabs (r - 6) < 0.15, "bell +6 dB at 1 kHz", l);
    measure (api, e, 100, -20, 1, 1, &l, &r);  check (fabs (l) < 0.2, "bell leaves 100 Hz alone", l);

    api->set_param (e, "b1_ch", "Side");
    measure (api, e, 1000, -20, 1, 1, &l, &r);  check (fabs (l) < 0.15, "side band: mono (mid) untouched", l);
    measure (api, e, 1000, -20, 1, -1, &l, &r); check (fabs (l - 6) < 0.15, "side band: side boosted +6", l);
    api->set_param (e, "b1_ch", "Left");
    measure (api, e, 1000, -20, 1, 1, &l, &r);  check (fabs (l - 6) < 0.15 && fabs (r) < 0.15, "left-only band", r);
    api->set_param (e, "b1_ch", "Stereo");

    api->set_param (e, "b1_type", "Low Cut"); api->set_param (e, "b1_slope", "-24 dB"); set (api, e, "b1_freq", fpos (200)); set (api, e, "b1_q", qpos (0.7071));
    measure (api, e, 200, -20, 1, 1, &l, &r); check (fabs (l + 3) < 0.3, "24 dB low cut: -3 dB at cutoff", l);
    measure (api, e, 100, -20, 1, 1, &l, &r); check (fabs (l + 24.1) < 0.5, "24 dB low cut: -24 dB one octave below", l);
    api->set_param (e, "b1_slope", "-12 dB");
    measure (api, e, 100, -20, 1, 1, &l, &r); check (fabs (l + 12.3) < 0.5, "12 dB low cut: -12 dB one octave below", l);
    api->set_param (e, "b1_slope", "1");     /* index form, as the wrapper sends it */
    measure (api, e, 100, -20, 1, 1, &l, &r); check (fabs (l + 12.3) < 0.5, "slope by index (wrapper form)", l);
    api->set_param (e, "b1_slope", "-24 dB");

    api->set_param (e, "b1_type", "Bell"); set (api, e, "b1_freq", fpos (1000)); set (api, e, "b1_gain", 0); set (api, e, "b1_q", qpos (1));
    api->set_param (e, "b1_dyn", "On"); set (api, e, "b1_thr", -30); set (api, e, "b1_rng", -6); set (api, e, "attack", 2); set (api, e, "release", 50);
    measure (api, e, 1000, -10, 1, 1, &l, &r); check (fabs (l + 6) < 0.6, "dynamic: loud signal gets full -6 dB", l);
    measure (api, e, 1000, -45, 1, 1, &l, &r); check (fabs (l) < 0.3, "dynamic: quiet signal untouched", l);

    api->set_param (e, "b1_dyn", "Off"); set (api, e, "out", -6);
    measure (api, e, 1000, -20, 1, 1, &l, &r); check (fabs (l + 6) < 0.15, "output trim -6 dB", l);

    char st[4096]; int n = api->get_param (e, "state", st, sizeof st);
    void *e2 = api->create (NULL); api->set_param (e2, "state", st);
    char a[64], b[64]; api->get_param (e2, "b1_dyn", a, 64); api->get_param (e2, "out", b, 64);
    check (n > 0 && !strcmp (a, "0") && atof (b) == -6, "state save/restore", n);
    api->destroy (e2);

    /* robustness: random parameters every block, no NaN, no stuck output */
    srand (3); int bad = 0; int16_t in[256], out[256];
    const char *keys[] = { "on", "type", "freq", "gain", "q", "slope", "ch", "dyn", "thr", "rng" };
    for (int it = 0; it < 4000; it++) {
        char k[24], v[24]; int bi = rand () % 8 + 1, ki = rand () % 10;
        snprintf (k, sizeof k, "b%d_%s", bi, keys[ki]);
        double x = rand () / (double) RAND_MAX;
        if (ki == 0 || ki == 7) snprintf (v, sizeof v, "%d", rand () % 2);
        else if (ki == 1) snprintf (v, sizeof v, "%d", rand () % 8);
        else if (ki == 5) snprintf (v, sizeof v, "%d", rand () % 6);
        else if (ki == 6) snprintf (v, sizeof v, "%d", rand () % 5);
        else if (ki == 3 || ki == 9) snprintf (v, sizeof v, "%g", x * 48 - 24);
        else if (ki == 8) snprintf (v, sizeof v, "%g", -x * 60);
        else snprintf (v, sizeof v, "%g", x);
        api->set_param (e, k, v);
        for (int i = 0; i < 256; i++) in[i] = (int16_t) (rand () % 20000 - 10000);
        api->process (e, in, out, 128);
    }
    set (api, e, "out", 0);
    for (int i = 0; i < 8; i++) { char k[16]; snprintf (k, 16, "b%d_on", i + 1); api->set_param (e, k, "Off"); }
    measure (api, e, 1000, -20, 1, 1, &l, &r); bad = fabs (l) > 0.05;
    check (!bad, "after 4000 random edits, bands off = clean bypass", l);
    api->destroy (e);
    printf (fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails != 0;
}
