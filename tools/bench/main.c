#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "bench.h"

enum { Pattern = 4096, Warmup = 20000, BigEvery = 61, PauseEvery = 997, ModeCount = 10, ModeLut = 8, ModeCustomCurve = 9 };
static const long long PauseNs = 150000000;

long long bench_now;

long long ktime_get(void) {
    return bench_now;
}

static const struct {
    const char *name;
    int mode, smoothing;
    const char *acceleration, *exponent, *midpoint, *motivity, *sensitivity, *pre_scale;
} named[] = {
    {"current", 0, 0, "1.25", "1.5", "1.6", "2", "0.4", "1.25"},
    {"linear", 1, 0, "0.05", "1.5", "1.6", "2", "0.4", "1.25"},
    {"power", 2, 0, "1.25", "1.5", "1.6", "2", "0.4", "1.25"},
    {"classic", 3, 0, "0.05", "1.5", "1.6", "2", "0.4", "1.25"},
    {"motivity", 4, 0, "1.25", "1.5", "1.6", "2", "0.4", "1.25"},
    {"synchronous", 5, 1, "1.6", "1.5", "0.5", "2", "0.4", "1.25"},
    {"natural", 6, 0, "0.05", "1.5", "1.6", "2", "0.4", "1.25"},
    {"jump", 7, 0, "1.25", "1.5", "1.6", "2", "0.4", "1.25"},
};

static int pattern_x[Pattern], pattern_y[Pattern];
static long long pattern_gap[Pattern];

static unsigned next_random(unsigned *state) {
    *state = *state * 1103515245u + 12345u;
    return *state >> 16;
}

#define PICK(list) list[next_random(&state) % (sizeof(list) / sizeof(list[0]))]

static void make_pattern(long long step) {
    unsigned state = 20261009u;
    for (int i = 0; i < Pattern; i++) {
        int big = i % BigEvery == BigEvery - 1;
        pattern_x[i] = big ? (int) (next_random(&state) % 4001) - 2000 : (int) (next_random(&state) % 121) - 60;
        pattern_y[i] = big ? (int) (next_random(&state) % 4001) - 2000 : (int) (next_random(&state) % 61) - 30;
        pattern_gap[i] = step - step / 4 + (long long) (next_random(&state) % (unsigned) (step / 2 + 1));
        if (i % PauseEvery == PauseEvery - 1)
            pattern_gap[i] = PauseNs;
    }
}

static struct bench_setting neutral_setting(void) {
    struct bench_setting s;
    memset(&s, 0, sizeof(s));
    s.ratio_yx = "1";
    s.output_cap = s.input_cap = s.offset = s.rotation = s.snap_angle = s.snap_threshold = "0";
    return s;
}

static int named_setting(const char *name, struct bench_setting *s) {
    for (size_t m = 0; m < sizeof(named) / sizeof(named[0]); m++) {
        if (strcmp(named[m].name, name) != 0)
            continue;
        *s = neutral_setting();
        s->name = named[m].name;
        s->mode = named[m].mode;
        s->smoothing = named[m].smoothing;
        s->acceleration = named[m].acceleration;
        s->exponent = named[m].exponent;
        s->midpoint = named[m].midpoint;
        s->motivity = named[m].motivity;
        s->sensitivity = named[m].sensitivity;
        s->pre_scale = named[m].pre_scale;
        return 1;
    }
    return 0;
}

static void decimal(char *out, long long thousandths) {
    snprintf(out, 24, "%s%lld.%03lld", thousandths < 0 ? "-" : "", llabs(thousandths) / 1000, llabs(thousandths) % 1000);
}

static void generated_setting(long id, struct bench_setting *s) {
    static const char *const accelerations[] = {"0", "0.001", "0.05", "0.3", "1", "1.25", "2", "10", "1000"};
    static const char *const exponents[] = {"0", "0.001", "0.15", "0.4", "1", "1.5", "2", "4"};
    static const char *const midpoints[] = {"0", "0.5", "1.6", "5", "9.75", "20", "100"};
    static const char *const motivities[] = {"0", "1", "1.5", "2", "3", "50"};
    static const char *const sensitivities[] = {"0.1", "0.4", "1", "2.5"};
    static const char *const pre_scales[] = {"1", "1.25", "0.5", "2"};
    static const char *const ratios[] = {"1", "1", "0.5", "2"};
    static const char *const output_caps[] = {"0", "0", "1.5", "10"};
    static const char *const input_caps[] = {"0", "0", "20", "200"};
    static const char *const offsets[] = {"0", "0", "1", "5"};
    static const char *const rotations[] = {"0", "0", "0.0873", "-0.5"};
    static const char *const snap_angles[] = {"0", "1.5708"};
    static const char *const snap_thresholds[] = {"0", "0", "0.1", "0.3"};
    static const int lut_sizes[] = {2, 3, 5, 16, BenchLutPoints};
    static const int lut_steps[] = {500, 2000, 10000};
    static const int lut_slopes[] = {0, 50, 250, -10};
    static const char *const input_offsets[] = {"0", "0", "2"};
    static const char *const legacy_caps[] = {"0", "0", "1.5"};
    static const char *const lp_norms[] = {"2", "2", "1", "3", "20"};
    static const char *const stretches[] = {"1", "1", "0.5", "2"};
    static const char *const input_half_lives[] = {"0", "0", "2"};
    static const char *const scale_half_lives[] = {"0", "0", "5"};
    static const char *const output_half_lives[] = {"0", "0", "3"};
    static const char *const axis_snaps[] = {"0", "0", "0.2"};
    static const char *const speed_clamps[] = {"0", "0", "50"};
    static const char *const directional_ratios[] = {"1", "1", "0.8", "1.5"};
    static const char *const min_times[] = {"0", "0", "0.0625", "1"};
    static const char *const max_times[] = {"100", "100", "20"};
    unsigned state = (unsigned) id * 2654435761u + 1u;

    *s = neutral_setting();
    s->name = "sweep";
    s->mode = (int) (id % ModeCount);
    s->smoothing = (int) (next_random(&state) % 2);
    s->acceleration = PICK(accelerations);
    s->exponent = PICK(exponents);
    s->midpoint = PICK(midpoints);
    s->motivity = PICK(motivities);
    s->sensitivity = PICK(sensitivities);
    s->pre_scale = PICK(pre_scales);
    s->ratio_yx = PICK(ratios);
    s->output_cap = PICK(output_caps);
    s->input_cap = PICK(input_caps);
    s->offset = PICK(offsets);
    s->rotation = PICK(rotations);
    s->snap_angle = PICK(snap_angles);
    s->snap_threshold = PICK(snap_thresholds);
    if (s->mode == ModeLut || s->mode == ModeCustomCurve) {
        int step = PICK(lut_steps), slope = PICK(lut_slopes);
        s->lut_points = PICK(lut_sizes);
        for (int i = 0; i < s->lut_points; i++) {
            decimal(s->lut[2 * i], (long long) (i + 1) * step);
            decimal(s->lut[2 * i + 1], 1000 + (long long) i * slope);
        }
    }
    if (id % 4 != 3)
        return;
    s->fork_only = 1;
    s->extras.input_offset = PICK(input_offsets);
    s->extras.legacy_cap = PICK(legacy_caps);
    s->extras.lp_norm = PICK(lp_norms);
    s->extras.domain_x = PICK(stretches);
    s->extras.domain_y = PICK(stretches);
    s->extras.range_x = PICK(stretches);
    s->extras.range_y = PICK(stretches);
    s->extras.input_half_life = PICK(input_half_lives);
    s->extras.scale_half_life = PICK(scale_half_lives);
    s->extras.output_half_life = PICK(output_half_lives);
    s->extras.axis_snap = PICK(axis_snaps);
    s->extras.speed_clamp = PICK(speed_clamps);
    s->extras.ratio_lr = PICK(directional_ratios);
    s->extras.ratio_ud = PICK(directional_ratios);
    s->extras.min_time = PICK(min_times);
    s->extras.max_time = PICK(max_times);
    s->extras.fixed_time = strcmp(s->extras.min_time, "0") != 0 && next_random(&state) % 4 == 0;
    s->extras.lut_velocity = s->lut_points > 0 && next_random(&state) % 2;
    s->extras.by_component = next_random(&state) % 4 == 0;
    s->extras.y_mode = (int) (next_random(&state) % ModeLut);
    s->extras.truncate_carry = (int) (next_random(&state) % 2);
}

static uint64_t mix(uint64_t hash, int value) {
    return (hash ^ (uint32_t) value) * 0x100000001b3ull;
}

static void finish_packet(void) {
#if defined(__x86_64__)
    __asm__ volatile("lfence" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("isb" ::: "memory");
#else
#error "no instruction barrier is known for this architecture"
#endif
}

static double now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double) t.tv_sec * 1e9 + (double) t.tv_nsec;
}

static uint64_t run_packets(long long packets, FILE *trace) {
    uint64_t hash = 0xcbf29ce484222325ull;
    for (long long i = 0; i < packets; i++) {
        int x = pattern_x[i % Pattern], y = pattern_y[i % Pattern];
        bench_now += pattern_gap[i % Pattern];
        bench_packet(&x, &y);
        if (trace)
            fprintf(trace, "%lld %d %d %lld %d %d\n", i, pattern_x[i % Pattern], pattern_y[i % Pattern],
                    pattern_gap[i % Pattern], x, y);
        hash = mix(mix(hash, x), y);
    }
    return hash;
}

static int time_setting(const char *name, long long packets, int exact, int alone) {
    struct bench_setting setting;
    uint64_t hash = 0xcbf29ce484222325ull;
    if (!named_setting(name, &setting)) {
        fprintf(stderr, "no setting named %s\n", name);
        return 2;
    }
    const char *problem = bench_configure(&setting, exact);
    if (problem) {
        fprintf(stderr, "%s refused: %s\n", name, problem);
        return 1;
    }
    for (long long i = 0; i < Warmup; i++) {
        int x = pattern_x[i % Pattern], y = pattern_y[i % Pattern];
        bench_now += pattern_gap[i % Pattern];
        bench_packet(&x, &y);
        if (alone)
            finish_packet();
    }
    double start = now_ns();
    for (long long i = 0; i < packets; i++) {
        int x = pattern_x[i % Pattern], y = pattern_y[i % Pattern];
        bench_now += pattern_gap[i % Pattern];
        bench_packet(&x, &y);
        if (alone)
            finish_packet();
        hash = mix(mix(hash, x), y);
    }
    double ns = (now_ns() - start) / (double) packets;
    printf("%s %.3f %016llx\n", name, ns, (unsigned long long) hash);
    return 0;
}

static int sweep(long first, long count, long long packets, int exact) {
    for (long id = first; id < first + count; id++) {
        fflush(stdout);
        pid_t child = fork();
        if (child < 0) {
            perror("fork");
            return 1;
        }
        if (child == 0) {
            struct bench_setting setting;
            generated_setting(id, &setting);
            const char *problem = bench_configure(&setting, exact);
            if (problem)
                printf("%ld refused: %s\n", id, problem);
            else
                printf("%ld %016llx\n", id, (unsigned long long) run_packets(packets, NULL));
            fflush(stdout);
            _exit(0);
        }
        int status;
        if (waitpid(child, &status, 0) != child) {
            perror("waitpid");
            return 1;
        }
        if (WIFSIGNALED(status))
            printf("%ld crashed: signal %d\n", id, WTERMSIG(status));
        else if (WEXITSTATUS(status) != 0)
            printf("%ld failed: status %d\n", id, WEXITSTATUS(status));
    }
    return 0;
}

static int usage(const char *self) {
    fprintf(stderr,
            "usage: %s time <ns between packets> <packets> <plain|exact> <setting> <back-to-back|one-at-a-time>\n"
            "       %s sweep <ns between packets> <first id> <count> <packets> <plain|exact>\n"
            "       %s trace <ns between packets> <id or setting> <packets> <plain|exact>\n"
            "       %s curve <ns between packets> <id or setting> <plain|exact>\n",
            self, self, self, self);
    return 2;
}

static int find_setting(const char *which, struct bench_setting *setting) {
    char *end;
    long id;
    if (named_setting(which, setting))
        return 1;
    id = strtol(which, &end, 10);
    if (*which == '\0' || *end != '\0' || id < 0)
        return 0;
    generated_setting(id, setting);
    return 1;
}

static int trace(const char *which, long long packets, int exact) {
    struct bench_setting setting;
    if (!find_setting(which, &setting))
        return usage("bench");
    const char *problem = bench_configure(&setting, exact);
    if (problem) {
        printf("refused: %s\n", problem);
        return 0;
    }
    fflush(stdout);
    run_packets(packets, stdout);
    return 0;
}

static int curve(const char *which, int exact) {
    struct bench_setting setting;
    if (!find_setting(which, &setting))
        return usage("bench");
    const char *problem = bench_configure(&setting, exact);
    if (problem) {
        printf("refused: %s\n", problem);
        return 0;
    }
    for (long long speed = 1ll << 16; speed < 1ll << 48; speed += (speed >> 12) + 1)
        printf("%lld %lld\n", speed, bench_curve(speed));
    return 0;
}

static int math_switch(const char *word, int *exact) {
    *exact = strcmp(word, "exact") == 0;
    return *exact || strcmp(word, "plain") == 0;
}

int main(int argc, char **argv) {
    int exact;
    if (argc < 3)
        return usage(argv[0]);
    long long step = atoll(argv[2]);
    if (step <= 0)
        return usage(argv[0]);
    make_pattern(step);
    if (strcmp(argv[1], "time") == 0 && argc == 7) {
        long long packets = atoll(argv[3]);
        int alone = strcmp(argv[6], "one-at-a-time") == 0;
        if (packets <= 0 || !math_switch(argv[4], &exact) || (!alone && strcmp(argv[6], "back-to-back") != 0))
            return usage(argv[0]);
        return time_setting(argv[5], packets, exact, alone);
    }
    if (strcmp(argv[1], "sweep") == 0 && argc == 7) {
        long first = atol(argv[3]), count = atol(argv[4]);
        long long packets = atoll(argv[5]);
        if (first < 0 || count <= 0 || packets <= 0 || !math_switch(argv[6], &exact))
            return usage(argv[0]);
        return sweep(first, count, packets, exact);
    }
    if (strcmp(argv[1], "curve") == 0 && argc == 5) {
        if (!math_switch(argv[4], &exact))
            return usage(argv[0]);
        return curve(argv[3], exact);
    }
    if (strcmp(argv[1], "trace") == 0 && argc == 6) {
        long long packets = atoll(argv[4]);
        if (packets <= 0 || !math_switch(argv[5], &exact))
            return usage(argv[0]);
        return trace(argv[3], packets, exact);
    }
    return usage(argv[0]);
}
