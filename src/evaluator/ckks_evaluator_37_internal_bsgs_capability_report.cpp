#include "internal_bsgs_common.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fhe_eval::internal_bsgs;

namespace {

constexpr double kCoeffTol = 1e-14;

std::vector<size_t> ExpectedSupportedPowers() {
    return {
        5, 6, 7,
        9, 10, 11,
        13, 14, 15,
        21, 22, 23,
        25, 26, 27,
        29, 30, 31,
    };
}

std::vector<size_t> DerivedSupportedPowers(size_t maxPower) {
    std::vector<size_t> out;
    for (size_t power = 0; power <= maxPower; ++power) {
        InnerTerm term;
        size_t outerPower = 0;
        if (TryDecomposePower(power, term, outerPower)) {
            out.push_back(power);
        }
    }
    return out;
}

CoeffPattern CoeffPatternFromList(const std::string& name, const std::vector<double>& coeffs) {
    if (coeffs.size() > 32) {
        throw std::runtime_error("CoeffPatternFromList: this prototype only accepts degree <= 31");
    }

    CoeffPattern pattern{name, {}};
    for (size_t power = 0; power < coeffs.size(); ++power) {
        const double coeff = coeffs[power];
        if (std::abs(coeff) <= kCoeffTol) {
            continue;
        }
        InnerTerm ignoredTerm;
        size_t ignoredOuter = 0;
        if (!TryDecomposePower(power, ignoredTerm, ignoredOuter)) {
            throw std::runtime_error("CoeffPatternFromList: unsupported nonzero coefficient at x^" +
                                     std::to_string(power));
        }
        pattern.coeffsByPower[power] = coeff;
    }
    if (pattern.coeffsByPower.empty()) {
        throw std::runtime_error("CoeffPatternFromList: no supported nonzero coefficients");
    }
    return pattern;
}

void PrintPowerList(const std::string& label, const std::vector<size_t>& powers) {
    std::cout << label;
    for (size_t i = 0; i < powers.size(); ++i) {
        if (i > 0) {
            std::cout << ", ";
        }
        std::cout << "x^" << powers[i];
    }
    std::cout << '\n';
}

void CheckSupportedPowerTable() {
    const auto expected = ExpectedSupportedPowers();
    const auto derived = DerivedSupportedPowers(31);
    PrintPowerList("[SUPPORTED_POWERS] ", derived);
    if (derived != expected) {
        throw std::runtime_error("CheckSupportedPowerTable: derived support table changed");
    }
    std::cout << "[PASS] supported power table matches fixed t=2,B=4 decomposition\n";
}

void PrintDecompositionTable() {
    std::cout << "\n[DECOMPOSITION_TABLE]\n";
    std::cout << "power,outer,low,high\n";
    for (const size_t power : ExpectedSupportedPowers()) {
        InnerTerm term;
        size_t outerPower = 0;
        if (!TryDecomposePower(power, term, outerPower)) {
            throw std::runtime_error("PrintDecompositionTable: expected supported power failed");
        }
        std::cout << power << ',' << outerPower << ',' << term.lowPower << ',' << term.highPower << '\n';
    }
}

void ExpectRejectedPower(size_t power) {
    CoeffPattern pattern{
        "unsupported_x_" + std::to_string(power),
        {{power, 1.0}},
    };
    try {
        (void)GenerateInternalPlanFromCoeffs(pattern);
    }
    catch (const std::exception& e) {
        std::cout << "[PASS] rejected nonzero x^" << power << ": " << e.what() << '\n';
        return;
    }
    throw std::runtime_error("ExpectRejectedPower: x^" + std::to_string(power) + " was unexpectedly accepted");
}

void CheckUnsupportedRejections() {
    std::cout << "\n[UNSUPPORTED_NONZERO_REJECTIONS]\n";
    for (const size_t power : {0, 1, 2, 3, 4, 8, 12, 16, 17, 20, 24, 28, 32}) {
        ExpectRejectedPower(power);
    }
}

void CheckZeroSkipping() {
    std::cout << "\n[ZERO_SKIPPING]\n";
    std::vector<double> coeffs(32, 0.0);
    coeffs[0] = 0.0;
    coeffs[1] = 0.0;
    coeffs[4] = 0.0;
    coeffs[5] = 0.125;
    coeffs[16] = 0.0;
    coeffs[31] = -0.0625;

    const auto pattern = CoeffPatternFromList("zero_unsupported_powers_are_ignored", coeffs);
    const auto plan = GenerateInternalPlanFromCoeffs(pattern);
    ValidateInternalPlan(plan);
    ValidateGeneratedPlanMatchesCoeffs(plan, pattern);

    std::cout << "[PASS] zero coefficients at unsupported powers were ignored\n";
    std::cout << "  active coeff count = " << pattern.coeffsByPower.size() << '\n';
    for (const auto& [power, coeff] : pattern.coeffsByPower) {
        std::cout << "  c[" << std::setw(2) << power << "] = " << std::scientific << coeff << '\n';
    }
}

void CheckUnsupportedNonzeroInCoeffList() {
    std::cout << "\n[COEFF_LIST_REJECTION]\n";
    std::vector<double> coeffs(32, 0.0);
    coeffs[5] = 0.10;
    coeffs[20] = 0.25;

    try {
        (void)CoeffPatternFromList("unsupported_nonzero_x20", coeffs);
    }
    catch (const std::exception& e) {
        std::cout << "[PASS] coefficient list rejected unsupported nonzero x^20: "
                  << e.what() << '\n';
        return;
    }
    throw std::runtime_error("CheckUnsupportedNonzeroInCoeffList: x^20 was unexpectedly accepted");
}

}  // namespace

int main() {
    try {
        std::cout << "[EVALUATOR] fixed t=2,B=4 Internal BSGS capability report\n";
        std::cout << "[BOUNDARY] this target checks planner capability only; it does not run CKKS\n";
        CheckSupportedPowerTable();
        PrintDecompositionTable();
        CheckUnsupportedRejections();
        CheckZeroSkipping();
        CheckUnsupportedNonzeroInCoeffList();
    }
    catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << '\n';
        return 1;
    }
    return 0;
}
