#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct T3Term {
    size_t a = 0;
    size_t b = 0;
    size_t c = 0;
    double coeff = 0.0;
};

struct BlockSpec {
    std::string name;
    size_t outerMultiplierPower = 0;
    std::vector<T3Term> terms;
};

struct OuterAssemblyPlan {
    std::string name;
    std::vector<size_t> componentPowers;
    std::vector<BlockSpec> blocks;
};

enum class PlanId {
    OuterAssembly24,
    OuterPattern25,
};

struct CoeffPattern {
    std::string name;
    std::map<size_t, double> coeffsByPower;
};

struct DecomposedTerm {
    size_t finalPower = 0;
    size_t innerPower = 0;
    size_t outerPower = 0;
    T3Term term;
    double coeff = 0.0;
};

OuterAssemblyPlan MakeOuterAssembly24Plan() {
    return OuterAssemblyPlan{
        "outer_assembly_24_shape",
        {1, 2, 4, 8},
        {
            BlockSpec{
                "block0",
                0,
                {
                    T3Term{1, 2, 4, 0.15},
                    T3Term{2, 2, 4, -0.25},
                    T3Term{1, 4, 4, 0.35},
                    T3Term{1, 2, 8, -0.20},
                },
            },
            BlockSpec{
                "block1",
                8,
                {
                    T3Term{1, 2, 4, -0.18},
                    T3Term{2, 2, 4, 0.22},
                    T3Term{1, 4, 4, -0.12},
                },
            },
        },
    };
}

OuterAssemblyPlan MakeOuterPattern25Plan() {
    return OuterAssemblyPlan{
        "outer_pattern_25_shape",
        {1, 2, 4, 8},
        {
            BlockSpec{
                "block0",
                0,
                {
                    T3Term{1, 1, 8, -0.11},
                    T3Term{2, 4, 8, 0.28},
                    T3Term{4, 4, 8, -0.19},
                },
            },
            BlockSpec{
                "block1",
                8,
                {
                    T3Term{1, 2, 8, 0.17},
                    T3Term{2, 4, 8, -0.13},
                    T3Term{4, 8, 8, 0.09},
                },
            },
        },
    };
}

OuterAssemblyPlan MakeWhitelistedPlan(PlanId id) {
    switch (id) {
        case PlanId::OuterAssembly24:
            return MakeOuterAssembly24Plan();
        case PlanId::OuterPattern25:
            return MakeOuterPattern25Plan();
    }
    throw std::runtime_error("MakeWhitelistedPlan: unknown plan id");
}

CoeffPattern MakeOuterAssembly24CoeffPattern() {
    return CoeffPattern{
        "coeff_pattern_outer_assembly_24",
        {
            {7, 0.15},
            {8, -0.25},
            {9, 0.35},
            {11, -0.20},
            {15, -0.18},
            {16, 0.22},
            {17, -0.12},
        },
    };
}

CoeffPattern MakeOuterPattern25CoeffPattern() {
    return CoeffPattern{
        "coeff_pattern_outer_pattern_25",
        {
            {10, -0.11},
            {14, 0.28},
            {16, -0.19},
            {19, 0.17},
            {22, -0.13},
            {28, 0.09},
        },
    };
}

std::vector<CoeffPattern> KnownCoeffPatterns() {
    return {
        MakeOuterAssembly24CoeffPattern(),
        MakeOuterPattern25CoeffPattern(),
    };
}

size_t TermInnerPower(const T3Term& term) {
    return term.a + term.b + term.c;
}

bool HasPower(const std::vector<size_t>& powers, size_t power) {
    return std::find(powers.begin(), powers.end(), power) != powers.end();
}

bool SameCoeffPattern(const CoeffPattern& lhs, const CoeffPattern& rhs) {
    constexpr double kTol = 1e-12;
    if (lhs.coeffsByPower.size() != rhs.coeffsByPower.size()) {
        return false;
    }
    for (const auto& [power, coeff] : lhs.coeffsByPower) {
        const auto it = rhs.coeffsByPower.find(power);
        if (it == rhs.coeffsByPower.end()) {
            return false;
        }
        if (std::abs(coeff - it->second) > kTol) {
            return false;
        }
    }
    return true;
}

PlanId SelectWhitelistedPlanForCoeffs(const CoeffPattern& pattern) {
    if (SameCoeffPattern(pattern, MakeOuterAssembly24CoeffPattern())) {
        return PlanId::OuterAssembly24;
    }
    if (SameCoeffPattern(pattern, MakeOuterPattern25CoeffPattern())) {
        return PlanId::OuterPattern25;
    }
    throw std::runtime_error("SelectWhitelistedPlanForCoeffs: unsupported coefficient pattern");
}

std::string PowerLabel(size_t power) {
    if (power == 0) {
        return "1";
    }
    if (power == 1) {
        return "x";
    }
    return "x^" + std::to_string(power);
}

void ValidatePlanShape(const OuterAssemblyPlan& plan) {
    if (plan.blocks.size() != 2) {
        throw std::runtime_error("ValidatePlanShape: this smoke expects exactly two blocks");
    }
    if (plan.blocks[0].outerMultiplierPower != 0) {
        throw std::runtime_error("ValidatePlanShape: first block must be unshifted");
    }
    for (const auto& block : plan.blocks) {
        if (block.terms.empty()) {
            throw std::runtime_error("ValidatePlanShape: empty block");
        }
        if (block.outerMultiplierPower != 0 && !HasPower(plan.componentPowers, block.outerMultiplierPower)) {
            throw std::runtime_error("ValidatePlanShape: outer multiplier is missing from components");
        }
        for (const auto& term : block.terms) {
            if (!HasPower(plan.componentPowers, term.a) ||
                !HasPower(plan.componentPowers, term.b) ||
                !HasPower(plan.componentPowers, term.c)) {
                throw std::runtime_error("ValidatePlanShape: term uses a missing component");
            }
        }
    }
}

std::vector<DecomposedTerm> DecomposePlanTerms(const OuterAssemblyPlan& plan) {
    ValidatePlanShape(plan);
    std::vector<DecomposedTerm> out;
    for (const auto& block : plan.blocks) {
        for (const auto& term : block.terms) {
            const size_t innerPower = TermInnerPower(term);
            out.push_back(DecomposedTerm{
                innerPower + block.outerMultiplierPower,
                innerPower,
                block.outerMultiplierPower,
                term,
                term.coeff,
            });
        }
    }
    std::sort(out.begin(), out.end(), [](const DecomposedTerm& lhs, const DecomposedTerm& rhs) {
        return lhs.finalPower < rhs.finalPower;
    });
    return out;
}

void ValidateDecompositionAgainstCoeffs(const CoeffPattern& pattern, const OuterAssemblyPlan& plan) {
    const auto terms = DecomposePlanTerms(plan);
    std::map<size_t, double> plannedCoeffs;
    for (const auto& item : terms) {
        plannedCoeffs[item.finalPower] += item.coeff;
    }
    if (plannedCoeffs.size() != pattern.coeffsByPower.size()) {
        throw std::runtime_error("ValidateDecompositionAgainstCoeffs: coefficient count mismatch");
    }

    constexpr double kTol = 1e-12;
    for (const auto& [power, expectedCoeff] : pattern.coeffsByPower) {
        const auto it = plannedCoeffs.find(power);
        if (it == plannedCoeffs.end()) {
            throw std::runtime_error("ValidateDecompositionAgainstCoeffs: missing final power x^" +
                                     std::to_string(power));
        }
        if (std::abs(expectedCoeff - it->second) > kTol) {
            throw std::runtime_error("ValidateDecompositionAgainstCoeffs: coeff mismatch at x^" +
                                     std::to_string(power));
        }
    }
}

void PrintDecomposition(const CoeffPattern& pattern) {
    const auto planId = SelectWhitelistedPlanForCoeffs(pattern);
    const auto plan = MakeWhitelistedPlan(planId);
    ValidateDecompositionAgainstCoeffs(pattern, plan);
    const auto terms = DecomposePlanTerms(plan);

    std::cout << "\n============================================================\n";
    std::cout << "[DECOMP] " << pattern.name << '\n';
    std::cout << "[PLAN]   " << plan.name << '\n';
    std::cout << "components:";
    for (const auto power : plan.componentPowers) {
        std::cout << ' ' << PowerLabel(power);
    }
    std::cout << '\n';

    for (const auto& item : terms) {
        std::cout << "  final=" << std::setw(4) << PowerLabel(item.finalPower)
                  << " inner=(" << PowerLabel(item.term.a) << ","
                  << PowerLabel(item.term.b) << ","
                  << PowerLabel(item.term.c) << ")"
                  << " inner_power=" << std::setw(4) << PowerLabel(item.innerPower)
                  << " outer=" << std::setw(4) << PowerLabel(item.outerPower)
                  << " coeff=" << std::showpos << std::fixed << std::setprecision(3)
                  << item.coeff << std::noshowpos << '\n';
    }

    std::cout << "[PASS] coefficient powers match inner t=3 factors plus optional outer x^8\n";
}

}  // namespace

int main() {
    try {
        std::cout << "[EVALUATOR] asymmetric exponent decomposition smoke\n";
        std::cout << "[PROJECT DECISION] verify known coefficient patterns against explicit t=3 decompositions\n";
        for (const auto& pattern : KnownCoeffPatterns()) {
            PrintDecomposition(pattern);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << '\n';
        return 1;
    }
    return 0;
}
