#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/task_wdt/task_wdt.h>

LOG_MODULE_REGISTER(l5_task, LOG_LEVEL_DBG);

#define STACK_SIZE        2048
#define SENSOR_COUNT      18
#define SENSOR_PERIOD_MS  100
#define QUEUE_DEPTH       6
#define WDT_TIMEOUT_MS    2000

/* ================================================================== */
/*  Shared message and queue                                          */
/* ================================================================== */
struct sensor_msg {
    uint32_t timestamp_ms;
    int32_t temperature_mc;
    uint8_t seq;
};

K_MSGQ_DEFINE(sensor_q, sizeof(struct sensor_msg), QUEUE_DEPTH, 4);

static K_SEM_DEFINE(prod_done, 0, 1);
static K_SEM_DEFINE(cons_done, 0, 1);
static K_SEM_DEFINE(health_done, 0, 1);
static atomic_t prod_finished;
static atomic_t cons_finished;

/* ================================================================== */
/*  Producer                                                          */
/* ================================================================== */
static void sensor_producer(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "sensor");

    for (int i = 0; i < SENSOR_COUNT; i++) {
        struct sensor_msg msg = {
            .timestamp_ms = k_uptime_get_32(),
            .temperature_mc = 24000 + (i * 350),
            .seq = (uint8_t)i,
        };

        int ret = k_msgq_put(&sensor_q, &msg, K_MSEC(200));
        if (ret == 0) {
            LOG_INF("[PRODUCER] sent seq=%u temp=%d mC q=%u/%d",
                    msg.seq, msg.temperature_mc,
                    k_msgq_num_used_get(&sensor_q), QUEUE_DEPTH);
        } else {
            LOG_WRN("[PRODUCER] put failed ret=%d seq=%u q=%u/%d",
                    ret, msg.seq,
                    k_msgq_num_used_get(&sensor_q), QUEUE_DEPTH);
        }

        k_msleep(SENSOR_PERIOD_MS);
    }

    atomic_set(&prod_finished, 1);
    LOG_INF("[PRODUCER] done");
    k_sem_give(&prod_done);
}

/* ================================================================== */
/*  Consumer and software watchdog                                    */
/* ================================================================== */
static void sensor_task_wdt_handler(int channel_id, void *data)
{
    k_tid_t thid = (k_tid_t)data;

    LOG_ERR("Sensor task watchdog timeout, channel id: %d, thread=%s",
            channel_id, k_thread_name_get(thid));
}

static void sensor_consumer(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "logger");

    int wdt_chan = task_wdt_add(WDT_TIMEOUT_MS, sensor_task_wdt_handler,
                                (void *)k_current_get());
    if (wdt_chan < 0) {
        LOG_ERR("Failed to add task watchdog: %d", wdt_chan);
        atomic_set(&cons_finished, 1);
        k_sem_give(&cons_done);
        return;
    }

    int received = 0;

    while (true) {
        struct sensor_msg msg;
        int ret = k_msgq_get(&sensor_q, &msg, K_MSEC(300));
        if (ret != 0) {
            if (atomic_get(&prod_finished) &&
                k_msgq_num_used_get(&sensor_q) == 0) {
                break;
            }

            LOG_WRN("[CONSUMER] timeout waiting for message");
            continue;
        }

        received++;
        task_wdt_feed(wdt_chan);

        LOG_INF("[CONSUMER] got seq=%u temp=%d mC q=%u/%d latency=%ums",
                msg.seq, msg.temperature_mc,
                k_msgq_num_used_get(&sensor_q), QUEUE_DEPTH,
                k_uptime_get_32() - msg.timestamp_ms);

        /* Increasing delay simulates a stuck consumer once it exceeds 2 s. */
        k_msleep(350 * (received < 10 ? received : 10) );
    }

    task_wdt_delete(wdt_chan);
    LOG_INF("[CONSUMER] done received=%d", received);
    atomic_set(&cons_finished, 1);
    k_sem_give(&cons_done);
}

/* ================================================================== */
/*  Queue health check                                                */
/* ================================================================== */
static void health_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "health");

    while (!atomic_get(&cons_finished)) {
        k_msleep(120);

        uint32_t used = k_msgq_num_used_get(&sensor_q);

        LOG_INF("[HEALTH] sensor_queue=%u/%u",
                used, (unsigned int)QUEUE_DEPTH);
        LOG_DBG("[HEALTH] queue has %u free slots", QUEUE_DEPTH - used);

        /* Compare without rounding down the 75% threshold. */
        if (used * 100U >= QUEUE_DEPTH * 75U) {
            LOG_WRN("[HEALTH] sensor queue at or above 75%% capacity (%u/%u)",
                    used, (unsigned int)QUEUE_DEPTH);
        }
    }

    LOG_INF("[HEALTH] done");
    k_sem_give(&health_done);
}

/* ================================================================== */
/*  Runtime threads                                                   */
/* ================================================================== */
K_THREAD_STACK_DEFINE(prod_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(cons_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(health_stack, STACK_SIZE);

static struct k_thread prod_thread;
static struct k_thread cons_thread;
static struct k_thread health_thread;

int main(void)
{
    LOG_INF("=== L5 Task: Message Queue Pipeline ===");

    int ret = task_wdt_init(NULL);
    if (ret != 0) {
        LOG_ERR("Failed to initialize task watchdog: %d", ret);
        return ret;
    }

    k_thread_create(&health_thread, health_stack,
                    K_THREAD_STACK_SIZEOF(health_stack), health_fn,
                    NULL, NULL, NULL, 7, 0, K_NO_WAIT);

    k_thread_create(&prod_thread, prod_stack,
                    K_THREAD_STACK_SIZEOF(prod_stack), sensor_producer,
                    NULL, NULL, NULL, 5, 0, K_NO_WAIT);

    /* Let the producer buffer messages before the consumer starts. */
    k_msleep(180);

    k_thread_create(&cons_thread, cons_stack,
                    K_THREAD_STACK_SIZEOF(cons_stack), sensor_consumer,
                    NULL, NULL, NULL, 6, 0, K_NO_WAIT);

    k_sem_take(&prod_done, K_FOREVER);
    k_sem_take(&cons_done, K_FOREVER);
    k_sem_take(&health_done, K_FOREVER);

    LOG_INF("=== Demo complete ===");
    return 0;
}
