#ifndef RESILIENT_STATE_H
#define RESILIENT_STATE_H

#include "operator.h"
#include "policy-repair/partial_state.h"

#include <set>

using namespace std;

typedef IntPacker::Bin PackedStateBin;

class ResilientState : public PartialState {
    friend class StateRegistry;
    int k;
    std::set<Operator> forbidden_op;

    ResilientState();

    ResilientState(const PartialState &partial_state_, int k_, std::set<Operator> forbidden_op_);

public:

    int get_k() const {
        return k;
    }

    std::set<Operator> get_forbidden_op() const {
        return forbidden_op;
    }
};

#endif
