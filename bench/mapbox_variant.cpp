#include "bench_api.hpp"
#include "bench_framework.hpp"
#include <mapbox/variant.hpp>

static constexpr uint32_t num_variants{NUM_VARIANTS};
static constexpr uint32_t seq_length{SEQ_LENGTH};
static constexpr uint32_t repeat_num{REPEAT_NUM};
static constexpr uint32_t rng_seed{RNG_SEED};

// Dummy visitor

struct dummy_visitor {
  template <uint32_t N>
  uint32_t operator()(const dummy<N> &) const {
    uint32_t result{N};
#ifdef OPAQUE_VISIT
    // This makes the return value of the visitor opaque to the optimizer
    benchmark::DoNotOptimize(result);
    benchmark::ClobberMemory();
#endif
    return result;
  }
};

struct visitor_applier {
  template <typename T>
  uint32_t operator()(T && t) const {
    return mapbox::util::apply_visitor(dummy_visitor{}, std::forward<T>(t));
  }
};

int
main() {
  benchmark::run_benchmark<mapbox::util::variant, num_variants, seq_length, repeat_num,
                            visitor_applier>("mapbox::variant", rng_seed);
}
