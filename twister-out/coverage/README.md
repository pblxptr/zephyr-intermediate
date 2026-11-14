# Lecture 08 coverage comparison

Coverage for `app/modules/temp_alarm/src/temp_alarm.c`, collected on
`native_sim` with both ordered and shuffled scenarios.

| Metric | Task 1 (`32e1c7e`, `l8-task1`) | Task 2 (`47a905c`, `l8-task2`) |
| --- | --- | --- |
| Lines | 79.6% (43/54) | 100.0% (54/54) |
| Functions | 100.0% (5/5) | 100.0% (5/5) |
| Branches | 55.6% (10/18) | 100.0% (18/18) |

Task 2 reaches the 11 previously uncovered lines:

- The log and `-ENODEV` return when no sensor device is supplied.
- The `-EINVAL` returns for invalid initial and replacement thresholds.
- The error assignment, log, and return in each of the two sensor-failure handlers.
- The `-EINVAL` return when the status pointer is NULL.

Function coverage was already 100% after Task 1 because every public function
had been called. Line and branch coverage reveal the error handling that those
calls did not exercise. Task 2 also checks recovery after a failed fetch and
confirms that rejected thresholds preserve the previous threshold.

The HTML report lists 28 excluded branches; the percentages above use the
18 remaining branches, matching the homework's expected results.

All 13 tests passed in each scenario with no skips or warnings in the final run.
Open [index.html](index.html) to inspect the final report.

To reproduce at either milestone, run from `test-lab` with the workspace virtual
environment activated:

```sh
west twister -T tests/temp_alarm -p native_sim \
  --coverage --coverage-tool gcovr --gcov-tool gcov \
  --coverage-basedir app/modules/temp_alarm
```

The Task 1 HTML report is also preserved locally in
`../traces/l8-task1-coverage/` relative to the repository root.
