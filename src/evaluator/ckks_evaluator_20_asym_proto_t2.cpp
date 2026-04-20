#include "smoke_common.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fhe_smoke;
using Clock = std::chrono::high_resolution_clock;

namespace {

struct TraceRow {
    std::string strategy;
    std::string step;
    size_t level = 0;
    size_t noiseScaleDeg = 0;
    size_t elements = 0;
    double timeSec = 0.0;
    double maxAbsErr = 0.0;
};

struct EvalStats {
    size_t tensorProducts = 0;
    size_t relinCount = 0;
    size_t rescaleCount = 0;
    size_t scalarMultCount = 0;
    size_t levelAlignCount = 0;
    size_t addCount = 0;
    double totalOpSec = 0.0;
};

struct PowerCt {
    size_t power = 0;
    Ciphertext<DCRTPoly> ct;
};

struct DecompTerm {
    size_t a = 0;
    size_t b = 0;
    double coeff = 0.0;
};

struct EvalOutput {
    Ciphertext<DCRTPoly> value;
    EvalStats stats;
    std::vector<TraceRow> trace;
};

// Teaching-sized asymmetric decomposition:
//   B=4, t=2
//   S1^(0) = {x, x^2, x^3}
//   S1^(1) = {x^4, x^8, x^12}
//   x^(a+4b) = x^a * x^(4b)
//
// This deliberately omits pure tail terms so the first prototype focuses on
// set-decomposition products and grouped lazy materialization.
const std::vector<DecompTerm> kTerms = {
    DecompTerm{1, 1, 0.15},   // x^5
    DecompTerm{2, 1, -0.25},  // x^6
    DecompTerm{3, 1, 0.35},   // x^7
    DecompTerm{1, 2, 0.45},   // x^9
    DecompTerm{2, 2, -0.55},  // x^10
    DecompTerm{3, 2, 0.65},   // x^11
    DecompTerm{1, 3, -0.20},  // x^13
    DecompTerm{2, 3, 0.30},   // x^14
    DecompTerm{3, 3, -0.40},  // x^15
};

size_t TermPower(const DecompTerm& term) {
    return term.a + 4 * term.b;
}

double PlainPower(double x, size_t power) {
    double y = 1.0;
    for (size_t i = 0; i < power; ++i) {
        y *= x;
    }
    return y;
}

std::vector<double> RefPower(size_t power) {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(PlainPower(x, power));
    }
    return ref;
}

std::vector<double> RefScaledPower(size_t power, double scalar) {
    auto ref = RefPower(power);
    for (double& v : ref) {
        v *= scalar;
    }
    return ref;
}

std::vector<double> RefPolynomial() {
    std::vector<double> ref(kInput.size(), 0.0);
    for (const auto& term : kTerms) {
        const size_t power = TermPower(term);
        for (size_t i = 0; i < kInput.size(); ++i) {
            ref[i] += term.coeff * PlainPower(kInput[i], power);
        }
    }
    return ref;
}

TraceRow Capture(const std::string& strategy,
                 const std::string& step,
                 CC cc,
                 const PrivateKey<DCRTPoly>& sk,
                 const Ciphertext<DCRTPoly>& ct,
                 const std::vector<double>& ref,
                 double timeSec) {
    const auto dec = DecryptVec(cc, sk, ct, kSlots);
    return TraceRow{
        strategy,
        step,
        ct->GetLevel(),
        ct->GetNoiseScaleDeg(),
        ct->NumberCiphertextElements(),
        timeSec,
        MaxAbsErrReal(dec, ref)
    };
}

void PrintTraceHeader() {
    std::cout << std::left
              << std::setw(17) << "strategy"
              << std::setw(43) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(12) << "ctElems"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';
    std::cout << std::string(130, '-') << '\n';
}

void PrintTraceRow(const TraceRow& row) {
    std::cout << std::left
              << std::setw(17) << row.strategy
              << std::setw(43) << row.step
              << std::setw(10) << row.level
              << std::setw(16) << row.noiseScaleDeg
              << std::setw(12) << row.elements
              << std::setw(16) << std::scientific << row.timeSec
              << std::setw(16) << std::scientific << row.maxAbsErr
              << '\n';
}

void PrintStats(const std::string& label, const EvalStats& stats) {
    std::cout << "  " << label << " tensor products    = " << stats.tensorProducts << '\n';
    std::cout << "  " << label << " relin count        = " << stats.relinCount << '\n';
    std::cout << "  " << label << " rescale count      = " << stats.rescaleCount << '\n';
    std::cout << "  " << label << " scalar mult count  = " << stats.scalarMultCount << '\n';
    std::cout << "  " << label << " level-align count  = " << stats.levelAlignCount << '\n';
    std::cout << "  " << label << " add count          = " << stats.addCount << '\n';
    std::cout << "  " << label << " total operator sec = " << std::scientific << stats.totalOpSec << '\n';
}

void ReduceToLevel(CC cc, Ciphertext<DCRTPoly>& ct, size_t targetLevel, EvalStats& stats) {
    const size_t currentLevel = ct->GetLevel();
    if (currentLevel > targetLevel) {
        throw std::runtime_error("ReduceToLevel: ciphertext is already deeper than target level");
    }
    if (currentLevel < targetLevel) {
        auto t0 = Clock::now();
        cc->LevelReduceInPlace(ct, nullptr, targetLevel - currentLevel);
        auto t1 = Clock::now();
        ++stats.levelAlignCount;
        stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    }
}

void RaiseNoiseScaleDegTo(CC cc, Ciphertext<DCRTPoly>& ct, size_t target, EvalStats& stats) {
    while (ct->GetNoiseScaleDeg() < target) {
        auto t0 = Clock::now();
        ct = cc->EvalMult(ct, 1.0);
        auto t1 = Clock::now();
        ++stats.scalarMultCount;
        stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    }
    if (ct->GetNoiseScaleDeg() > target) {
        throw std::runtime_error("RaiseNoiseScaleDegTo: term is already above target noiseScaleDeg");
    }
}

void AlignToState(CC cc,
                  Ciphertext<DCRTPoly>& ct,
                  size_t targetLevel,
                  size_t targetNoiseScaleDeg,
                  EvalStats& stats) {
    RaiseNoiseScaleDegTo(cc, ct, targetNoiseScaleDeg, stats);
    ReduceToLevel(cc, ct, targetLevel, stats);
}

void AlignPairForProduct(CC cc,
                         Ciphertext<DCRTPoly>& lhs,
                         Ciphertext<DCRTPoly>& rhs,
                         EvalStats& stats) {
    const size_t targetNoiseScaleDeg = std::max(lhs->GetNoiseScaleDeg(), rhs->GetNoiseScaleDeg());
    RaiseNoiseScaleDegTo(cc, lhs, targetNoiseScaleDeg, stats);
    RaiseNoiseScaleDegTo(cc, rhs, targetNoiseScaleDeg, stats);

    const size_t targetLevel = std::max(lhs->GetLevel(), rhs->GetLevel());
    ReduceToLevel(cc, lhs, targetLevel, stats);
    ReduceToLevel(cc, rhs, targetLevel, stats);
}

Ciphertext<DCRTPoly> Materialize(CC cc, Ciphertext<DCRTPoly> ct, EvalStats& stats) {
    auto t0 = Clock::now();
    cc->RelinearizeInPlace(ct);
    auto t1 = Clock::now();
    ++stats.relinCount;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();

    auto t2 = Clock::now();
    cc->RescaleInPlace(ct);
    auto t3 = Clock::now();
    ++stats.rescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
    return ct;
}

Ciphertext<DCRTPoly> Scale(CC cc, Ciphertext<DCRTPoly> ct, double scalar, EvalStats& stats) {
    auto t0 = Clock::now();
    ct = cc->EvalMult(ct, scalar);
    auto t1 = Clock::now();
    ++stats.scalarMultCount;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    return ct;
}

Ciphertext<DCRTPoly> RawProduct(CC cc,
                                const PrivateKey<DCRTPoly>& sk,
                                const std::string& strategy,
                                const std::string& step,
                                const Ciphertext<DCRTPoly>& lhs,
                                const Ciphertext<DCRTPoly>& rhs,
                                const std::vector<double>& ref,
                                std::vector<TraceRow>& rows,
                                EvalStats& stats) {
    auto t0 = Clock::now();
    auto raw = cc->EvalMultNoRelin(lhs, rhs);
    auto t1 = Clock::now();
    ++stats.tensorProducts;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(Capture(strategy, step + " raw product", cc, sk, raw, ref,
                           std::chrono::duration<double>(t1 - t0).count()));
    return raw;
}

Ciphertext<DCRTPoly> MaterializedProduct(CC cc,
                                         const PrivateKey<DCRTPoly>& sk,
                                         const std::string& strategy,
                                         const std::string& step,
                                         const Ciphertext<DCRTPoly>& lhs,
                                         const Ciphertext<DCRTPoly>& rhs,
                                         const std::vector<double>& ref,
                                         std::vector<TraceRow>& rows,
                                         EvalStats& stats) {
    auto raw = RawProduct(cc, sk, strategy, step, lhs, rhs, ref, rows, stats);
    auto t0 = Clock::now();
    auto out = Materialize(cc, raw, stats);
    auto t1 = Clock::now();
    rows.push_back(Capture(strategy, step + " materialized", cc, sk, out, ref,
                           std::chrono::duration<double>(t1 - t0).count()));
    return out;
}

const Ciphertext<DCRTPoly>& FindPower(const std::vector<PowerCt>& powers, size_t power) {
    const auto it = std::find_if(powers.begin(), powers.end(), [power](const PowerCt& item) {
        return item.power == power;
    });
    if (it == powers.end()) {
        throw std::runtime_error("FindPower: missing power x^" + std::to_string(power));
    }
    return it->ct;
}

std::vector<PowerCt> BuildComponentSets(CC cc,
                                        const PrivateKey<DCRTPoly>& sk,
                                        const std::string& strategy,
                                        const Ciphertext<DCRTPoly>& input,
                                        std::vector<TraceRow>& rows,
                                        EvalStats& stats) {
    rows.push_back(Capture(strategy, "S1^(0) component x", cc, sk, input, RefPower(1), 0.0));

    auto x2 = MaterializedProduct(
        cc, sk, strategy, "S1^(0) build x^2", input, input, RefPower(2), rows, stats);

    auto xForX3 = input->Clone();
    auto x2ForX3 = x2->Clone();
    AlignPairForProduct(cc, xForX3, x2ForX3, stats);
    auto x3 = MaterializedProduct(
        cc, sk, strategy, "S1^(0) build x^3", xForX3, x2ForX3, RefPower(3), rows, stats);

    auto x4 = MaterializedProduct(
        cc, sk, strategy, "S1^(1) build x^4", x2, x2, RefPower(4), rows, stats);

    auto x8 = MaterializedProduct(
        cc, sk, strategy, "S1^(1) build x^8", x4, x4, RefPower(8), rows, stats);

    auto x8ForX12 = x8->Clone();
    auto x4ForX12 = x4->Clone();
    AlignPairForProduct(cc, x8ForX12, x4ForX12, stats);
    auto x12 = MaterializedProduct(
        cc, sk, strategy, "S1^(1) build x^12", x8ForX12, x4ForX12, RefPower(12), rows, stats);

    return {
        PowerCt{1, input->Clone()},
        PowerCt{2, x2},
        PowerCt{3, x3},
        PowerCt{4, x4},
        PowerCt{8, x8},
        PowerCt{12, x12},
    };
}

std::vector<PowerCt> AlignComponentsForDecomposition(CC cc,
                                                     const PrivateKey<DCRTPoly>& sk,
                                                     const std::string& strategy,
                                                     const std::vector<PowerCt>& powers,
                                                     std::vector<TraceRow>& rows,
                                                     EvalStats& stats) {
    size_t targetLevel = 0;
    size_t targetNoiseScaleDeg = 0;
    for (const auto& item : powers) {
        targetLevel = std::max(targetLevel, item.ct->GetLevel());
        targetNoiseScaleDeg = std::max(targetNoiseScaleDeg, item.ct->GetNoiseScaleDeg());
    }

    std::vector<PowerCt> aligned;
    aligned.reserve(powers.size());
    for (const auto& item : powers) {
        auto ct = item.ct->Clone();
        AlignToState(cc, ct, targetLevel, targetNoiseScaleDeg, stats);
        rows.push_back(Capture(strategy, "aligned component x^" + std::to_string(item.power),
                               cc, sk, ct, RefPower(item.power), 0.0));
        aligned.push_back(PowerCt{item.power, ct});
    }
    return aligned;
}

Ciphertext<DCRTPoly> AddTerms(CC cc,
                              const PrivateKey<DCRTPoly>& sk,
                              const std::string& strategy,
                              const std::string& step,
                              const std::vector<Ciphertext<DCRTPoly>>& terms,
                              const std::vector<double>& ref,
                              std::vector<TraceRow>& rows,
                              EvalStats& stats) {
    if (terms.empty()) {
        throw std::runtime_error("AddTerms: empty terms");
    }
    auto t0 = Clock::now();
    auto acc = terms[0]->Clone();
    for (size_t i = 1; i < terms.size(); ++i) {
        acc = cc->EvalAdd(acc, terms[i]);
        ++stats.addCount;
    }
    auto t1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(Capture(strategy, step, cc, sk, acc, ref,
                           std::chrono::duration<double>(t1 - t0).count()));
    return acc;
}

EvalOutput RunExpandedEager(CC cc,
                            const PrivateKey<DCRTPoly>& sk,
                            const Ciphertext<DCRTPoly>& input) {
    EvalOutput out;
    out.trace.reserve(64);
    auto components = BuildComponentSets(cc, sk, "expanded-eager", input, out.trace, out.stats);
    components = AlignComponentsForDecomposition(
        cc, sk, "expanded-eager", components, out.trace, out.stats);

    std::vector<Ciphertext<DCRTPoly>> materializedTerms;
    materializedTerms.reserve(kTerms.size());
    for (const auto& term : kTerms) {
        const size_t leftPower = term.a;
        const size_t rightPower = 4 * term.b;
        const size_t outPower = TermPower(term);
        auto product = MaterializedProduct(
            cc, sk, "expanded-eager",
            "term x^" + std::to_string(leftPower) + "*x^" + std::to_string(rightPower),
            FindPower(components, leftPower),
            FindPower(components, rightPower),
            RefPower(outPower),
            out.trace,
            out.stats);
        product = Scale(cc, product, term.coeff, out.stats);
        out.trace.push_back(Capture("expanded-eager",
                                    "scaled term c*x^" + std::to_string(outPower),
                                    cc, sk, product,
                                    RefScaledPower(outPower, term.coeff), 0.0));
        materializedTerms.push_back(product);
    }

    out.value = AddTerms(cc, sk, "expanded-eager", "final materialized term sum",
                         materializedTerms, RefPolynomial(), out.trace, out.stats);
    return out;
}

EvalOutput RunGroupedLazy(CC cc,
                          const PrivateKey<DCRTPoly>& sk,
                          const Ciphertext<DCRTPoly>& input) {
    EvalOutput out;
    out.trace.reserve(64);
    auto components = BuildComponentSets(cc, sk, "grouped-lazy", input, out.trace, out.stats);
    components = AlignComponentsForDecomposition(
        cc, sk, "grouped-lazy", components, out.trace, out.stats);

    std::vector<Ciphertext<DCRTPoly>> rawTerms;
    rawTerms.reserve(kTerms.size());
    for (const auto& term : kTerms) {
        const size_t leftPower = term.a;
        const size_t rightPower = 4 * term.b;
        const size_t outPower = TermPower(term);
        auto raw = RawProduct(
            cc, sk, "grouped-lazy",
            "term x^" + std::to_string(leftPower) + "*x^" + std::to_string(rightPower),
            FindPower(components, leftPower),
            FindPower(components, rightPower),
            RefPower(outPower),
            out.trace,
            out.stats);
        raw = Scale(cc, raw, term.coeff, out.stats);
        out.trace.push_back(Capture("grouped-lazy",
                                    "raw scaled term c*x^" + std::to_string(outPower),
                                    cc, sk, raw,
                                    RefScaledPower(outPower, term.coeff), 0.0));
        rawTerms.push_back(raw);
    }

    auto rawSum = AddTerms(cc, sk, "grouped-lazy", "raw decomposed terms folded",
                           rawTerms, RefPolynomial(), out.trace, out.stats);

    auto t0 = Clock::now();
    out.value = Materialize(cc, rawSum, out.stats);
    auto t1 = Clock::now();
    out.trace.push_back(Capture("grouped-lazy", "final folded sum materialized",
                                cc, sk, out.value, RefPolynomial(),
                                std::chrono::duration<double>(t1 - t0).count()));
    return out;
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] asymmetric prototype t=2 fixed decomposition\n";
    std::cout << "[PROJECT DECISION] t=2 is a conservative teaching prototype, not the paper's optimal t>=3 setting\n";
    std::cout << "[DECOMP] B=4, S1^(0)={x,x^2,x^3}, S1^(1)={x^4,x^8,x^12}\n";
    std::cout << "[POLY] terms use x^(a+4b), a in {1,2,3}, b in {1,2,3}; degree <= 15\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    auto eager = RunExpandedEager(cc, kp.secretKey, ct);
    auto lazy = RunGroupedLazy(cc, kp.secretKey, ct);

    const auto ref = RefPolynomial();
    const double eagerErr = MaxAbsErrReal(DecryptVec(cc, kp.secretKey, eager.value, kSlots), ref);
    const double lazyErr = MaxAbsErrReal(DecryptVec(cc, kp.secretKey, lazy.value, kSlots), ref);

    std::cout << '\n';
    PrintTraceHeader();
    for (const auto& row : eager.trace) {
        PrintTraceRow(row);
    }
    for (const auto& row : lazy.trace) {
        PrintTraceRow(row);
    }

    std::cout << "\nsummary:\n";
    PrintStats("expanded-eager", eager.stats);
    PrintStats("grouped-lazy  ", lazy.stats);
    std::cout << "  expanded-eager final max_abs_err = " << std::scientific << eagerErr << '\n';
    std::cout << "  grouped-lazy   final max_abs_err = " << std::scientific << lazyErr << '\n';

    std::cout << "\ncomparison:\n";
    std::cout << "  tensor products           = " << eager.stats.tensorProducts << " vs "
              << lazy.stats.tensorProducts << '\n';
    std::cout << "  relin count               = " << eager.stats.relinCount << " vs "
              << lazy.stats.relinCount << '\n';
    std::cout << "  rescale count             = " << eager.stats.rescaleCount << " vs "
              << lazy.stats.rescaleCount << '\n';
    std::cout << "  note                      = fixed set decomposition; lazy folds 9 raw decomposed products\n";

    if (eagerErr >= 1e-8 || lazyErr >= 1e-8) {
        throw std::runtime_error("asym proto t2 error threshold failed");
    }
    if (lazy.stats.relinCount > eager.stats.relinCount ||
        lazy.stats.rescaleCount > eager.stats.rescaleCount) {
        throw std::runtime_error("asym proto t2 lazy switch count is worse than eager");
    }
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
