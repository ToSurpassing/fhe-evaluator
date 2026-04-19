#include "ckks_lazy_poly_evaluator.h"

#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace fhe_smoke;

namespace {

void PrintTraceHeader() {
    std::cout << std::left
              << std::setw(17) << "strategy"
              << std::setw(40) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(12) << "ctElems"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';
    std::cout << std::string(127, '-') << '\n';
}

void PrintTraceRow(const fhe_eval::TraceRow& row) {
    std::cout << std::left
              << std::setw(17) << row.strategy
              << std::setw(40) << row.step
              << std::setw(10) << row.level
              << std::setw(16) << row.noiseScaleDeg
              << std::setw(12) << row.elements
              << std::setw(16) << std::scientific << row.timeSec
              << std::setw(16) << std::scientific << row.maxAbsErr
              << '\n';
}

void PrintStats(const std::string& label, const fhe_eval::EvalStats& stats) {
    std::cout << "  " << label << " tensor products    = " << stats.tensorProducts << '\n';
    std::cout << "  " << label << " relin count        = " << stats.relinCount << '\n';
    std::cout << "  " << label << " rescale count      = " << stats.rescaleCount << '\n';
    std::cout << "  " << label << " scalar mult count  = " << stats.scalarMultCount << '\n';
    std::cout << "  " << label << " level-align count  = " << stats.levelAlignCount << '\n';
    std::cout << "  " << label << " add count          = " << stats.addCount << '\n';
    std::cout << "  " << label << " total operator sec = " << std::scientific << stats.totalOpSec << '\n';
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] parameterized coefficient block prototype\n";
    std::cout << "[POLY] P(x)=0.7*x^2 -1.2*x^3 +0.5*x^4 -0.4*x^6 +0.9*x^7 +1.1*x^8\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    fhe_eval::RestrictedDegree8Plan plan;
    plan.coeffs = {
        0.0, 0.0, 0.7, -1.2, 0.5, 0.0, -0.4, 0.9, 1.1
    };
    plan.slots = kSlots;

    const auto result = fhe_eval::EvalRestrictedDegree8(cc, kp.secretKey, ct, plan);
    const auto ref = fhe_eval::EvalRestrictedDegree8Plain(kInput, plan.coeffs);
    const auto eagerErr = MaxAbsErrReal(DecryptVec(cc, kp.secretKey, result.expandedEager.value, kSlots), ref);
    const auto lazyErr = MaxAbsErrReal(DecryptVec(cc, kp.secretKey, result.groupedLazy.value, kSlots), ref);

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    std::cout << '\n';
    PrintTraceHeader();
    for (const auto& row : result.expandedEager.trace) {
        PrintTraceRow(row);
    }
    for (const auto& row : result.groupedLazy.trace) {
        PrintTraceRow(row);
    }

    std::cout << "\nsummary:\n";
    PrintStats("expanded-eager", result.expandedEager.stats);
    PrintStats("grouped-lazy  ", result.groupedLazy.stats);
    std::cout << "  expanded-eager final max_abs_err = " << std::scientific << eagerErr << '\n';
    std::cout << "  grouped-lazy   final max_abs_err = " << std::scientific << lazyErr << '\n';

    std::cout << "\ncomparison:\n";
    std::cout << "  tensor products           = "
              << result.expandedEager.stats.tensorProducts
              << " vs " << result.groupedLazy.stats.tensorProducts << '\n';
    std::cout << "  relin count               = "
              << result.expandedEager.stats.relinCount
              << " vs " << result.groupedLazy.stats.relinCount << '\n';
    std::cout << "  rescale count             = "
              << result.expandedEager.stats.rescaleCount
              << " vs " << result.groupedLazy.stats.rescaleCount << '\n';
    std::cout << "  note                      = first reusable restricted-degree evaluator prototype\n";
}

}  // namespace

int main() {
    try {
        RunOneMode("FIXEDMANUAL", FIXEDMANUAL);
        RunOneMode("COMPOSITESCALINGMANUAL", COMPOSITESCALINGMANUAL);
    }
    catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << '\n';
        return 1;
    }
    return 0;
}
