/*
 * Lecture 3 - Homework Starter Code
 *
 * GOAL: Convert a polling loop to an event-driven workqueue architecture.
 *
 * The starter code works but is INEFFICIENT.
 * polling_thread wakes every 10ms to check a flag.
 * sensor_sim fires every 100ms - that's 10 wasted wake-ups per event.
 *
 *
 * ================================================================
 * TASKS
 * ================================================================
 *
 * TASK 1 (starter - already works, just run it):
 *   Run the starter. Count wake-ups vs real events in the log.
 *   Expected: ~10 wake-ups per sensor event. Confirm this.
 *
 * TASK 2 (implement):
 *   Replace polling_thread with a k_work handler.
 *   sensor_sim should call k_work_submit() instead of setting a flag.
 *   The handler should do what polling_thread currently does.
 *
 *   Steps:
 *   - Define a work item with K_WORK_DEFINE
 *   - Write the handler function
 *   - In sensor_sim: call k_work_submit() (remove k_sem_give + flag)
 *   - Remove the polling_thread entirely
 *
 * TASK 3 (verify):
 *   Add k_uptime_get_32() to your handler's LOG_INF.
 *   Confirm handler runs only when sensor_sim fires (every ~100ms).
 *   No unnecessary wake-ups.
 *
 * BONUS (debounce):
 *   Change sensor_sim to fire 5 events within 20ms (not 1 per 100ms).
 *   Use k_work_reschedule with 30ms delay so only ONE handler
 *   call occurs after the burst - not 5.
 *   Log the reschedule timestamps to confirm the burst collapses.
 *
 * ================================================================
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_DBG);

#define STACK_SIZE          1024
#define SENSOR_MS           100    /* wait before the burst starts */
#define BURST_EVENT_COUNT   5      /* events produced in one burst */
#define BURST_WINDOW_MS     20     /* maximum burst duration required */
#define DEBOUNCE_MS         30     /* wait for the burst to settle */

/* ================================================================
 * DEBOUNCE VERSION
 * sensor_sim reschedules delayable work for each event in a burst.
 * ================================================================ */

/* Statistics */
static int total_events;
static int total_processed;

static void sensor_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    total_processed++;
    LOG_INF("[HANDLER] processed burst %d  raw_events=%d  tick=%u",
            total_processed, total_events, k_uptime_get_32());
}
K_WORK_DELAYABLE_DEFINE(debounce_work, sensor_handler);

/* ------------------------------------------------------------------ */
/*  sensor_sim - fires one rapid burst for debounce testing      */
/* ------------------------------------------------------------------ */

static void sensor_sim_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    k_msleep(SENSOR_MS);
    uint32_t burst_start = k_uptime_get_32();

    for (int i = 0; i < BURST_EVENT_COUNT; i++) {
        total_events++;
        uint32_t now = k_uptime_get_32();
        LOG_INF("[SENSOR] event %d/%d  tick=%u  burst_elapsed=%u",
                i + 1, BURST_EVENT_COUNT, now, now - burst_start);

        int ret = k_work_reschedule(&debounce_work, K_MSEC(DEBOUNCE_MS));
        if (ret < 0) {
            LOG_ERR("reschedule failed: %d", ret);
        } else {
            LOG_INF("[SENSOR] debounce rescheduled for +%dms  tick=%u",
                    DEBOUNCE_MS, k_uptime_get_32());
        }
    }

    LOG_INF("[SENSOR] all events produced in %ums",
            k_uptime_get_32() - burst_start);
}

K_THREAD_DEFINE(sensor_thread,  STACK_SIZE, sensor_sim_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    LOG_INF("=== L3 Homework: Debounced Workqueue ===");
    LOG_INF("Debounce: %d events within %dms, handler delay %dms",
            BURST_EVENT_COUNT, BURST_WINDOW_MS, DEBOUNCE_MS);
    LOG_INF("Handler should run once after the burst.");

    /* Wait long enough for all events to complete */
    k_msleep(SENSOR_MS + BURST_WINDOW_MS + DEBOUNCE_MS + 500);

    return 0;
}
