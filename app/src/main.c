#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(l2_task1, LOG_LEVEL_DBG);

#define STACK_SIZE 1024
#define PRIO 5
#define INCREMENTS 1000000

static volatile uint32_t counter;
static struct k_sem done_sem;

void worker_fn(void *p1, void *p2, void *p3)
{
    const char *th_name = k_thread_name_get(k_current_get());

    for (int i = 0; i < INCREMENTS; i++) {
        counter++;
    }

    LOG_INF("[%s] is done", th_name);

    k_sem_give(&done_sem);
}

K_THREAD_DEFINE(worker_a, STACK_SIZE, worker_fn,
                NULL, NULL, NULL, PRIO, 0, 0);
K_THREAD_DEFINE(worker_b, STACK_SIZE, worker_fn,
                NULL, NULL, NULL, PRIO, 0, 0);

int main(void)
{
    k_sem_init(&done_sem, 0, 2);

    LOG_INF("Expected value: %d", INCREMENTS * 2);

    k_sem_take(&done_sem, K_FOREVER);
    k_sem_take(&done_sem, K_FOREVER);

    if (counter == INCREMENTS * 2) {
        LOG_WRN("No race detected");
    } else {
        LOG_ERR("Race condition detected, lost %d updates", (INCREMENTS * 2) - counter);
    }


    return 0;
}

