#include "ckks_lazy_poly_evaluator.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fhe_smoke;

namespace {

struct CaseDef {
    std::string name;
    std::vector<double> coeffs;
    size_t expectedBlock0Terms = 0;
    size_t expectedBlock1Terms = 0;
    size_t expectedTailTerms = 0;
    size_t expectedEagerTensor = 0;
    size_t expectedLazyTensor = 0;
    size_t expectedEagerRelin = 0;
    size_t expectedLazyRelin = 0;
    size_t expectedEagerRescale = 0;
    size_t expectedLazyRescale = 0;
};

struct CaseResult {
    std::string mode;
    std::string name;
    std::string planSummary;
    double eagerErr = 0.0;
    double lazyErr = 0.0;
    size_t eagerTensor = 0;
    size_t lazyTensor = 0;
    size_t eagerRelin = 0;
    size_t lazyRelin = 0;
    size_t eagerRescale = 0;
    size_t lazyRescale = 0;
    bool pass = false;
};

constexpr double kMaxErrThreshold = 1e-8;

std::vector<CaseDef> BuildCases() {
    return {
        CaseDef{"only_c0", {0.25}, 0, 0, 1, 0, 0, 0, 0, 0, 0},
        CaseDef{"only_c1", {0.0, -0.3}, 0, 0, 1, 0, 0, 0, 0, 0, 0},
        CaseDef{"only_c5", {0.0, 0.0, 0.0, 0.0, 0.0, 0.2}, 0, 0, 1, 3, 3, 3, 3, 3, 3},
        CaseDef{"block0_only", {0.0, 0.0, 0.7, -1.2, 0.5}, 3, 0, 0, 5, 5, 5, 2, 5, 2},
        CaseDef{"block1_only", {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.4, 0.9, 1.1}, 0, 3, 0, 7, 7, 7, 4, 7, 4},
        CaseDef{"sparse_tail_block_mix", {0.0, -0.3, 0.7, 0.0, 0.0, 0.2, 0.0, 0.0, 1.1}, 1, 1, 2, 6, 6, 6, 6, 6, 6},
        CaseDef{"constant_x4_x7", {0.25, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.9}, 1, 1, 1, 6, 6, 6, 5, 6, 5},
        CaseDef{"dense_degree8", {0.25, -0.3, 0.7, -1.2, 0.5, 0.2, -0.4, 0.9, 1.1}, 3, 3, 3, 12, 12, 12, 6, 12, 6},
    };
}

void PrintSummaryHeader() {
    std::cout << std::left
              << std::setw(23) << "case"
              << std::setw(24) << "mode"
              << std::setw(38) << "plan"
              << std::setw(14) << "eager_err"
              << std::setw(14) << "lazy_err"
              << std::setw(12) << "tensor"
              << std::setw(12) << "relin"
              << std::setw(12) << "rescale"
              << std::setw(8) << "pass"
              << '\n';
    std::cout << std::string(155, '-') << '\n';
}

void PrintSummaryRow(const CaseResult& row) {
    std::cout << std::left
              << std::setw(23) << row.name
              << std::setw(24) << row.mode
              << std::setw(38) << row.planSummary
              << std::setw(14) << std::scientific << row.eagerErr
              << std::setw(14) << std::scientific << row.lazyErr
              << std::setw(12) << (std::to_string(row.eagerTensor) + "/" + std::to_string(row.lazyTensor))
              << std::setw(12) << (std::to_string(row.eagerRelin) + "/" + std::to_string(row.lazyRelin))
              << std::setw(12) << (std::to_string(row.eagerRescale) + "/" + std::to_string(row.lazyRescale))
              << std::setw(8) << (row.pass ? "yes" : "no")
              << '\n';
}

bool LazyNoWorseOnSwitches(const fhe_eval::PairedEvalResult& result) {
    return result.groupedLazy.stats.relinCount <= result.expandedEager.stats.relinCount &&
           result.groupedLazy.stats.rescaleCount <= result.expandedEager.stats.rescaleCount;
}

bool PlanSummaryMatches(const fhe_eval::Degree8PlanSummary& summary, const CaseDef& testCase) {
    return summary.block0Terms == testCase.expectedBlock0Terms &&
           summary.block1Terms == testCase.expectedBlock1Terms &&
           summary.tailTerms == testCase.expectedTailTerms;
}

bool StatsMatchExpected(const fhe_eval::PairedEvalResult& result, const CaseDef& testCase) {
    return result.expandedEager.stats.tensorProducts == testCase.expectedEagerTensor &&
           result.groupedLazy.stats.tensorProducts == testCase.expectedLazyTensor &&
           result.expandedEager.stats.relinCount == testCase.expectedEagerRelin &&
           result.groupedLazy.stats.relinCount == testCase.expectedLazyRelin &&
           result.expandedEager.stats.rescaleCount == testCase.expectedEagerRescale &&
           result.groupedLazy.stats.rescaleCount == testCase.expectedLazyRescale;
}

CaseResult RunCase(const CaseDef& testCase,
                   const std::string& modeName,
                   ScalingTechnique scalTech) {
    auto cc = BuildContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    const auto ct = cc->Encrypt(kp.publicKey, pt);

    fhe_eval::RestrictedDegree8Plan plan;
    plan.coeffs = testCase.coeffs;
    plan.plaintextInput = kInput;
    plan.slots = kSlots;

    const auto planSummary = fhe_eval::SummarizeRestrictedDegree8Plan(plan.coeffs);
    const auto result = fhe_eval::EvalRestrictedDegree8(cc, kp.secretKey, ct, plan);
    const auto ref = fhe_eval::EvalRestrictedDegree8Plain(kInput, plan.coeffs);
    const double eagerErr = MaxAbsErrReal(DecryptVec(cc, kp.secretKey, result.expandedEager.value, kSlots), ref);
    const double lazyErr = MaxAbsErrReal(DecryptVec(cc, kp.secretKey, result.groupedLazy.value, kSlots), ref);

    const bool pass = eagerErr < kMaxErrThreshold &&
                      lazyErr < kMaxErrThreshold &&
                      LazyNoWorseOnSwitches(result) &&
                      PlanSummaryMatches(planSummary, testCase) &&
                      StatsMatchExpected(result, testCase);

    return CaseResult{
        modeName,
        testCase.name,
        fhe_eval::FormatDegree8PlanSummary(planSummary),
        eagerErr,
        lazyErr,
        result.expandedEager.stats.tensorProducts,
        result.groupedLazy.stats.tensorProducts,
        result.expandedEager.stats.relinCount,
        result.groupedLazy.stats.relinCount,
        result.expandedEager.stats.rescaleCount,
        result.groupedLazy.stats.rescaleCount,
        pass
    };
}

void RunAll() {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] coefficient pattern regression\n";
    std::cout << "[CHECK] EvalRestrictedDegree8 across sparse/dense coefficient patterns\n";
    std::cout << "[PASS] eager/lazy err < " << std::scientific << kMaxErrThreshold
              << " and lazy relin/rescale <= eager\n\n";

    const auto cases = BuildCases();
    std::vector<CaseResult> rows;
    rows.reserve(cases.size() * 2);

    for (const auto& testCase : cases) {
        rows.push_back(RunCase(testCase, "FIXEDMANUAL", FIXEDMANUAL));
        rows.push_back(RunCase(testCase, "COMPOSITESCALINGMANUAL", COMPOSITESCALINGMANUAL));
    }

    PrintSummaryHeader();
    bool allPass = true;
    for (const auto& row : rows) {
        PrintSummaryRow(row);
        allPass = allPass && row.pass;
    }

    std::cout << "\nsummary:\n";
    std::cout << "  cases             = " << rows.size() << '\n';
    std::cout << "  all pass          = " << (allPass ? "yes" : "no") << '\n';

    if (!allPass) {
        throw std::runtime_error("coefficient pattern regression failed");
    }
}

}  // namespace

int main() {
    try {
        RunAll();
    }
    catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << '\n';
        return 1;
    }
    return 0;
}
