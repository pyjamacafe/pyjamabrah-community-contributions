/* spsc_ring.c - a single-producer, single-consumer ring buffer
 *
 * Build and run on a laptop:
 *   gcc -O2 -std=c11 -pthread spsc_ring.c -o spsc_ring && ./spsc_ring
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define RING_SIZE 8u                 /* must be a power of two */
#define MASK      (RING_SIZE - 1u)
#define N_JOBS    1000000u

typedef struct {
    uint32_t id;
    uint32_t len;
} job_t;

typedef struct {
    job_t            slots[RING_SIZE];
    _Atomic uint32_t head;           /* only the producer writes this */
    _Atomic uint32_t tail;           /* only the consumer writes this */
} ring_t;

/* Producer side */
bool ring_put(ring_t *r, const job_t *job)
{
    uint32_t head = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t next = (head + 1) & MASK;

    if (next == atomic_load_explicit(&r->tail, memory_order_acquire))
        return false;                                  /* full */

    r->slots[head] = *job;                             /* 1. write the data  */
    atomic_store_explicit(&r->head, next,
                          memory_order_release);       /* 2. then publish it */
    return true;
}

/* Consumer side */
bool ring_get(ring_t *r, job_t *out)
{
    uint32_t tail = atomic_load_explicit(&r->tail, memory_order_relaxed);

    if (tail == atomic_load_explicit(&r->head, memory_order_acquire))
        return false;                                  /* empty */

    *out = r->slots[tail];                             /* 1. read the data   */
    atomic_store_explicit(&r->tail, (tail + 1) & MASK,
                          memory_order_release);       /* 2. then free slot  */
    return true;
}

/* ---- demo: one thread produces, one thread consumes ---- */
static ring_t ring;

static void *producer(void *arg)
{
    (void)arg;
    for (uint32_t i = 1; i <= N_JOBS; ) {
        job_t job = { .id = i, .len = i * 2 };
        if (ring_put(&ring, &job))
            i++;                     /* ring full? just try again */
    }
    return NULL;
}

static void *consumer(void *arg)
{
    (void)arg;
    uint32_t expected = 1, errors = 0;
    while (expected <= N_JOBS) {
        job_t job;
        if (!ring_get(&ring, &job))
            continue;                /* ring empty? just try again */
        if (job.id != expected || job.len != expected * 2)
            errors++;
        expected++;
    }
    printf("received %u jobs, %u errors\n", N_JOBS, errors);
    return NULL;
}

int main(void)
{
    pthread_t p, c;
    pthread_create(&c, NULL, consumer, NULL);
    pthread_create(&p, NULL, producer, NULL);
    pthread_join(p, NULL);
    pthread_join(c, NULL);
    return 0;
}
