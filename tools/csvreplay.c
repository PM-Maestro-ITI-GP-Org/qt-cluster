/*
 * csvreplay.c
 * ----------------------------------------------------------------------------
 * Replays a recorded CSV into the motor_controller shared-memory contract, so
 * the cluster shows real speed and power with no STM32 and no Pi attached. The
 * cluster cannot tell this from the real producer: it publishes the same
 * shm_region_t, the same 200-row blocks, at the same 100 Hz cadence.
 *
 * Expected columns (the header line is skipped):
 *     timestamp,current_0..current_7,vib_x,vib_y,vib_z,rpm
 * one row per 20 kHz sample. current_3 is the speed command and current_0..2 /
 * current_4..6 are the phase currents and voltages -- see MotorChannel in
 * SpiReader.h.
 *
 * Build (needs motor_shm.h from the producer tree, which is not vendored here):
 *     gcc -std=c11 -O2 -I../../motor-data-producer tools/csvreplay.c \
 *         -o /tmp/csvreplay -lrt
 *
 * Run:
 *     /tmp/csvreplay <file.csv>            # loops until Ctrl-C
 * ----------------------------------------------------------------------------
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include "motor_shm.h"

#define SAMPLE_HZ 20000   /* matches config.json sample_rate_hz */
#define ROWS      200     /* matches config.json block_rows -> 100 Hz blocks */

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.csv>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) { perror("fopen"); return 1; }

    /* Whole file into memory: 595k rows of the 24-byte row is ~14 MB, and
     * replaying from RAM keeps the 10 ms cadence off the disk.              */
    size_t cap = 1u << 20, n = 0;
    motor_row_t *all = malloc(cap * sizeof *all);
    if (!all) { perror("malloc"); return 1; }

    char line[512];
    if (!fgets(line, sizeof line, f)) { fprintf(stderr, "empty file\n"); return 1; }
    while (fgets(line, sizeof line, f)) {
        if (n == cap) {
            cap *= 2;
            motor_row_t *grown = realloc(all, cap * sizeof *all);
            if (!grown) { perror("realloc"); return 1; }
            all = grown;
        }
        unsigned long long ts; int c[8], vx, vy, vz, rp;
        if (sscanf(line, "%llu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                   &ts, &c[0],&c[1],&c[2],&c[3],&c[4],&c[5],&c[6],&c[7],
                   &vx,&vy,&vz,&rp) != 13)
            continue;                     /* skip malformed / short lines */
        for (int i = 0; i < 8; ++i) all[n].current[i] = (uint16_t)c[i];
        all[n].vib_x = (int16_t)vx;
        all[n].vib_y = (int16_t)vy;
        all[n].vib_z = (int16_t)vz;
        all[n].rpm   = (uint16_t)rp;
        ++n;
    }
    fclose(f);

    if (n < ROWS) { fprintf(stderr, "only %zu rows, need >= %d\n", n, ROWS); return 1; }
    printf("loaded %zu rows (%.1f s at %d Hz)\n", n, (double)n / SAMPLE_HZ, SAMPLE_HZ);

    shm_unlink(MOTOR_SHM_NAME);           /* drop a stale region from a crash */
    int fd = shm_open(MOTOR_SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { perror("shm_open"); return 1; }
    if (ftruncate(fd, sizeof(shm_region_t))) { perror("ftruncate"); return 1; }
    shm_region_t *r = mmap(NULL, sizeof(shm_region_t),
                           PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (r == MAP_FAILED) { perror("mmap"); return 1; }
    motor_shm_region_init(r);

    printf("replaying into %s -- start the cluster now, Ctrl-C to stop\n",
           MOTOR_SHM_NAME);
    fflush(stdout);

    size_t   pos = 0;
    uint32_t seq = 0;
    for (;;) {
        if (pos + ROWS > n) { pos = 0; printf("  ...looping\n"); fflush(stdout); }

        frame_header_t h = {
            MOTOR_FRAME_MAGIC, seq++,
            (uint64_t)pos * 1000000ULL / SAMPLE_HZ,
            MOTOR_CONTRACT_VERSION, 0, ROWS, 0
        };
        /* Both publish paths, as the real producer does. The cluster reads the
         * ring -- the snapshot alone is one arbitrary point on the waveform. */
        motor_ring_publish(&r->ring, &h, &all[pos], SAMPLE_HZ);
        motor_snapshot_publish(&r->snapshot, &all[pos + ROWS - 1],
                               h.seq, h.timestamp, 0);

        if ((seq % 100u) == 0u) {         /* once a second */
            double sum = 0;
            for (int i = 0; i < ROWS; ++i) sum += all[pos + i].current[3];
            printf("  t=%5.1fs  speed_cmd=%4.0f counts\n",
                   (double)pos / SAMPLE_HZ, sum / ROWS);
            fflush(stdout);
        }

        pos += ROWS;
        struct timespec ts = { 0, 10 * 1000 * 1000 };   /* 10 ms per block */
        nanosleep(&ts, NULL);
    }
}
