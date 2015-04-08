/* 
 * papi-perf-counters.c
 *
 * From: http://stackoverflow.com/questions/8091182/how-to-read-performance-counters-on-i5-i7-cpus
 */

#include <stdio.h>
#include <stdlib.h>
#include <papi.h>

#define NUM_EVENTS 4

void matmul(const double *A, const double *B,
        double *C, int m, int n, int p)
{
    int i, j, k;
    for (i = 0; i < m; ++i)
        for (j = 0; j < p; ++j) {
            double sum = 0;
            for (k = 0; k < n; ++k)
                sum += A[i*n + k] * B[k*p + j];
            C[i*p + j] = sum;
        }
}

int main()
{
    const int size = 300;
    double a[size][size];
    double b[size][size];
    double c[size][size];

    int event[NUM_EVENTS] = {PAPI_TOT_INS, PAPI_TOT_CYC, PAPI_BR_MSP, PAPI_L1_DCM };
    long long values[NUM_EVENTS];

    /* Start counting events */
    if (PAPI_start_counters(event, NUM_EVENTS) != PAPI_OK) {
        fprintf(stderr, "PAPI_start_counters - FAILED\n");
        exit(1);
    }

    matmul((double *)a, (double *)b, (double *)c, size, size, size);

    /* Read the counters */
    if (PAPI_read_counters(values, NUM_EVENTS) != PAPI_OK) {
        fprintf(stderr, "PAPI_read_counters - FAILED\n");
        exit(1);
    }

    printf("Total instructions: %lld\n", values[0]);
    printf("Total cycles: %lld\n", values[1]);
    printf("Instr per cycle: %2.3f\n", (double)values[0] / (double) values[1]);
    printf("Branches mispredicted: %lld\n", values[2]);
    printf("L1 Cache misses: %lld\n", values[3]);

    /* Stop counting events */
    if (PAPI_stop_counters(values, NUM_EVENTS) != PAPI_OK) {
        fprintf(stderr, "PAPI_stoped_counters - FAILED\n");
        exit(1);
    }

    return 0;
} 
