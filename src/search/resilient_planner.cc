#include "globals.h"
#include "operator.h"
#include "option_parser.h"
#include "ext/tree_util.hh"
#include "timer.h"
#include "utilities.h"
#include "search_engine.h"
#include "policy-repair/regression.h"
#include "policy-repair/policy.h"
#include "policy-repair/partial_state.h"
#include "resilient_node.h"
#include "search_engine.h"

#include <iostream>
#include <list>
#include <new>
#include <string>
#include <vector>
#include <stack>
#include <tr1/functional>
using namespace std;

bool verbose = false;

// probably better move this function in a separate class in future
bool resiliency_check(ResilientNode node);
bool replan(ResilientNode current_node, SearchEngine *engine);
void reset_goal();
void add_fault_model_deadend(ResilientNode node);
void print_results();
void print_timings();
bool find_in_nodes_list(std::list<ResilientNode> set, ResilientNode node);
bool find_in_op_set(std::set<Operator> set, Operator op);

std::list<ResilientNode> resilient_nodes;
std::list<ResilientNode> resilient_deadends;
std::stack<ResilientNode> nodes;

int main(int argc, const char **argv)
{
    register_event_handlers();

    if (argc < 2)
    {
        cout << OptionParser::usage(argv[0]) << endl;
        exit_with(EXIT_INPUT_ERROR);
    }

    if (string(argv[1]).compare("--help") != 0)
        read_everything(cin);

    SearchEngine *engine = 0;
    g_policy = 0;

    g_timer_regression.stop();
    g_timer_engine_init.stop();
    g_timer_search.stop();
    g_timer_policy_build.stop();
    g_timer_policy_use.stop();

    g_timer_regression.reset();
    g_timer_engine_init.reset();
    g_timer_search.reset();
    g_timer_policy_build.reset();
    g_timer_policy_use.reset();

    // the input will be parsed twice:
    // once in dry-run mode, to check for simple input errors,
    // then in normal mode
    g_timer_engine_init.resume();
    try
    {
        OptionParser::parse_cmd_line(argc, argv, true);
        engine = OptionParser::parse_cmd_line(argc, argv, false);
    }
    catch (ParseError &pe)
    {
        cerr << pe << endl;
        exit_with(EXIT_INPUT_ERROR);
    }
    g_timer_engine_init.stop();

    /* HAZ: Unfortunately, this must go here (as supposed to globals.cc)
     *      since we need to know if g_detect_deadends is true or not. */
    if (g_detect_deadends)
    {
        generate_regressable_ops();
    }
    /* HAZ: We create the policies even if we aren't using deadends, as
     *      they may be consulted by certain parts of the code. */
    g_deadend_policy = new Policy();
    g_deadend_states = new Policy();
    g_temporary_deadends = new Policy();

    /***************************************
     * Assert the settings are consistent. *
     ***************************************/
    if (((g_record_online_deadends || g_generalize_deadends) && !g_detect_deadends) ||
        ((g_partial_planlocal || g_plan_locally_limited) && !g_plan_locally) ||
        (g_optimized_scd && (g_jic_limit == 0)) ||
        (g_forgetpolicy && (g_jic_limit > 0)))
    {
        cout << "\n  Parameter Error: Make sure that the set of parameters is consistent.\n"
             << endl;
        exit(0);
    }

    /*********************
     * Handle JIC Limits *
     *********************/
    cout << "\nTotal allotted time (s): " << g_jic_limit << endl;

    // If we are going to do a final FSAP-free round, then we modify the
    //  time limits to give a 50/50 split between the JIC phase and final
    //  round phase
    double jic_ratio = 0.5;
    if (g_final_fsap_free_round)
        g_jic_limit *= jic_ratio;

    cout << "Max time for core JIC (remaining used in final-round repairs): " << g_jic_limit << endl;

    // Adjust the g_jic_limit so the epochs are handled properly
    int epochs_remaining = g_num_epochs;
    double single_jic_limit = g_jic_limit / (double)g_num_epochs;
    g_jic_limit = single_jic_limit;

    cout << "Max time for each of the " << epochs_remaining << " epochs: " << g_jic_limit << endl
         << endl;

    /************************
     * Initial policy search *
     *************************/
    engine->reset();
    g_timer_search.resume();
    engine->search();
    g_timer_search.stop();
    engine->save_plan_if_necessary();
    engine->statistics();
    engine->heuristic_statistics();
    cout << "Initial search time: " << g_timer_search << endl;
    cout << "Initial total time: " << g_timer << endl;

    if (!engine->found_solution())
    {
        cout << "No solution -- aborting repairs." << endl;
        exit(1);
    }

    cout << "\nRegressing the plan..." << endl;
    list<PolicyItem *> regression_steps = perform_regression(engine->get_plan(), g_matched_policy, 0, true);

    cout << "\nGenerating an initial policy..." << endl;
    g_policy = new Policy();
    g_policy->update_policy(regression_steps);

    // save the first policy as the original policy
    g_original_policy = new Policy();
    g_original_policy->update_policy(regression_steps);

    if (g_sample_for_depth1_deadends)
        sample_for_depth1_deadends(engine->get_plan(), new PartialState(g_initial_state()));

    /***********************
     * Resilient Alghoritm *
     ***********************/

    // first fill of the nodes stack with the initial policy
    State current = g_initial_state();
    std::vector<const Operator *> plan = engine->get_plan();
    ResilientNode initial_node = ResilientNode(current, g_max_faults);
    g_current_faults = g_max_faults;

    for (vector<const Operator *>::iterator it = plan.begin(); it != plan.end(); ++it)
    {
        ResilientNode res_node = ResilientNode(current, g_max_faults);

        std::set<Operator> post_actions;
        post_actions.insert(*(*it));
        ResilientNode res_node_f = ResilientNode(current, g_max_faults - 1, post_actions);

        if (verbose)
        {
            cout << "\nPushing nodes of the initial policy:" << endl;
            res_node.dump();
            res_node_f.dump();
        }

        nodes.push(res_node);
        nodes.push(res_node_f);

        current = g_state_registry->get_successor_state(current, *(*it));
    }

    cout << "\nInitial policy:" << endl;
    g_policy->dump();
    int iteration = 1;

    while (!nodes.empty())
    {
        ResilientNode current_node = nodes.top();
        nodes.pop();
        g_current_faults = current_node.get_k();
        g_current_forbidden_ops = current_node.get_deactivated_op();

        k_v_pair current_pair = std::make_pair(g_current_faults, g_current_forbidden_ops);

        if (verbose)
        {
            cout << "\n----------------------------------------" << endl;
            cout << "\nIteration:" << iteration << endl;
            cout << "Current node:" << endl;
            current_node.dump();
        }

        if (!resiliency_check(current_node))
        {
            if (verbose)
                cout << "\nFailed resiliency check.\n"
                     << endl;
            if (!replan(current_node, engine))
            {
                if (verbose)
                    cout << "\nFailed replanning." << endl;
                add_fault_model_deadend(current_node);

                if (!find_in_nodes_list(resilient_deadends, current_node))
                {
                    if (verbose)
                        cout << "Adding nodes to deadend set.\n"
                             << endl;
                    resilient_deadends.push_back(current_node);
                }
                else
                {
                    if (verbose)
                        cout << "Node already in deadend set.\n"
                             << endl;
                }
            }
            else
            {
                if (verbose)
                    cout << "Successfull replanning" << endl;

                State current = g_initial_state();
                std::vector<const Operator *> plan = engine->get_plan();

                if (current_node.get_k() >= 1)
                {
                    if (verbose)
                        cout << "\nPushing nodes:\n"
                             << endl;

                    for (vector<const Operator *>::iterator it = plan.begin(); it != plan.end(); ++it)
                    {
                        ResilientNode res_node = ResilientNode(current, g_current_faults, g_current_forbidden_ops);

                        std::set<Operator> post_actions = g_current_forbidden_ops;
                        post_actions.insert(*(*it));
                        ResilientNode res_node_f = ResilientNode(current, g_current_faults - 1, post_actions);

                        if (verbose)
                        {
                            res_node.dump();
                            res_node_f.dump();
                            cout << "\n"
                                 << endl;
                        }

                        nodes.push(res_node);
                        nodes.push(res_node_f);
                        current = g_state_registry->get_successor_state(current, *(*it));
                    }
                }
                else
                {
                    for (vector<const Operator *>::iterator it = plan.begin(); it != plan.end(); ++it)
                    {
                        cout << "Pushing to R" << endl;

                        ResilientNode res_node = ResilientNode(current, 0, current_node.get_deactivated_op());
                        res_node.dump();
                        resilient_nodes.push_back(res_node);
                        current = g_state_registry->get_successor_state(current, *(*it));
                    }
                }

                regression_steps.clear();
                regression_steps = perform_regression(engine->get_plan(), g_matched_policy, 0, true);

                Policy *resilient_policy = new Policy();
                resilient_policy->update_policy(regression_steps);
                g_resilient_policies.insert(std::make_pair(std::make_pair(g_current_faults, g_current_forbidden_ops), resilient_policy));

                g_policy->update_policy(regression_steps);
                cout << "\n Updated policy:" << endl;
                g_policy->dump();
            }
        }
        else
        {
            if (verbose)
                cout << "\nSuccessfull resiliency check.\n"
                     << endl;
        }

        //if (iteration > 100)
        //    break;

        iteration++;
    }

    if (find_in_nodes_list(resilient_deadends, initial_node))
        cout << "\nInitial state is a deadend, problem is not " << g_max_faults << "-resilient!\n"
             << endl;
    else
        print_results();

    g_timer.stop();
    print_timings();
}

/*********************
 *       RCheck      *
 *********************/
bool resiliency_check(ResilientNode node)
{
    if (resilient_nodes.empty())
    {
        return false;
    }

    PartialState state_to_check = PartialState(node.get_state());

    if (find_in_nodes_list(resilient_nodes, node))
        return true;

    std::set<Operator> next_actions;
    next_actions.clear();
    list<PolicyItem *> current_policy = g_policy->get_items();
    PolicyItem *goal_step = NULL;

    // getPolicyActions(P,s)
    for (std::list<PolicyItem *>::iterator it = current_policy.begin(); it != current_policy.end(); ++it)
    {
        RegressionStep *reg_step = dynamic_cast<RegressionStep *>(*it);
        if (!reg_step->is_goal)
        {
            PartialState policy_state = PartialState(*reg_step->state);

            if ((*reg_step->state).implies(state_to_check) && !find_in_op_set(node.get_deactivated_op(), reg_step->get_op()))
            {   
                next_actions.insert(reg_step->get_op());
            }
        }
        else if (reg_step->is_goal)
        {
            goal_step = reg_step;
        }
    }

    State state = node.get_state();
    StateRegistry *registry = const_cast<StateRegistry *>(&state.get_registry());

    for (std::set<Operator>::iterator it_o = next_actions.begin(); it_o != next_actions.end(); ++it_o)
    {
        State successor = registry->get_successor_state(node.get_state(), *it_o);
        PartialState successor_p = PartialState(successor);
        ResilientNode successor_r = ResilientNode(successor, node.get_k(), node.get_deactivated_op()); // <s[a], k, V>

        std::set<Operator> forbidden_plus_current = node.get_deactivated_op();
        forbidden_plus_current.insert(*it_o);
        ResilientNode current_r = ResilientNode(node.get_state(), node.get_k() - 1, forbidden_plus_current); // <s, k-1, V U {a}>

        if ((find_in_nodes_list(resilient_nodes, successor_r) || (*goal_step->state).implies(successor_p)) && (find_in_nodes_list(resilient_nodes, current_r)))
        {
            resilient_nodes.push_back(node);
            return true;
        }
    }
    return false;
}

/*********************
 *    computePlan    *
 *********************/
bool replan(ResilientNode current_node, SearchEngine *engine)
{
    bool verbose = true;

    if (verbose)
    {
        cout << "Replanning... " << endl;
    }

    PartialState current_state = PartialState(current_node.get_state());

    if (is_deadend(current_state))
    {
        if (verbose)
            cout << "\nDetected deadend" << endl;
        return false;
    }

    if (verbose)
        cout << "\nCreating initial state." << endl;

    g_state_registry->reset_initial_state();
    for (int i = 0; i < g_variable_name.size(); i++)
        g_initial_state_data[i] = current_state[i];

    if (verbose)
        cout << "Creating new engine." << endl;

    reset_goal();
    g_timer_engine_init.resume();
    engine->reset();
    g_timer_engine_init.stop();

    if (verbose)
        cout << "Searching for a solution." << endl;

    g_timer_search.resume();
    engine->search();
    g_timer_search.stop();

    if (engine->found_solution())
    {
        cout << "Solution found" << endl;
        if (verbose)
        {
            engine->save_plan_if_necessary();
            engine->statistics();
            engine->heuristic_statistics();
        }

        cout << "Plan: " << endl;
        cout << engine->get_plan() << endl;

        reset_goal();
        return true;
    }
    else
    {
        if (verbose)
            cout << "Replanning failed!" << endl;

        reset_goal();
        return false;
    }
}

// regress the state and add every state-action pair Regr(s,A) to the fault model policy map indexed by the current (k,V)
void add_fault_model_deadend(ResilientNode node)
{
    State state = node.get_state();
    list<PolicyItem *> de_items;

    PartialState *dummy_state = new PartialState();

    PartialState *de_state = new PartialState(state);
    generalize_deadend(*de_state);

    vector<PolicyItem *> reg_items;
    g_regressable_ops->generate_applicable_items(*de_state, reg_items, true, g_regress_only_relevant_deadends);

    for (int j = 0; j < reg_items.size(); j++)
    {
        RegressableOperator *ro = (RegressableOperator *)(reg_items[j]);
        de_items.push_back(new NondetDeadend(new PartialState(*de_state, *(ro->op), false, dummy_state),
                                             ro->op->nondet_index));
    }

    delete dummy_state;
    Policy *current_deadend_policy = new Policy();

    current_deadend_policy->update_policy(de_items);
    g_fault_model.insert(std::make_pair(std::make_pair(g_current_faults, g_current_forbidden_ops), current_deadend_policy));

    std::set<Operator> v = node.get_deactivated_op();
    for (std::set<Operator>::iterator it = v.begin(); it != v.end(); ++it)
    {
        std::set<Operator> forbidden_minus_a = g_current_forbidden_ops;
        forbidden_minus_a.erase(*it);

        Policy *s_a = new Policy();
        list<PolicyItem *> s_a_item;

        s_a_item.push_back(new NondetDeadend(new PartialState(state), it->nondet_index));

        if (g_fault_model.find(std::make_pair(g_current_faults + 1, forbidden_minus_a)) != g_fault_model.end())
        {
            g_fault_model.find(std::make_pair(g_current_faults + 1, forbidden_minus_a))->second->update_policy(s_a_item);
        }
        else
        {
            s_a->update_policy(s_a_item);
            g_fault_model.insert(std::make_pair(std::make_pair(g_current_faults + 1, forbidden_minus_a), s_a));
        }
    }
}

void reset_goal()
{
    g_goal.clear();
    for (int i = 0; i < g_goal_orig.size(); i++)
        g_goal.push_back(make_pair(g_goal_orig[i].first, g_goal_orig[i].second));
}

bool find_in_nodes_list(std::list<ResilientNode> res_set, ResilientNode node)
{
    if (res_set.size() == 0)
    {
        return false;
    }
    else
    {
        bool found;
        for (std::list<ResilientNode>::iterator it = res_set.begin(); it != res_set.end(); ++it)
        {

            found = true;
            for (int i = 0; i < g_variable_domain.size(); i++)
            {
                const string &fact_name1 = g_fact_names[i][(it->get_state())[i]];
                const string &fact_name2 = g_fact_names[i][(node.get_state())[i]];
                if (fact_name1 != "<none of those>" && fact_name2 != "<none of those>" && fact_name1.compare(fact_name2) != 0)
                {
                    found = false;
                }
            }

            if (it->get_k() != node.get_k())
            {
                found = false;
            }

            if (it->get_deactivated_op().size() != node.get_deactivated_op().size())
            {
                found = false;
            }

            std::set<Operator> prova = it->get_deactivated_op();
            for (std::set<Operator>::iterator it_o = prova.begin(); it_o != prova.end(); ++it_o)
            {
                bool equal_op = false;
                if (find_in_op_set(node.get_deactivated_op(), *it_o))
                    equal_op = true;
                if (!equal_op)
                {
                    found = false;
                }
            }
            if (found)
            {
                return true;
            }
        }
        return false;
    }
}

bool find_in_op_set(std::set<Operator> op_set, Operator op)
{
    if (op_set.empty())
        return false;

    for (std::set<Operator>::iterator it = op_set.begin(); it != op_set.end(); ++it)
    {
        if (it->get_nondet_name() == op.get_nondet_name())
        {
            return true;
        }
    }
    return false;
}

void print_results()
{
    cout << "\n\n--------------------------------------------------------------------" << endl;
    cout << "\n                  -{ Base policy without faults}-\n"
         << endl;
    cout << "--------------------------------------------------------------------\n"
         << endl;

    g_original_policy->dump();

    cout << "--------------------------------------------------------------------" << endl;
    cout << "\n                  -{ Alternative policies }-\n"
         << endl;
    cout << "--------------------------------------------------------------------" << endl;

    int i = 1;
    for (std::map<k_v_pair, Policy *>::iterator it = g_resilient_policies.begin(); it != g_resilient_policies.end(); ++it)
    {
        cout << "\n-{ Policy number " << i << " }-\n"
             << endl;
        cout << "Remaining faults : " << it->first.first << endl;
        cout << "Failed operator : " << endl;
        for (std::set<Operator>::iterator it_o = it->first.second.begin(); it_o != it->first.second.end(); ++it_o)
            cout << it_o->get_nondet_name() << endl;
        cout << endl;
        it->second->dump();
        i++;
        cout << "--------------------------------------------------------------" << endl;
    }
}

void print_timings()
{
    cout << "\n                  -{ Timing Statistics }-\n"
         << endl;
    cout << "        Regression Computation: " << g_timer_regression << endl;
    cout << "         Engine Initialization: " << g_timer_engine_init << endl;
    cout << "                   Search Time: " << g_timer_search << endl;
    cout << "           Policy Construction: " << g_timer_policy_build << endl;
    cout << "              Using the policy: " << g_timer_policy_use << endl;
    cout << "                    Total time: " << g_timer << endl;
    cout << "\n--------------------------------------------------------------\n"
         << endl;
}
