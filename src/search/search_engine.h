#ifndef SEARCH_ENGINE_H
#define SEARCH_ENGINE_H

#include "operator_cost.h"
#include "operator_id.h"
#include "plan_manager.h"
#include "search_progress.h"
#include "search_space.h"
#include "search_statistics.h"
#include "state_registry.h"
#include "task_proxy.h"

#include <vector>
#include <string>

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/unordered_set.hpp>
#include <boost/container_hash/hash.hpp>

namespace bip = boost::interprocess;
namespace bctr = boost::container;

typedef bip::managed_shared_memory::segment_manager                         SegmentManager;
typedef bip::allocator<void, SegmentManager>                                VoidAllocator;
typedef bip::allocator<char, SegmentManager>                                CharAllocator;
typedef bctr::basic_string<char, std::char_traits<char>, CharAllocator>     String;
typedef bip::allocator<String, SegmentManager>                              StringAllocator;
typedef bctr::vector<String, StringAllocator>                               StringVector;
typedef bip::allocator<StringVector, SegmentManager>                        StringVectorAllocator;
typedef bctr::vector<StringVector, StringVectorAllocator>                   StringVectorVector;

typedef bip::allocator<int, SegmentManager>                             IntAllocator;
typedef bctr::vector<int, IntAllocator>                                 VecInt;
typedef bip::allocator<VecInt, SegmentManager>                          VecIntAllocator;
struct my_hash : boost::hash_detail::hash_base<VecInt> {
   std::size_t operator()(VecInt const& val) const
   {
      return boost::hash_range(val.begin(), val.end());
   }
};

typedef boost::unordered_set<
   VecInt,
   my_hash,
   std::equal_to<VecInt>,
   VecIntAllocator>                                                     VecIntSet;

namespace options {
class OptionParser;
class Options;
}

namespace ordered_set {
template<typename T>
class OrderedSet;
}

namespace successor_generator {
class SuccessorGenerator;
}

namespace utils {
enum class Verbosity;
}

enum SearchStatus {IN_PROGRESS, TIMEOUT, FAILED, SOLVED};

class SearchEngine {
    SearchStatus status;
    bool solution_found;
    Plan plan;
protected:
    // Hold a reference to the task implementation and pass it to objects that need it.
    const std::shared_ptr<AbstractTask> task;
    // Use task_proxy to access task information.
    TaskProxy task_proxy;
    std::string shared_memory_name;
    // std::vector<std::vector<std::string>> previous_states_names;
    StringVectorVector *previous_states_names;
    VecIntSet *previous_states_set;
    bip::managed_shared_memory* previous_states_segment;
    VoidAllocator* previous_states_alloc_inst;
    std::set<std::vector<int>> previous_state_values;
    std::vector<std::vector<FactProxy>> previous_states_facts;
    std::set<std::vector<int>> previous_states;
    bool no_cache = false;

    PlanManager plan_manager;
    StateRegistry state_registry;
    const successor_generator::SuccessorGenerator &successor_generator;
    SearchSpace search_space;
    SearchProgress search_progress;
    SearchStatistics statistics;
    int bound;
    OperatorCost cost_type;
    bool is_unit_cost;
    double max_time;
    const utils::Verbosity verbosity;

    virtual void initialize();
    virtual SearchStatus step() = 0;

    void set_plan(const Plan &plan);
    bool check_goal_and_set_plan(const State &state);
    int get_adjusted_cost(const OperatorProxy &op) const;
public:
    SearchEngine(const options::Options &opts);
    virtual ~SearchEngine();
    virtual void print_statistics() const = 0;
    virtual void save_plan_if_necessary();
    bool found_solution() const;
    SearchStatus get_status() const;
    const Plan &get_plan() const;
    void search();
    const SearchStatistics &get_statistics() const {return statistics;}
    void set_bound(int b) {bound = b;}
    int get_bound() {return bound;}
    PlanManager &get_plan_manager() {return plan_manager;}
    void set_shared_memory_name(std::string shared_memory_name) { this->shared_memory_name = shared_memory_name; }
    void set_no_cache(bool no_cache) { this->no_cache = no_cache; }

    /* The following three methods should become functions as they
       do not require access to private/protected class members. */
    static void add_pruning_option(options::OptionParser &parser);
    static void add_options_to_parser(options::OptionParser &parser);
    static void add_succ_order_options(options::OptionParser &parser);
};

/*
  Print evaluator values of all evaluators evaluated in the evaluation context.
*/
extern void print_initial_evaluator_values(const EvaluationContext &eval_context);

extern void collect_preferred_operators(
    EvaluationContext &eval_context, Evaluator *preferred_operator_evaluator,
    ordered_set::OrderedSet<OperatorID> &preferred_operators);

#endif
