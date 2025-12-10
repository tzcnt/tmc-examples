#include "tmc/all_headers.hpp"
#include "tmc/detail/thread_layout.hpp"
#include <coroutine>

// An executor that delegates to efficiency cores, if available.
// On a hybrid CPU system:
// - priority 0 (high priority) tasks will be sent to P-cores, at priority 0
// - priority 1 (low priority) tasks will be sent to E-cores, at priority 0
// On a homogeneous CPU system:
// - all tasks will be sent to P-cores, at their original priority
struct ex_hybrid {
  tmc::ex_cpu performance_executor;
  tmc::ex_cpu efficiency_executor;

  bool hybrid_enabled;
  tmc::ex_any type_erased_this;

  inline ex_hybrid() : hybrid_enabled{false}, type_erased_this{this} {}

  inline void init() {
    auto topo = tmc::topology::query();
    hybrid_enabled = topo.is_hybrid();
    if (hybrid_enabled) {
      {
        // This will take priority 0 (high priority) items
        tmc::topology::TopologyFilter f;
        f.set_cpu_kinds(tmc::topology::CpuKind::PERFORMANCE);
        performance_executor.set_topology_filter(f);
        performance_executor.init();
      }
      {
        // This will take priority 1 (low priority) items
        tmc::topology::TopologyFilter f;
        f.set_cpu_kinds(tmc::topology::CpuKind::EFFICIENCY1);
        efficiency_executor.set_topology_filter(f);
        efficiency_executor.init();
      }
    } else {
      // We only have 1 executor - it takes both high and low priority
      performance_executor.set_priority_count(2);
      performance_executor.init();
    }
  }

  inline void post(
    tmc::work_item&& Item, size_t Priority = 0, size_t ThreadHint = NO_HINT
  ) {
    if (hybrid_enabled && Priority > 0) {
      efficiency_executor.post(
        static_cast<tmc::work_item&&>(Item), 0, ThreadHint
      );
    } else [[likely]] {
      performance_executor.post(
        static_cast<tmc::work_item&&>(Item), 0, ThreadHint
      );
    }
  }

  template <typename It>
  inline void post_bulk(
    It&& Items, size_t Count, size_t Priority = 0, size_t ThreadHint = NO_HINT
  ) {
    if (hybrid_enabled && Priority > 0) {
      efficiency_executor.post_bulk(
        static_cast<It&&>(Items), Count, 0, ThreadHint
      );
    } else [[likely]] {
      performance_executor.post_bulk(
        static_cast<It&&>(Items), Count, 0, ThreadHint
      );
    }
  }

  /// Returns a pointer to the type erased `ex_any` version of this executor.
  /// This object shares a lifetime with this executor.
  /// Unlike most executors, this CANNOT be used for pointer-based equality
  /// comparison against the thread-local `tmc::current_executor()`,
  /// because threads will be owned by the child executors.
  inline tmc::ex_any* type_erased() { return &type_erased_this; }
};

// The implementation of tmc::detail::executor_traits for ex_hybrid,
// which makes this compatible with all of the TMC functions.
namespace tmc::detail {
template <> struct executor_traits<ex_hybrid> {
  inline static void post(
    ex_hybrid& ex, tmc::work_item&& Item, size_t Priority, size_t ThreadHint
  ) {
    ex.post(static_cast<tmc::work_item&&>(Item), Priority, ThreadHint);
  }

  template <typename It>
  inline static void post_bulk(
    ex_hybrid& ex, It&& Items, size_t Count, size_t Priority, size_t ThreadHint
  ) {
    ex.post_bulk(static_cast<It&&>(Items), Count, Priority, ThreadHint);
  }

  inline static tmc::ex_any* type_erased(ex_hybrid& ex) {
    return ex.type_erased();
  }

  inline static std::coroutine_handle<> task_enter_context(
    ex_hybrid& ex, std::coroutine_handle<> Outer, size_t Priority
  ) {
    ex.post(static_cast<std::coroutine_handle<>&&>(Outer), Priority);
    return std::noop_coroutine();
  }
};
} // namespace tmc::detail
