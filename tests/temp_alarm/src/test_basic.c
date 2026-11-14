/*
 * temp_alarm - Happy-path unit tests (homework skeleton)
 *
 * test_status_after_init is provided as a worked example. Tests cover the
 * Task 1 scenarios in TEST_SPEC.md using the fake sensor device.
 *
 * Run:
 *   west twister -T tests/temp_alarm -p native_sim
 */

#include <errno.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "sensor_fake.h"
#include "temp_alarm.h"

#define DEFAULT_THRESHOLD 30

/*
 * Shared by every suite: a clean fake, then the module initialised
 * with the fake device. temp_alarm_init() resets all module state,
 * so no deinit is needed between tests.
 */
static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	sensor_fake_reset();
	sensor_fake_init();

	zassume_ok(temp_alarm_init(&fake_sensor_dev, DEFAULT_THRESHOLD),
		   "precondition: temp_alarm_init must succeed");
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_get_status
 *
 * The status right after a successful init.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_get_status, NULL, NULL, before, NULL, NULL);

/* PROVIDED — study this test before writing the rest. */
ZTEST(temp_alarm_get_status, test_status_after_init)
{
	struct temp_alarm_status status;

	zassert_ok(temp_alarm_get_status(&status), "get_status must succeed");

	zassert_equal(status.last_temp, 0, "last_temp must be 0 after init, got %d",
		      status.last_temp);
	zassert_equal(status.alarm_count, 0U, "alarm_count must be 0 after init, got %u",
		      status.alarm_count);
	zassert_false(status.is_alarming, "is_alarming must be false after init");
	zassert_equal(status.last_error, 0, "last_error must be 0 after init, got %d",
		      status.last_error);
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_check
 *
 * Readings below, at and above the threshold.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_check, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_check, test_below_threshold)
{
	struct temp_alarm_status status;

	sensor_fake_set_temperature(20);

	zassert_ok(temp_alarm_check(), "temp_alarm_check must succeed");
	zassert_ok(temp_alarm_get_status(&status), "temp_alarm_get_status must succeed");
	zassert_equal(status.last_temp, 20, "last_temp must be 20, got %d", status.last_temp);
	zassert_false(status.is_alarming, "is_alarming must be false");

	zassert_equal(fake_sensor_sample_fetch_fake.call_count, 1U,
		      "sample should be fetched exactly once");
	zassert_equal(fake_sensor_sample_fetch_fake.arg1_val, SENSOR_CHAN_AMBIENT_TEMP);
	zassert_equal(fake_sensor_channel_get_fake.arg1_val, SENSOR_CHAN_AMBIENT_TEMP);
}

ZTEST(temp_alarm_check, test_at_threshold)
{
	struct temp_alarm_status status;

	sensor_fake_set_temperature(30);

	zassert_ok(temp_alarm_check(), "check at threshold must succeed");
	zassert_ok(temp_alarm_get_status(&status));
	zassert_false(status.is_alarming, "a reading equal to the threshold must not alarm");
	zassert_equal(status.alarm_count, 0U);
}

ZTEST(temp_alarm_check, test_above_threshold)
{
	struct temp_alarm_status status;

	sensor_fake_set_temperature(40);

	zassert_ok(temp_alarm_check(), "check above threshold must succeed");
	zassert_ok(temp_alarm_get_status(&status));
	zassert_equal(status.last_temp, 40);
	zassert_true(status.is_alarming);
	zassert_equal(status.alarm_count, 1U);
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_threshold
 *
 * Changing the threshold at run time.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_threshold, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_threshold, test_new_threshold_clears_alarm)
{
	struct temp_alarm_status status;

	sensor_fake_set_temperature(40);
	zassert_ok(temp_alarm_check());
	zassert_ok(temp_alarm_get_status(&status));
	zassert_true(status.is_alarming, "precondition: 40 C must raise the alarm");
	zassert_equal(status.alarm_count, 1U);

	zassert_ok(temp_alarm_set_threshold(50));
	zassert_ok(temp_alarm_get_status(&status));
	zassert_false(status.is_alarming, "changing the threshold must clear the alarm immediately");

	zassert_ok(temp_alarm_check());
	zassert_ok(temp_alarm_get_status(&status));
	zassert_false(status.is_alarming, "40 C is below the new threshold");
	zassert_equal(status.alarm_count, 1U, "changing the threshold must keep the alarm count");
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_reset
 *
 * temp_alarm_reset() clears the alarm but keeps the last reading.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_reset, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_reset, test_reset_clears_state)
{
	struct temp_alarm_status status;

	sensor_fake_set_temperature(40);
	for (int i = 0; i < 3; i++) {
		zassert_ok(temp_alarm_check());
	}
	zassert_ok(temp_alarm_get_status(&status));
	zassert_true(status.is_alarming);
	zassert_equal(status.alarm_count, 3U);

	temp_alarm_reset();

	zassert_ok(temp_alarm_get_status(&status));
	zassert_false(status.is_alarming);
	zassert_equal(status.alarm_count, 0U);
	zassert_equal(status.last_error, 0);
	zassert_equal(status.last_temp, 40, "reset must keep the last reading");
}
