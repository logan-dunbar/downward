#include "search_engine.h"

#include "evaluation_context.h"
#include "evaluator.h"
#include "option_parser.h"
#include "plugin.h"

#include "algorithms/ordered_set.h"
#include "task_utils/successor_generator.h"
#include "task_utils/task_properties.h"
#include "tasks/root_task.h"
#include "utils/countdown_timer.h"
#include "utils/logging.h"
#include "utils/rng_options.h"
#include "utils/system.h"
#include "utils/timer.h"

#include <cassert>
#include <iostream>
#include <limits>
#include <vector>
#include <iterator>

using namespace std;
using utils::ExitCode;

namespace bip = boost::interprocess;

class PruningMethod;

successor_generator::SuccessorGenerator &get_successor_generator(const TaskProxy &task_proxy) {
    utils::g_log << "Building successor generator..." << flush;
    int peak_memory_before = utils::get_peak_memory_in_kb();
    utils::Timer successor_generator_timer;
    successor_generator::SuccessorGenerator &successor_generator =
        successor_generator::g_successor_generators[task_proxy];
    successor_generator_timer.stop();
    utils::g_log << "done!" << endl;
    int peak_memory_after = utils::get_peak_memory_in_kb();
    int memory_diff = peak_memory_after - peak_memory_before;
    utils::g_log << "peak memory difference for successor generator creation: "
                 << memory_diff << " KB" << endl
                 << "time for successor generation creation: "
                 << successor_generator_timer << endl;
    return successor_generator;
}

SearchEngine::SearchEngine(const Options &opts)
    : status(IN_PROGRESS),
      solution_found(false),
      task(tasks::g_root_task),
      task_proxy(*task),
      state_registry(task_proxy),
      successor_generator(get_successor_generator(task_proxy)),
      search_space(state_registry),
      search_progress(opts.get<utils::Verbosity>("verbosity")),
      statistics(opts.get<utils::Verbosity>("verbosity")),
      cost_type(opts.get<OperatorCost>("cost_type")),
      is_unit_cost(task_properties::is_unit_cost(task_proxy)),
      max_time(opts.get<double>("max_time")),
      verbosity(opts.get<utils::Verbosity>("verbosity")) {
    if (opts.get<int>("bound") < 0) {
        cerr << "error: negative cost bound " << opts.get<int>("bound") << endl;
        utils::exit_with(ExitCode::SEARCH_INPUT_ERROR);
    }
    bound = opts.get<int>("bound");
    task_properties::print_variable_statistics(task_proxy);
}

SearchEngine::~SearchEngine() {
    delete previous_states_segment;
    delete previous_states_alloc_inst;
}

bool SearchEngine::found_solution() const {
    return solution_found;
}

SearchStatus SearchEngine::get_status() const {
    return status;
}

const Plan &SearchEngine::get_plan() const {
    assert(solution_found);
    return plan;
}

void SearchEngine::set_plan(const Plan &p) {
    solution_found = true;
    plan = p;
}

void SearchEngine::initialize() {
    // auto rng = std::default_random_engine {};

    // initial_state.unpack();
    // std::vector<std::string> init_strs;
    // std::vector<int> init_ints(initial_state.get_unpacked_values());

    // for (size_t i = 0; i < initial_state.size(); i++) {
    //     std::string fact_name_std = initial_state[i].get_name();
    //     init_strs.push_back(fact_name_std);
    // }

    // std::shuffle(std::begin(init_strs), std::end(init_strs), rng);

    // std::vector<int> test_ints;
    // VariablesProxy variables = task_proxy.get_variables();
    // for (size_t i = 0; i < variables.size(); ++i) {
    //     for (int j = 0; j < variables[i].get_domain_size(); ++j) {
    //         for (auto state_str : init_strs) {
    //             if (state_str == variables[i].get_fact(j).get_name()) {
    //                 test_ints.push_back(variables[i].get_fact(j).get_value());
    //                 goto test;
    //             }
    //         }
    //     }
    //     test:;
    // }

    // if (test_ints == init_ints) {
    //     utils::g_log << "It worked!!!" << " ";
    // }

    // auto init_state = task_proxy.get_initial_state();
    // auto registry = init_state.get_registry();
    // auto registered_states = registry->size();

    if (!shared_memory_name.empty())
    {
        // shared_memory_name = "PreviousStatesMemory";
        previous_states_segment = new bip::managed_shared_memory(bip::open_only, shared_memory_name.c_str());
        previous_states_alloc_inst = new VoidAllocator(previous_states_segment->get_segment_manager());

        previous_states_names = previous_states_segment->find<StringVectorVector>("PreviousStates").first;
        previous_states_set = previous_states_segment->find<VecIntSet>("PreviousStatesSet").first;

        // vector<int> default_vec;
        // auto vars = task_proxy.get_variables();
        // for (size_t i = 0; i < vars.size(); ++i)
        // {
        //     auto var = vars[i];
        //     default_vec.push_back(FactPair::no_fact.value);
        //     std::cout << var.get_name() << std::endl;
        //     for (size_t j = 0; j < var.get_domain_size(); ++j)
        //     {
        //         auto fact = var.get_fact(j);
        //         if (fact.get_name().rfind("NegatedAtom", 0) == 0) {
        //             default_vec[i] = fact.get_value();
        //         }
        //         std::cout << fact.get_name() << ", ";
        //     }
        //     std::cout << std::endl;
        // }
        
        // auto facts = task_proxy.get_variables().get_facts();
        // for (auto fact : facts) {
        //     auto fact_name = fact.get_name();
        //     std::cout << fact_name << std::endl;
        // }

        // for (auto state_vec : *previous_states_names) {
        //     for (auto fact : state_vec) {
        //         std::string fact_str(fact.begin(), fact.end());
        //         std::cout << fact_str << ", ";
        //     }
        //     std::cout << std::endl;
        // }

        // auto vars = task_proxy.get_variables();
        // for (auto var : vars) {
        //     std::cout << var.get_name() << " : " << std::to_string(var.get_id()) << std::endl;
        // }

        // for (auto state_vec : *previous_states_set) {
        //     for (auto fact_val : state_vec) {
        //         std::cout << std::to_string(fact_val) << ", ";
        //     }
        //     std::cout << std::endl;
        // }

        auto prev_sz = previous_states_set->size();

        // size_t num_variables = task_proxy.get_variables().size();
        // FactsProxy facts = task_proxy.get_variables().get_facts();
        // VariablesProxy vars = task_proxy.get_variables();
        for (StringVector state : *previous_states_names) {

            VecInt state_vec(*previous_states_alloc_inst);
            // state_vec.assign(default_vec.begin(), default_vec.end());
            // for (size_t i = 0; i < num_variables; ++i) {
            //     auto var = vars[i];
                
            //     state_vec.push_back(FactPair::no_fact.value);
            // }
            for (String val : state) {
                state_vec.push_back(stoi(std::string(val.c_str())));
            }

            // for (String fact_name_str : state) {
            //     std::string fact_name = "Atom " + std::string(fact_name_str.begin(), fact_name_str.end());
            //     for (FactProxy fact : facts) {
            //         if (fact.get_name() == fact_name) {
            //         // if (fact.get_name().compare(fact.get_name().size() - fact_name.size(), fact_name.size(), fact_name) == 0) {
            //             auto var = fact.get_variable();
            //             // std::cout << var.get_name() << " : " << std::to_string(var.get_id()) << std::endl;
            //             state_vec[var.get_id()] = fact.get_value();
            //             break;
            //         }
            //     }
            // }
            // // std::cout << "End state" << std::endl;
            // for (auto fact_val : state_vec) {
            //     std::cout << std::to_string(fact_val) << ", ";
            // }
            // std::cout << std::endl;

            previous_states_set->insert(state_vec);
        }

        auto after_sz = previous_states_set->size();

        // for (auto state_vec : *previous_states_set) {
        //     for (auto fact_val : state_vec) {
        //         std::cout << std::to_string(fact_val) << ", ";
        //     }
        //     std::cout << std::endl;
        // }

        previous_states_names->clear();

        // auto variables = task_proxy.get_variables();
        // for (auto state : *previous_states_names) {
        //     std::vector<int> state_values;
        //     for (size_t i = 0; i < variables.size(); i++) {
        //         auto variable = variables[i];
        //         int state_value = FactPair::no_fact.value;
        //         for (int j = 0; j < variable.get_domain_size(); j++) {
        //             auto fact = variable.get_fact(j);
        //             for (auto fact_name_str : state) {
        //                 std::string fact_name(fact_name_str.begin(), fact_name_str.end());
        //                 if (fact.get_name() == fact_name) {
        //                     state_value = fact.get_value();
        //                     goto found_state_value;
        //                 }
        //             }
        //         }
        //         found_state_value:;
        //         state_values.push_back(state_value);
        //     }
        //     // auto state_with_values = task_proxy.create_state(std::move(state_values));
        //     previous_states.insert(state_values);
        // }

        // for (auto state : previous_states) {
        //     if (state == init_state) {
        //         std::cout << "OMG it worked..." << std::endl;
        //     }
        // }

        // TODO: this is the working fact version
        // auto facts = task_proxy.get_variables().get_facts();
        // for (auto state : *previous_states_names) {
        //     vector<FactProxy> previous_state_facts;
        //     for (auto state_str : state) {
        //         std::string state_str_std(state_str.begin(), state_str.end());
        //         auto factIt = std::find_if(facts.begin(), facts.end(),
        //             [&state_str_std](const FactProxy x) { return x.get_name() == state_str_std;});
        //         // for (auto fact : facts) {
        //         //     if (state_str_std == fact.get_name()) {
        //         //         previous_state_facts.push_back(fact);
        //         //         goto next_state_str;
        //         //     }
        //         // }
        //         // next_state_str:;
        //         if (factIt != facts.end()) {
        //             previous_state_facts.push_back(*factIt);
        //         }
        //     }
        //     previous_states_facts.push_back(previous_state_facts);
        // }

        // VariablesProxy variables = task_proxy.get_variables();
        // for (auto state : *previous_states_names) {
        //     std::vector<int> values;
        //     for (size_t i = 0; i < variables.size(); ++i) {
        //         for (int j = 0; j < variables[i].get_domain_size(); ++j) {
        //             for (auto state_str : state) {
        //                 std::string state_str_std(state_str.begin(), state_str.end());
        //                 std::string fact_name(variables[i].get_fact(j).get_name());
        //                 if (state_str_std == fact_name) {
        //                     values.push_back(variables[i].get_fact(j).get_value());
        //                     goto next_variable;
        //                 }
        //             }
        //         }
        //         // no fact for variable
        //         values.push_back(-1);
        //         next_variable:;
        //     }

        //     // for (auto state_str : state) {
        //     //     std::string state_str_std(state_str.begin(), state_str.end());
        //     //     // utils::g_log << state_str_std << " ";
        //     //     for (FactProxy fact : task_proxy.get_variables().get_facts()) {
        //     //         if (state_str_std == fact.get_name()) {
        //     //             values.push_back(fact.get_value());
        //     //             goto next_name;
        //     //         }
        //     //     }
        //     //     next_name:;
        //     // }
        //     previous_state_values.insert(std::move(values));
        // }

        // initial_state.unpack();
        // if (previous_state_values.find(initial_state.get_unpacked_values()) != state_values.end()){
        //     utils::g_log << "States are equal!!" << endl;
        // }
    }



    // utils::g_log << "Facts:" << endl;
    // for (FactProxy fact : task_proxy.get_variables().get_facts())
    //     utils::g_log << fact.get_name() << endl;

    // utils::g_log << "Variables:" << endl;
    // VariablesProxy variables = task_proxy.get_variables();
    // for (size_t i = 0; i < variables.size(); ++i) {
    //     for (int j = 0; j < variables[i].get_domain_size(); ++j)
    //         utils::g_log << variables[i].get_fact(j).get_name() << " ";
    //     utils::g_log << endl;
    // }
}

void SearchEngine::search() {
    initialize();
    utils::CountdownTimer timer(max_time);
    while (status == IN_PROGRESS) {
        status = step();
        if (timer.is_expired()) {
            utils::g_log << "Time limit reached. Abort search." << endl;
            status = TIMEOUT;
            break;
        }
    }
    // TODO: Revise when and which search times are logged.
    utils::g_log << "Actual search time: " << timer.get_elapsed_time() << endl;
}

bool SearchEngine::check_goal_and_set_plan(const State &state) {
    // utils::g_log << "Logan's Test!" << endl;
    bool normal_goal_test = task_properties::is_goal_state(task_proxy, state);
    bool logan_goal_test = false;
    if (!shared_memory_name.empty()) {
        // state.unpack();
        // logan_goal_test = previous_state_values.find(state.get_unpacked_values()) != previous_state_values.end();

        // state.unpack();
        // logan_goal_test = previous_states.find(state.get_unpacked_values()) != previous_states.end();

        // logan_goal_test = false;
        // state.unpack();
        // for(auto prev_state : previous_states) {
        //     if (prev_state.get_unpacked_values() == state.get_unpacked_values()) {
        //         logan_goal_test = true;
        //         break;
        //     }
        // }

        // for (auto state_facts : previous_states_facts) {
        //     logan_goal_test = true;
        //     for (auto state_fact : state_facts) {
        //         if (state[state_fact.get_variable()] != state_fact) {
        //             logan_goal_test = false;
        //             break;
        //         }
        //     }
        //     if (logan_goal_test)
        //         break;
        // }

        state.unpack();
        auto vals = state.get_unpacked_values();
        VecInt state_vec(*previous_states_alloc_inst);
        state_vec.assign(vals.begin(), vals.end());
        logan_goal_test = previous_states_set->find(state_vec) != previous_states_set->end();
    }

    if (normal_goal_test || logan_goal_test) {
        if (logan_goal_test) {
            utils::g_log << "Logan found!" << endl;
        }
        utils::g_log << "Solution found!" << endl;
        Plan plan;
        search_space.trace_path(state, plan);
        set_plan(plan);

        if (!shared_memory_name.empty() && !no_cache) {
            std::vector<StateID> state_path_ids;
            search_space.trace_path_state(state, state_path_ids);

            // for (auto state_path_id : state_path_ids) {
            //     StringVector state_str(*previous_states_alloc_inst);
            //     State state_path = state_registry.lookup_state(state_path_id);
            //     auto vals = state_path.get_unpacked_values();
            //     for (size_t i = 0; i < state_path.size(); i++) {
            //         std::string fact_name_str = state_path[i].get_name();
            //         if (fact_name_str != "<none of those>") {
            //             String fact_name(*previous_states_alloc_inst);
            //             fact_name = fact_name_str.c_str();
            //             state_str.push_back(fact_name);
            //         }
            //     }
            //     previous_states_names->push_back(state_str);
            // }

            for (auto state_path_id : state_path_ids) {
                VecInt state_vec(*previous_states_alloc_inst);
                State state_path = state_registry.lookup_state(state_path_id);
                state_path.unpack();
                auto vals = state_path.get_unpacked_values();
                state_vec.assign(vals.begin(), vals.end());
                // for (size_t i = 0; i < state_path.size(); i++) {
                //     std::string fact_name_str = state_path[i].get_name();
                //     if (fact_name_str != "<none of those>") {
                //         String fact_name(*previous_states_alloc_inst);
                //         fact_name = fact_name_str.c_str();
                //         state_str.push_back(fact_name);
                //     }
                // }
                previous_states_set->insert(state_vec);
            }


            
            // OperatorsProxy operators = task_proxy.get_operators();
            // for (OperatorID op_id : get_plan()) {
            //     StringVector state(*previous_states_alloc_inst);
            //     for (size_t i = 0; i < current_state.size(); i++) {
            //         std::string fact_name_std = current_state[i].get_name();
            //         // if (fact_name_std != "<none of those>") {
            //             String fact_name(*previous_states_alloc_inst);
            //             fact_name = fact_name_std.c_str();
            //             state.push_back(fact_name);
            //         // }
            //     }
            //     previous_states_names->push_back(state);
            //     // utils::g_log << "State: " << boost::algorithm::join(state_names, ", ") << endl;
            //     // utils::g_log << "Operator: " << operators[op_id].get_name() << endl;
                
            //     current_state = current_state.get_unregistered_successor(operators[op_id]);
            // }
            utils::g_log << "Cache size: " << previous_states_set->size() << endl;

            // for (auto state : *previous_states_names) {
            //     for (auto state_str : state) {
            //         std::string state_str_std(state_str.begin(), state_str.end());
            //         std::cout << state_str_std << " ";
            //     }
            //     std::cout << std::endl;
            // }
        }

        return true;
    }
    return false;
}

void SearchEngine::save_plan_if_necessary() {
    if (found_solution()) {
        plan_manager.save_plan(get_plan(), task_proxy);
    }
}

int SearchEngine::get_adjusted_cost(const OperatorProxy &op) const {
    return get_adjusted_action_cost(op, cost_type, is_unit_cost);
}

/* TODO: merge this into add_options_to_parser when all search
         engines support pruning.

   Method doesn't belong here because it's only useful for certain derived classes.
   TODO: Figure out where it belongs and move it there. */
void SearchEngine::add_pruning_option(OptionParser &parser) {
    parser.add_option<shared_ptr<PruningMethod>>(
        "pruning",
        "Pruning methods can prune or reorder the set of applicable operators in "
        "each state and thereby influence the number and order of successor states "
        "that are considered.",
        "null()");
}

void SearchEngine::add_options_to_parser(OptionParser &parser) {
    ::add_cost_type_option_to_parser(parser);
    parser.add_option<int>(
        "bound",
        "exclusive depth bound on g-values. Cutoffs are always performed according to "
        "the real cost, regardless of the cost_type parameter", "infinity");
    parser.add_option<double>(
        "max_time",
        "maximum time in seconds the search is allowed to run for. The "
        "timeout is only checked after each complete search step "
        "(usually a node expansion), so the actual runtime can be arbitrarily "
        "longer. Therefore, this parameter should not be used for time-limiting "
        "experiments. Timed-out searches are treated as failed searches, "
        "just like incomplete search algorithms that exhaust their search space.",
        "infinity");
    utils::add_verbosity_option_to_parser(parser);
}

/* Method doesn't belong here because it's only useful for certain derived classes.
   TODO: Figure out where it belongs and move it there. */
void SearchEngine::add_succ_order_options(OptionParser &parser) {
    vector<string> options;
    parser.add_option<bool>(
        "randomize_successors",
        "randomize the order in which successors are generated",
        "false");
    parser.add_option<bool>(
        "preferred_successors_first",
        "consider preferred operators first",
        "false");
    parser.document_note(
        "Successor ordering",
        "When using randomize_successors=true and "
        "preferred_successors_first=true, randomization happens before "
        "preferred operators are moved to the front.");
    utils::add_rng_options(parser);
}

void print_initial_evaluator_values(const EvaluationContext &eval_context) {
    eval_context.get_cache().for_each_evaluator_result(
        [] (const Evaluator *eval, const EvaluationResult &result) {
            if (eval->is_used_for_reporting_minima()) {
                eval->report_value_for_initial_state(result);
            }
        }
        );
}

static PluginTypePlugin<SearchEngine> _type_plugin(
    "SearchEngine",
    // TODO: Replace empty string by synopsis for the wiki page.
    "");

void collect_preferred_operators(
    EvaluationContext &eval_context,
    Evaluator *preferred_operator_evaluator,
    ordered_set::OrderedSet<OperatorID> &preferred_operators) {
    if (!eval_context.is_evaluator_value_infinite(preferred_operator_evaluator)) {
        for (OperatorID op_id : eval_context.get_preferred_operators(preferred_operator_evaluator)) {
            preferred_operators.insert(op_id);
        }
    }
}
