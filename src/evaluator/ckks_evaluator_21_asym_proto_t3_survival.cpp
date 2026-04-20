#include "smoke_common.h"

#include <algorithm>
#include <chrono>
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
    double totalOpSec = 0.0;
};

struct ComponentSet {
    Ciphertext<DCRTPoly> x;
    Ciphertext<DCRTPoly> x2;
    Ciphertext<DCRTPoly> x4;
};

struct EvalOutput {
    Ciphertext<DCRTPoly> value;
    EvalStats stats;
    std::vector<TraceRow> trace;
};

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

CC BuildT3Context(ScalingTechnique scalTech) {
    CCParams<CryptoContextCKKSRNS> parameters;

    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(1 << 12);
    parameters.SetMultiplicativeDepth(6);
    parameters.SetBatchSize(kSlots);
    parameters.SetFirstModSize(60);
    parameters.SetScalingModSize(50);
    parameters.SetScalingTechnique(scalTech);

    // This survival prototype materializes a degree-3 secret-key intermediate.
    parameters.SetMaxRelinSkDeg(3);

    if (scalTech == COMPOSITESCALINGMANUAL) {
        parameters.SetCompositeDegree(2);
        parameters.SetRegisterWordSize(32);
    }

    CC cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);
    return cc;
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
              << std::setw(42) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(12) << "ctElems"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';
    std::cout << std::string(129, '-') << '\n';
}

void PrintTraceRow(const TraceRow& row) {
    std::cout << std::left
              << std::setw(17) << row.strategy
              << std::setw(42) << row.step
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

Ciphertext<DCRTPoly> RawProduct(CC cc,
                                const PrivateKey<DCRTPoly>& sk,
                                const std::string& strategy,
                                const std::string& step,
                                const Ciphertext<DCRTPoly>& lhs,
                                const Ciphertext<DCRTPoly>& rhs,
                                size_t refPower,
                                std::vector<TraceRow>& rows,
                                EvalStats& stats) {
    auto t0 = Clock::now();
    auto raw = cc->EvalMultNoRelin(lhs, rhs);
    auto t1 = Clock::now();
    ++stats.tensorProducts;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(Capture(strategy, step + " raw product", cc, sk, raw, RefPower(refPower),
                           std::chrono::duration<double>(t1 - t0).count()));
    return raw;
}

Ciphertext<DCRTPoly> MaterializedProduct(CC cc,
                                         const PrivateKey<DCRTPoly>& sk,
                                         const std::string& strategy,
                                         const std::string& step,
                                         const Ciphertext<DCRTPoly>& lhs,
                                         const Ciphertext<DCRTPoly>& rhs,
                                         size_t refPower,
                                         std::vector<TraceRow>& rows,
                                         EvalStats& stats) {
    auto raw = RawProduct(cc, sk, strategy, step, lhs, rhs, refPower, rows, stats);
    auto t0 = Clock::now();
    auto out = Materialize(cc, raw, stats);
    auto t1 = Clock::now();
    rows.push_back(Capture(strategy, step + " materialized", cc, sk, out, RefPower(refPower),
                           std::chrono::duration<double>(t1 - t0).count()));
    return out;
}

ComponentSet BuildComponentSets(CC cc,
                                const PrivateKey<DCRTPoly>& sk,
                                const std::string& strategy,
                                const Ciphertext<DCRTPoly>& input,
                                std::vector<TraceRow>& rows,
                                EvalStats& stats) {
    rows.push_back(Capture(strategy, "S1^(0) component x", cc, sk, input, RefPower(1), 0.0));

    auto x2 = MaterializedProduct(
        cc, sk, strategy, "S1^(1) build x^2", input, input, 2, rows, stats);

    auto x4 = MaterializedProduct(
        cc, sk, strategy, "S1^(2) build x^4", x2, x2, 4, rows, stats);

    return ComponentSet{input->Clone(), x2, x4};
}

ComponentSet AlignComponentsForT3(CC cc,
                                  const PrivateKey<DCRTPoly>& sk,
                                  const std::string& strategy,
                                  const ComponentSet& components,
                                  std::vector<TraceRow>& rows,
                                  EvalStats& stats) {
    const size_t targetLevel = std::max({
        components.x->GetLevel(),
        components.x2->GetLevel(),
        components.x4->GetLevel(),
    });
    const size_t targetNoiseScaleDeg = std::max({
        components.x->GetNoiseScaleDeg(),
        components.x2->GetNoiseScaleDeg(),
        components.x4->GetNoiseScaleDeg(),
    });

    auto x = components.x->Clone();
    auto x2 = components.x2->Clone();
    auto x4 = components.x4->Clone();

    AlignToState(cc, x, targetLevel, targetNoiseScaleDeg, stats);
    AlignToState(cc, x2, targetLevel, targetNoiseScaleDeg, stats);
    AlignToState(cc, x4, targetLevel, targetNoiseScaleDeg, stats);

    rows.push_back(Capture(strategy, "aligned S1^(0) x", cc, sk, x, RefPower(1), 0.0));
    rows.push_back(Capture(strategy, "aligned S1^(1) x^2", cc, sk, x2, RefPower(2), 0.0));
    rows.push_back(Capture(strategy, "aligned S1^(2) x^4", cc, sk, x4, RefPower(4), 0.0));

    return ComponentSet{x, x2, x4};
}

EvalOutput RunExpandedEager(CC cc,
                            const PrivateKey<DCRTPoly>& sk,
                            const Ciphertext<DCRTPoly>& input) {
    EvalOutput out;
    out.trace.reserve(24);
    const auto components = BuildComponentSets(cc, sk, "expanded-eager", input, out.trace, out.stats);

    auto x = components.x->Clone();
    auto x2 = components.x2->Clone();
    AlignPairForProduct(cc, x, x2, out.stats);
    auto x3 = MaterializedProduct(
        cc, sk, "expanded-eager", "term prefix x*x^2", x, x2, 3, out.trace, out.stats);

    auto x3ForX7 = x3->Clone();
    auto x4 = components.x4->Clone();
    AlignPairForProduct(cc, x3ForX7, x4, out.stats);
    out.value = MaterializedProduct(
        cc, sk, "expanded-eager", "term x^3*x^4", x3ForX7, x4, 7, out.trace, out.stats);
    return out;
}

EvalOutput RunGroupedLazy(CC cc,
                          const PrivateKey<DCRTPoly>& sk,
                          const Ciphertext<DCRTPoly>& input) {
    EvalOutput out;
    out.trace.reserve(24);
    auto components = BuildComponentSets(cc, sk, "grouped-lazy", input, out.trace, out.stats);
    components = AlignComponentsForT3(cc, sk, "grouped-lazy", components, out.trace, out.stats);

    auto xX2Raw = RawProduct(
        cc, sk, "grouped-lazy", "term prefix x*x^2",
        components.x, components.x2, 3, out.trace, out.stats);

    out.value = RawProduct(
        cc, sk, "grouped-lazy", "term raw prefix*x^4",
        xX2Raw, components.x4, 7, out.trace, out.stats);

    auto t0 = Clock::now();
    out.value = Materialize(cc, out.value, out.stats);
    auto t1 = Clock::now();
    out.trace.push_back(Capture("grouped-lazy", "final t=3 product materialized",
                                cc, sk, out.value, RefPower(7),
                                std::chrono::duration<double>(t1 - t0).count()));
    return out;
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] asymmetric prototype t=3 survival\n";
    std::cout << "[PROJECT DECISION] fixed B=2 survival probe, not a full evaluator\n";
    std::cout << "[DECOMP] S1^(0)={x}, S1^(1)={x^2}, S1^(2)={x^4}\n";
    std::cout << "[TERM] x*x^2*x^4 = x^7\n";
    std::cout << "[KEYGEN] SetMaxRelinSkDeg(3) + EvalMultKeysGen\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildT3Context(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeysGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    auto eager = RunExpandedEager(cc, kp.secretKey, ct);
    auto lazy = RunGroupedLazy(cc, kp.secretKey, ct);

    const auto ref = RefPower(7);
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
    std::cout << "  note                      = lazy keeps a length-2 raw chain before one materialization\n";

    if (eagerErr >= 1e-8 || lazyErr >= 1e-8) {
        throw std::runtime_error("asym proto t3 survival error threshold failed");
    }
    if (lazy.stats.relinCount > eager.stats.relinCount ||
        lazy.stats.rescaleCount > eager.stats.rescaleCount) {
        throw std::runtime_error("asym proto t3 survival lazy switch count is worse than eager");
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
