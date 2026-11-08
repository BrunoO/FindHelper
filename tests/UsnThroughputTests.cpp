#include "doctest/doctest.h"

#include "usn/UsnThroughput.h"

using usn_throughput::ComputeThroughput;
using usn_throughput::FormatDrainEta;
using usn_throughput::ThroughputRates;
using usn_throughput::ThroughputSample;

TEST_SUITE("UsnThroughput - ComputeThroughput") {

  TEST_CASE("nominal window derives buffers/s, records/s and ETA") {
    const ThroughputSample prev{100, 1000, 50};
    const ThroughputSample cur{120, 1400, 40};
    const ThroughputRates rates = ComputeThroughput(prev, cur, 10.0);
    CHECK(rates.buffers_per_sec == doctest::Approx(2.0));
    CHECK(rates.records_per_sec == doctest::Approx(40.0));
    CHECK(rates.has_eta);
    // 40 buffers queued at 2/s drains in 20s.
    CHECK(rates.drain_eta_sec == doctest::Approx(20.0));
  }

  TEST_CASE("empty queue has no ETA") {
    const ThroughputSample prev{100, 1000, 0};
    const ThroughputSample cur{120, 1400, 0};
    const ThroughputRates rates = ComputeThroughput(prev, cur, 10.0);
    CHECK(rates.buffers_per_sec == doctest::Approx(2.0));
    CHECK_FALSE(rates.has_eta);
  }

  TEST_CASE("stalled window has no ETA") {
    const ThroughputSample prev{100, 1000, 40};
    const ThroughputSample cur{100, 1000, 40};
    const ThroughputRates rates = ComputeThroughput(prev, cur, 10.0);
    CHECK(rates.buffers_per_sec == doctest::Approx(0.0));
    CHECK_FALSE(rates.has_eta);
  }

  TEST_CASE("zero or negative elapsed yields zeros") {
    const ThroughputSample prev{100, 1000, 40};
    const ThroughputSample cur{120, 1400, 40};
    const ThroughputRates zero = ComputeThroughput(prev, cur, 0.0);
    CHECK(zero.buffers_per_sec == doctest::Approx(0.0));
    CHECK(zero.records_per_sec == doctest::Approx(0.0));
    CHECK_FALSE(zero.has_eta);
    const ThroughputRates negative = ComputeThroughput(prev, cur, -1.0);
    CHECK(negative.buffers_per_sec == doctest::Approx(0.0));
    CHECK_FALSE(negative.has_eta);
  }

  TEST_CASE("counter reset after ResetMetrics yields no spike") {
    const ThroughputSample prev{5000, 50000, 40};
    const ThroughputSample cur{10, 100, 5};
    const ThroughputRates rates = ComputeThroughput(prev, cur, 1.0);
    CHECK(rates.buffers_per_sec == doctest::Approx(0.0));
    CHECK(rates.records_per_sec == doctest::Approx(0.0));
    CHECK_FALSE(rates.has_eta);
  }
}

TEST_SUITE("UsnThroughput - FormatDrainEta") {

  TEST_CASE("seconds only") {
    CHECK(FormatDrainEta(0.0) == "0s");
    CHECK(FormatDrainEta(45.0) == "45s");
  }

  TEST_CASE("minutes pad seconds") {
    CHECK(FormatDrainEta(65.0) == "1m 05s");
    CHECK(FormatDrainEta(125.0) == "2m 05s");
  }

  TEST_CASE("hours drop seconds") {
    CHECK(FormatDrainEta(3720.0) == "1h 2m");
  }

  TEST_CASE("negative clamps to zero") {
    CHECK(FormatDrainEta(-5.0) == "0s");
  }
}
