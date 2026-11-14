/*
 * temp_alarm - Error-path unit tests
 *
 * Task 2 scenarios from TEST_SPEC.md: sensor failures, recovery and invalid input.
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

/* Same before() hook as in test_basic.c: a clean fake and an initialised module. */
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
 * Test Suite: temp_alarm_sensor_errors
 *
 * Failures from the sensor driver, injected through the fakes.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_sensor_errors, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_sensor_errors, test_fetch_error_propagates)
{
	struct temp_alarm_status status;

	fake_sensor_sample_fetch_fake.return_val = -EIO;

	zassert_equal(temp_alarm_check(), -EIO, "fetch error must propagate to the caller");
	zassert_ok(temp_alarm_get_status(&status));
	zassert_equal(status.last_error, -EIO, "fetch error must be stored in the status");
	zassert_equal(fake_sensor_channel_get_fake.call_count, 0U,
		      "a failed fetch must prevent the channel read");
}

ZTEST(temp_alarm_sensor_errors, test_channel_get_error_propagates)
{
	struct temp_alarm_status status;

	/* The temperature callback takes precedence over return_val. */
	fake_sensor_channel_get_fake.custom_fake = NULL;
	fake_sensor_channel_get_fake.return_val = -EIO;

	zassert_equal(temp_alarm_check(), -EIO, "channel read error must propagate");
	zassert_ok(temp_alarm_get_status(&status));
	zassert_equal(status.last_error, -EIO);
	zassert_equal(fake_sensor_sample_fetch_fake.call_count, 1U);
}

ZTEST(temp_alarm_sensor_errors, test_fail_then_recover)
{
	struct temp_alarm_status status;
	int fetch_results[] = {-EIO, 0};

	SET_RETURN_SEQ(fake_sensor_sample_fetch, fetch_results, ARRAY_SIZE(fetch_results));
	sensor_fake_set_temperature(20);

	zassert_equal(temp_alarm_check(), -EIO);
	zassert_ok(temp_alarm_check(), "the second fetch must recover");
	zassert_ok(temp_alarm_get_status(&status));
	zassert_equal(status.last_temp, 20);
	zassert_false(status.is_alarming);
	zassert_equal(status.last_error, -EIO, "recovery must retain the last error until reset");
}

/*
 * ============================================================================
 * Test Suite: temp_alarm_invalid_input
 *
 * Arguments the public API must reject.
 * ============================================================================
 */
ZTEST_SUITE(temp_alarm_invalid_input, NULL, NULL, before, NULL, NULL);

ZTEST(temp_alarm_invalid_input, test_get_status_null)
{
	zassert_equal(temp_alarm_get_status(NULL), -EINVAL);
}

ZTEST(temp_alarm_invalid_input, test_init_without_device)
{
	zassert_equal(temp_alarm_init(NULL, DEFAULT_THRESHOLD), -ENODEV);
}

ZTEST(temp_alarm_invalid_input, test_init_threshold_out_of_range)
{
	zassert_equal(temp_alarm_init(&fake_sensor_dev, -1), -EINVAL);
	zassert_equal(temp_alarm_init(&fake_sensor_dev, 101), -EINVAL);
}

ZTEST(temp_alarm_invalid_input, test_set_threshold_out_of_range)
{
	struct temp_alarm_status status;
	const int invalid_thresholds[] = {-1, 101};

	for (size_t i = 0; i < ARRAY_SIZE(invalid_thresholds); i++) {
		zassert_equal(temp_alarm_set_threshold(invalid_thresholds[i]), -EINVAL);

		/* Probe both sides to verify that the threshold is still exactly 30 C. */
		sensor_fake_set_temperature(DEFAULT_THRESHOLD);
		zassert_ok(temp_alarm_check());
		zassert_ok(temp_alarm_get_status(&status));
		zassert_false(status.is_alarming, "30 C must not raise the alarm");

		sensor_fake_set_temperature(DEFAULT_THRESHOLD + 1);
		zassert_ok(temp_alarm_check());
		zassert_ok(temp_alarm_get_status(&status));
		zassert_true(status.is_alarming, "31 C must still raise the alarm");
	}
}
