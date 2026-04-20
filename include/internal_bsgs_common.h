#ifndef FHE_EVALUATOR_INTERNAL_BSGS_COMMON_H
#define FHE_EVALUATOR_INTERNAL_BSGS_COMMON_H

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace fhe_eval::internal_bsgs {

struct InnerTerm {
    size_t lowPower = 0;
    size_t highPower = 0;
    double coeff = 0.0;
};

struct OuterBlock {
    std::string name;
    size_t outerPower = 0;
    std::vector<InnerTerm> terms;
};

struct InternalBsgsPlan {
    std::string name;
    size_t base = 4;
    size_t t = 2;
    std::vector<size_t> lowPowers;
    std::vector<size_t> highPowers;
    size_t outerPower = 16;
    std::vector<OuterBlock> blocks;
};

struct CoeffPattern {
    std::string name;
    std::map<size_t, double> coeffsByPower;
};

inline InternalBsgsPlan MakeTinyInternalBsgsPlan() {
    return InternalBsgsPlan{
        "tiny_internal_bsgs_t2_b4",
        4,
        2,
        {1, 2, 3},
        {4, 8, 12},
        16,
        {
            OuterBlock{
                "outer0",
                0,
                {
                    InnerTerm{1, 4, 0.12},
                    InnerTerm{2, 4, 0.05},
                    InnerTerm{2, 8, -0.18},
                    InnerTerm{3, 8, 0.04},
                    InnerTerm{3, 12, 0.09},
                },
            },
            OuterBlock{
                "outer1",
                16,
                {
                    InnerTerm{2, 4, -0.07},
                    InnerTerm{3, 4, 0.03},
                    InnerTerm{1, 8, 0.11},
                    InnerTerm{3, 12, -0.05},
                },
            },
        },
    };
}

inline InternalBsgsPlan MakeTinyInternalBsgsPlanAlt() {
    return InternalBsgsPlan{
        "tiny_internal_bsgs_t2_b4_alt",
        4,
        2,
        {1, 2, 3},
        {4, 8, 12},
        16,
        {
            OuterBlock{
                "outer0",
                0,
                {
                    InnerTerm{1, 4, -0.08},
                    InnerTerm{3, 4, 0.06},
                    InnerTerm{1, 8, 0.14},
                    InnerTerm{2, 8, -0.03},
                    InnerTerm{2, 12, 0.07},
                },
            },
            OuterBlock{
                "outer1",
                16,
                {
                    InnerTerm{1, 4, 0.05},
                    InnerTerm{3, 4, -0.04},
                    InnerTerm{2, 8, -0.09},
                    InnerTerm{1, 12, 0.025},
                    InnerTerm{3, 12, 0.035},
                },
            },
        },
    };
}

inline std::vector<InternalBsgsPlan> KnownInternalPlans() {
    return {
        MakeTinyInternalBsgsPlan(),
        MakeTinyInternalBsgsPlanAlt(),
    };
}

inline CoeffPattern MakeInternalPatternA() {
    return CoeffPattern{
        "coeff_pattern_internal_t2_b4_a",
        {
            {5, 0.12},
            {6, 0.05},
            {10, -0.18},
            {11, 0.04},
            {15, 0.09},
            {22, -0.07},
            {23, 0.03},
            {25, 0.11},
            {31, -0.05},
        },
    };
}

inline CoeffPattern MakeInternalPatternB() {
    return CoeffPattern{
        "coeff_pattern_internal_t2_b4_b",
        {
            {5, -0.08},
            {7, 0.06},
            {9, 0.14},
            {10, -0.03},
            {14, 0.07},
            {21, 0.05},
            {23, -0.04},
            {26, -0.09},
            {29, 0.025},
            {31, 0.035},
        },
    };
}

inline std::vector<CoeffPattern> KnownCoeffPatterns() {
    return {
        MakeInternalPatternA(),
        MakeInternalPatternB(),
    };
}

inline std::vector<size_t> OuterCandidates() {
    return {0, 16};
}

inline std::vector<size_t> HighCandidates() {
    return {12, 8, 4};
}

inline std::vector<size_t> LowCandidates() {
    return {1, 2, 3};
}

inline bool HasPower(const std::vector<size_t>& powers, size_t power) {
    return std::find(powers.begin(), powers.end(), power) != powers.end();
}

inline void ValidateInternalPlan(const InternalBsgsPlan& plan) {
    if (plan.base != 4 || plan.t != 2) {
        throw std::runtime_error("ValidateInternalPlan: this prototype is fixed to t=2, B=4");
    }
    if (plan.blocks.size() != 2) {
        throw std::runtime_error("ValidateInternalPlan: this prototype requires exactly two outer blocks");
    }
    if (plan.blocks[0].outerPower != 0 || plan.blocks[1].outerPower != plan.outerPower) {
        throw std::runtime_error("ValidateInternalPlan: expected outer powers 0 and x^16");
    }
    for (const auto& block : plan.blocks) {
        if (block.terms.empty()) {
            throw std::runtime_error("ValidateInternalPlan: empty block");
        }
        for (const auto& term : block.terms) {
            if (!HasPower(plan.lowPowers, term.lowPower)) {
                throw std::runtime_error("ValidateInternalPlan: term low power is outside bar(S1)");
            }
            if (!HasPower(plan.highPowers, term.highPower)) {
                throw std::runtime_error("ValidateInternalPlan: term high power is outside hat(S1)");
            }
        }
    }
}

inline bool TryDecomposePower(size_t finalPower, InnerTerm& term, size_t& outerPower) {
    const auto lowCandidates = LowCandidates();
    for (const size_t outer : OuterCandidates()) {
        if (finalPower <= outer) {
            continue;
        }
        const size_t residual = finalPower - outer;
        for (const size_t high : HighCandidates()) {
            if (residual <= high) {
                continue;
            }
            const size_t low = residual - high;
            if (std::find(lowCandidates.begin(), lowCandidates.end(), low) != lowCandidates.end()) {
                term.lowPower = low;
                term.highPower = high;
                outerPower = outer;
                return true;
            }
        }
    }
    return false;
}

inline InternalBsgsPlan GenerateInternalPlanFromCoeffs(const CoeffPattern& pattern) {
    InternalBsgsPlan plan{
        "generated_" + pattern.name,
        4,
        2,
        {1, 2, 3},
        {4, 8, 12},
        16,
        {
            OuterBlock{"outer0", 0, {}},
            OuterBlock{"outer1", 16, {}},
        },
    };

    for (const auto& [power, coeff] : pattern.coeffsByPower) {
        InnerTerm term;
        size_t outerPower = 0;
        if (!TryDecomposePower(power, term, outerPower)) {
            throw std::runtime_error("GenerateInternalPlanFromCoeffs: unsupported power x^" +
                                     std::to_string(power));
        }
        term.coeff = coeff;
        if (outerPower == 0) {
            plan.blocks[0].terms.push_back(term);
        }
        else if (outerPower == 16) {
            plan.blocks[1].terms.push_back(term);
        }
        else {
            throw std::runtime_error("GenerateInternalPlanFromCoeffs: unsupported outer power");
        }
    }
    return plan;
}

inline std::map<size_t, double> PlanCoeffMap(const InternalBsgsPlan& plan) {
    std::map<size_t, double> out;
    for (const auto& block : plan.blocks) {
        for (const auto& term : block.terms) {
            out[block.outerPower + term.lowPower + term.highPower] += term.coeff;
        }
    }
    return out;
}

inline void ValidateGeneratedPlanMatchesCoeffs(const InternalBsgsPlan& plan,
                                               const CoeffPattern& pattern) {
    constexpr double kTol = 1e-12;
    const auto planned = PlanCoeffMap(plan);
    if (planned.size() != pattern.coeffsByPower.size()) {
        throw std::runtime_error("ValidateGeneratedPlanMatchesCoeffs: coefficient count mismatch");
    }
    for (const auto& [power, coeff] : pattern.coeffsByPower) {
        const auto it = planned.find(power);
        if (it == planned.end()) {
            throw std::runtime_error("ValidateGeneratedPlanMatchesCoeffs: missing x^" +
                                     std::to_string(power));
        }
        if (std::abs(coeff - it->second) > kTol) {
            throw std::runtime_error("ValidateGeneratedPlanMatchesCoeffs: coefficient mismatch at x^" +
                                     std::to_string(power));
        }
    }
}

inline InternalBsgsPlan ReferencePlanForPattern(const CoeffPattern& pattern) {
    if (pattern.name == "coeff_pattern_internal_t2_b4_a") {
        return MakeTinyInternalBsgsPlan();
    }
    if (pattern.name == "coeff_pattern_internal_t2_b4_b") {
        return MakeTinyInternalBsgsPlanAlt();
    }
    throw std::runtime_error("ReferencePlanForPattern: unsupported coefficient pattern");
}

}  // namespace fhe_eval::internal_bsgs

#endif  // FHE_EVALUATOR_INTERNAL_BSGS_COMMON_H
