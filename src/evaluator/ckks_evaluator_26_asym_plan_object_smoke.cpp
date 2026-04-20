#include "smoke_common.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fhe_smoke;

namespace {

struct T3TermPlan {
    size_t a = 0;
    size_t b = 0;
    size_t c = 0;
    double coeff = 0.0;
};

struct BlockPlan {
    std::string name;
    size_t outerMultiplierPower = 0;
    std::vector<T3TermPlan> terms;
};

struct OuterAssemblyPlan {
    std::string name;
    std::vector<size_t> componentPowers;
    std::vector<BlockPlan> blocks;
};

size_t TermInnerPower(const T3TermPlan& term) {
    return term.a + term.b + term.c;
}

double PlainPower(double x, size_t power) {
    double y = 1.0;
    for (size_t i = 0; i < power; ++i) {
        y *= x;
    }
    return y;
}

bool HasPower(const std::vector<size_t>& powers, size_t power) {
    return std::find(powers.begin(), powers.end(), power) != powers.end();
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

std::map<size_t, double> CoeffMap(const OuterAssemblyPlan& plan) {
    std::map<size_t, double> coeffs;
    for (const auto& block : plan.blocks) {
        for (const auto& term : block.terms) {
            coeffs[TermInnerPower(term) + block.outerMultiplierPower] += term.coeff;
        }
    }
    return coeffs;
}

std::vector<double> EvalPlain(const OuterAssemblyPlan& plan) {
    const auto coeffs = CoeffMap(plan);
    std::vector<double> values(kInput.size(), 0.0);
    for (size_t i = 0; i < kInput.size(); ++i) {
        for (const auto& [power, coeff] : coeffs) {
            values[i] += coeff * PlainPower(kInput[i], power);
        }
    }
    return values;
}

void ValidatePlan(const OuterAssemblyPlan& plan) {
    if (plan.componentPowers.empty()) {
        throw std::runtime_error(plan.name + ": componentPowers is empty");
    }
    if (plan.blocks.empty()) {
        throw std::runtime_error(plan.name + ": blocks is empty");
    }

    std::set<size_t> seen(plan.componentPowers.begin(), plan.componentPowers.end());
    if (seen.size() != plan.componentPowers.size()) {
        throw std::runtime_error(plan.name + ": duplicate component power");
    }

    for (const auto power : plan.componentPowers) {
        if (power == 0) {
            throw std::runtime_error(plan.name + ": component power 0 is not a ciphertext component");
        }
    }

    for (const auto& block : plan.blocks) {
        if (block.terms.empty()) {
            throw std::runtime_error(plan.name + "/" + block.name + ": terms is empty");
        }
        if (block.outerMultiplierPower != 0 && !HasPower(plan.componentPowers, block.outerMultiplierPower)) {
            throw std::runtime_error(plan.name + "/" + block.name + ": outer multiplier is not in components");
        }
        for (const auto& term : block.terms) {
            if (!HasPower(plan.componentPowers, term.a) ||
                !HasPower(plan.componentPowers, term.b) ||
                !HasPower(plan.componentPowers, term.c)) {
                throw std::runtime_error(plan.name + "/" + block.name + ": term uses a missing component");
            }
            if (term.coeff == 0.0) {
                throw std::runtime_error(plan.name + "/" + block.name + ": zero coeff term should be skipped");
            }
        }
    }
}

void PrintPlanSummary(const OuterAssemblyPlan& plan) {
    ValidatePlan(plan);
    const auto coeffs = CoeffMap(plan);
    const auto values = EvalPlain(plan);

    double maxAbsPlain = 0.0;
    for (const auto value : values) {
        maxAbsPlain = std::max(maxAbsPlain, std::abs(value));
    }

    std::cout << "\n============================================================\n";
    std::cout << "[PLAN] " << plan.name << '\n';
    std::cout << "components:";
    for (const auto power : plan.componentPowers) {
        std::cout << ' ' << PowerLabel(power);
    }
    std::cout << '\n';

    for (const auto& block : plan.blocks) {
        std::cout << "block " << block.name
                  << " outer=" << PowerLabel(block.outerMultiplierPower)
                  << " terms=" << block.terms.size() << '\n';
        for (const auto& term : block.terms) {
            const size_t innerPower = TermInnerPower(term);
            const size_t finalPower = innerPower + block.outerMultiplierPower;
            std::cout << "  coeff=" << std::setw(8) << std::fixed << std::setprecision(3) << term.coeff
                      << " factors=(" << PowerLabel(term.a) << ","
                      << PowerLabel(term.b) << ","
                      << PowerLabel(term.c) << ")"
                      << " inner=" << PowerLabel(innerPower)
                      << " final=" << PowerLabel(finalPower) << '\n';
        }
    }

    std::cout << "final polynomial:";
    for (const auto& [power, coeff] : coeffs) {
        std::cout << ' ' << std::showpos << std::fixed << std::setprecision(3)
                  << coeff << "*" << PowerLabel(power) << std::noshowpos;
    }
    std::cout << '\n';
    std::cout << "max_abs_plain_on_kInput = " << std::scientific << maxAbsPlain << '\n';
    std::cout << "[PASS] plan object validated\n";
}

OuterAssemblyPlan MakeOuterAssembly24Plan() {
    return OuterAssemblyPlan{
        "outer_assembly_24_shape",
        {1, 2, 4, 8},
        {
            BlockPlan{
                "block0",
                0,
                {
                    T3TermPlan{1, 2, 4, 0.15},
                    T3TermPlan{2, 2, 4, -0.25},
                    T3TermPlan{1, 4, 4, 0.35},
                    T3TermPlan{1, 2, 8, -0.20},
                },
            },
            BlockPlan{
                "block1",
                8,
                {
                    T3TermPlan{1, 2, 4, -0.18},
                    T3TermPlan{2, 2, 4, 0.22},
                    T3TermPlan{1, 4, 4, -0.12},
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
            BlockPlan{
                "block0",
                0,
                {
                    T3TermPlan{1, 1, 8, -0.11},
                    T3TermPlan{2, 4, 8, 0.28},
                    T3TermPlan{4, 4, 8, -0.19},
                },
            },
            BlockPlan{
                "block1",
                8,
                {
                    T3TermPlan{1, 2, 8, 0.17},
                    T3TermPlan{2, 4, 8, -0.13},
                    T3TermPlan{4, 8, 8, 0.09},
                },
            },
        },
    };
}

}  // namespace

int main() {
    try {
        std::cout << "[EVALUATOR] asymmetric plan object smoke\n";
        std::cout << "[PROJECT DECISION] validate fixed plan data before wiring a planner-driven executor\n";
        PrintPlanSummary(MakeOuterAssembly24Plan());
        PrintPlanSummary(MakeOuterPattern25Plan());
    }
    catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << '\n';
        return 1;
    }
    return 0;
}
