#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(l2_task1, LOG_LEVEL_DBG);

#define STACK_SIZE 1024
#define PRIO 5
#define INCREMENTS 1000000
#define EXPECTED_TOTAL (2U * INCREMENTS)

static volatile uint32_t counter;
static K_MUTEX_DEFINE(counter_mtx);
static struct k_thread worker_a;
static struct k_thread worker_b;
K_THREAD_STACK_DEFINE(stack_a, STACK_SIZE);
K_THREAD_STACK_DEFINE(stack_b, STACK_SIZE);

void worker_unprotected_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    for (int i = 0; i < INCREMENTS; i++) {
        uint32_t value = counter;

        /*
         * Split the read-modify-write and widen the race window so it is
         * visible even on a single CPU with equal-priority threads.
         * A reads N and yields; B reads N and yields; both write N + 1.
         * Two increments then produce only one update. Volatile does not
         * make this sequence atomic or provide mutual exclusion.
         */
        if (i % 1000 == 0) {
            k_yield();
        }
        counter = value + 1;
    }
}

void worker_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    for (int i = 0; i < INCREMENTS; i++) {
        k_mutex_lock(&counter_mtx, K_FOREVER);
        counter++;
        k_mutex_unlock(&counter_mtx);
    }
}

static uint32_t run_pair(k_thread_entry_t entry)
{
    /* Reset only after the previous pair has terminated. */
    counter = 0;
    k_thread_create(&worker_a, stack_a, K_THREAD_STACK_SIZEOF(stack_a),
                    entry, NULL, NULL, NULL, PRIO, 0, K_FOREVER);
    k_thread_create(&worker_b, stack_b, K_THREAD_STACK_SIZEOF(stack_b),
                    entry, NULL, NULL, NULL, PRIO, 0, K_FOREVER);
    k_thread_start(&worker_a);
    k_thread_start(&worker_b);
    k_thread_join(&worker_a, K_FOREVER);
    k_thread_join(&worker_b, K_FOREVER);
    return counter;
}

int main(void)
{
    uint32_t unprotected = run_pair(worker_unprotected_fn);
    uint32_t protected = run_pair(worker_fn);

    LOG_INF("Expected value: %u", EXPECTED_TOTAL);
    LOG_INF("Unprotected: %u (lost %u updates)",
            unprotected, EXPECTED_TOTAL - unprotected);
    LOG_INF("Mutex-protected: %u", protected);

    /*
     * The mutex covers the entire read-modify-write, so only one worker
     * can increment at a time. After both workers finish, all 2 * INCREMENTS
     * updates are counted. The unprotected result depends on interleaving.
     */
    if (protected != EXPECTED_TOTAL) {
        LOG_ERR("Mutex-protected total does not match expected value");
        return 1;
    }

    return 0;
}
