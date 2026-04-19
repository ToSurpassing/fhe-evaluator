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

struct PowerTerm {
    size_t exponent;
    Ciphertext<DCRTPoly> ct;
    std::string state;
};

struct TraceRow {
    std::string strategy;
    std::string step;
    size_t level;
    size_t noiseScaleDeg;
    size_t elements;
    double timeSec;
    double maxAbsErr;
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

std::vector<double> BuildRefPower(size_t power) {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        double y = 1.0;
        for (size_t i = 0; i < power; ++i) {
            y *= x;
        }
        ref.push_back(y);
    }
    return ref;
}

std::vector<double> BuildRefBlockPoly() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(x + x * x + x * x * x + x * x * x * x);
    }
    return ref;
}

CC BuildLinearBlockContext(ScalingTechnique scalTech) {
    CCParams<CryptoContextCKKSRNS> parameters;

    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(1 << 12);
    parameters.SetMultiplicativeDepth(6);
    parameters.SetBatchSize(kSlots);
    parameters.SetFirstModSize(60);
    parameters.SetScalingModSize(50);
    parameters.SetScalingTechnique(scalTech);

    // The raw construction path keeps x^4 as a degree-4 intermediate.
    parameters.SetMaxRelinSkDeg(4);

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

TraceRow CaptureTrace(const std::string& strategy,
                      const std::string& step,
                      CC cc,
                      const PrivateKey<DCRTPoly>& sk,
                      const Ciphertext<DCRTPoly>& ct,
                      const std::vector<double>& ref,
                      double timeSec) {
    std::vector<std::complex<double>> dec;
    try {
        dec = DecryptVec(cc, sk, ct, kSlots);
    }
    catch (const std::exception& e) {
        throw std::runtime_error("CaptureTrace failed at [" + strategy + "] " + step + ": " + e.what());
    }

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
              << std::setw(14) << "strategy"
              << std::setw(38) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(12) << "ctElems"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';

    std::cout << std::string(122, '-') << '\n';
}

void PrintTraceRow(const TraceRow& row) {
    std::cout << std::left
              << std::setw(14) << row.strategy
              << std::setw(38) << row.step
              << std::setw(10) << row.level
              << std::setw(16) << row.noiseScaleDeg
              << std::setw(12) << row.elements
              << std::setw(16) << std::scientific << row.timeSec
              << std::setw(16) << std::scientific << row.maxAbsErr
              << '\n';
}

void PrintStats(const std::string& label,
                const EvalStats& stats) {
    std::cout << "  " << label << " tensor products    = " << stats.tensorProducts << '\n';
    std::cout << "  " << label << " relin count        = " << stats.relinCount << '\n';
    std::cout << "  " << label << " rescale count      = " << stats.rescaleCount << '\n';
    std::cout << "  " << label << " scalar mult count  = " << stats.scalarMultCount << '\n';
    std::cout << "  " << label << " level-align count  = " << stats.levelAlignCount << '\n';
    std::cout << "  " << label << " add count          = " << stats.addCount << '\n';
    std::cout << "  " << label << " total operator sec = " << std::scientific << stats.totalOpSec << '\n';
}

void ReduceToLevel(CC cc,
                   Ciphertext<DCRTPoly>& ct,
                   size_t targetLevel,
                   EvalStats& stats) {
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

void RaiseNoiseScaleDegTo(CC cc,
                          Ciphertext<DCRTPoly>& ct,
                          size_t targetNoiseScaleDeg,
                          EvalStats& stats) {
    while (ct->GetNoiseScaleDeg() < targetNoiseScaleDeg) {
        auto t0 = Clock::now();
        ct = cc->EvalMult(ct, 1.0);
        auto t1 = Clock::now();
        ++stats.scalarMultCount;
        stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    }
    if (ct->GetNoiseScaleDeg() > targetNoiseScaleDeg) {
        throw std::runtime_error("RaiseNoiseScaleDegTo: term is already above target noiseScaleDeg");
    }
}

Ciphertext<DCRTPoly> BuildEagerNext(CC cc,
                                    const PrivateKey<DCRTPoly>& sk,
                                    const Ciphertext<DCRTPoly>& prev,
                                    size_t prevExponent,
                                    const Ciphertext<DCRTPoly>& x,
                                    std::vector<TraceRow>& rows,
                                    EvalStats& stats) {
    auto xAligned = x->Clone();
    ReduceToLevel(cc, xAligned, prev->GetLevel(), stats);

    const size_t exponent = prevExponent + 1;
    auto t0 = Clock::now();
    auto raw = cc->EvalMultNoRelin(prev, xAligned);
    auto t1 = Clock::now();
    ++stats.tensorProducts;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "eager", "x^" + std::to_string(exponent) + " raw multiply",
        cc, sk, raw, BuildRefPower(exponent),
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    cc->RelinearizeInPlace(raw);
    auto t3 = Clock::now();
    ++stats.relinCount;
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();

    auto t4 = Clock::now();
    cc->RescaleInPlace(raw);
    auto t5 = Clock::now();
    ++stats.rescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(t5 - t4).count();
    rows.push_back(CaptureTrace(
        "eager", "x^" + std::to_string(exponent) + " materialized",
        cc, sk, raw, BuildRefPower(exponent),
        std::chrono::duration<double>(t5 - t2).count()));

    return raw;
}

std::vector<PowerTerm> BuildEagerTerms(CC cc,
                                       const PrivateKey<DCRTPoly>& sk,
                                       const Ciphertext<DCRTPoly>& x,
                                       std::vector<TraceRow>& rows,
                                       EvalStats& stats) {
    std::vector<PowerTerm> terms;
    terms.reserve(4);
    terms.push_back(PowerTerm{1, x->Clone(), "input"});
    rows.push_back(CaptureTrace("eager", "x^1 input", cc, sk, x, BuildRefPower(1), 0.0));

    auto x2 = BuildEagerNext(cc, sk, x, 1, x, rows, stats);
    terms.push_back(PowerTerm{2, x2, "materialized"});
    auto x3 = BuildEagerNext(cc, sk, x2, 2, x, rows, stats);
    terms.push_back(PowerTerm{3, x3, "materialized"});
    auto x4 = BuildEagerNext(cc, sk, x3, 3, x, rows, stats);
    terms.push_back(PowerTerm{4, x4, "materialized"});

    return terms;
}

Ciphertext<DCRTPoly> BuildRawNext(CC cc,
                                  const PrivateKey<DCRTPoly>& sk,
                                  const Ciphertext<DCRTPoly>& prev,
                                  size_t prevExponent,
                                  const Ciphertext<DCRTPoly>& x,
                                  std::vector<TraceRow>& rows,
                                  EvalStats& stats) {
    const size_t exponent = prevExponent + 1;
    auto t0 = Clock::now();
    auto raw = cc->EvalMultNoRelin(prev, x);
    auto t1 = Clock::now();
    ++stats.tensorProducts;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "raw-delay", "x^" + std::to_string(exponent) + " raw retained",
        cc, sk, raw, BuildRefPower(exponent),
        std::chrono::duration<double>(t1 - t0).count()));
    return raw;
}

Ciphertext<DCRTPoly> MaterializeRawTerm(CC cc,
                                        const PrivateKey<DCRTPoly>& sk,
                                        const Ciphertext<DCRTPoly>& rawTerm,
                                        size_t exponent,
                                        std::vector<TraceRow>& rows,
                                        EvalStats& stats) {
    auto term = rawTerm->Clone();

    auto t0 = Clock::now();
    cc->RelinearizeInPlace(term);
    auto t1 = Clock::now();
    ++stats.relinCount;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();

    auto t2 = Clock::now();
    cc->RescaleInPlace(term);
    auto t3 = Clock::now();
    ++stats.rescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();

    rows.push_back(CaptureTrace(
        "raw-delay", "x^" + std::to_string(exponent) + " eval-boundary materialized",
        cc, sk, term, BuildRefPower(exponent),
        std::chrono::duration<double>(t3 - t0).count()));

    return term;
}

std::vector<PowerTerm> BuildRawDelayedTerms(CC cc,
                                            const PrivateKey<DCRTPoly>& sk,
                                            const Ciphertext<DCRTPoly>& x,
                                            std::vector<TraceRow>& rows,
                                            EvalStats& stats) {
    std::vector<PowerTerm> terms;
    terms.reserve(4);
    terms.push_back(PowerTerm{1, x->Clone(), "input"});
    rows.push_back(CaptureTrace("raw-delay", "x^1 input", cc, sk, x, BuildRefPower(1), 0.0));

    auto x2Raw = BuildRawNext(cc, sk, x, 1, x, rows, stats);
    auto x3Raw = BuildRawNext(cc, sk, x2Raw, 2, x, rows, stats);
    auto x4Raw = BuildRawNext(cc, sk, x3Raw, 3, x, rows, stats);

    terms.push_back(PowerTerm{2, MaterializeRawTerm(cc, sk, x2Raw, 2, rows, stats), "eval-boundary materialized"});
    terms.push_back(PowerTerm{3, MaterializeRawTerm(cc, sk, x3Raw, 3, rows, stats), "eval-boundary materialized"});
    terms.push_back(PowerTerm{4, MaterializeRawTerm(cc, sk, x4Raw, 4, rows, stats), "eval-boundary materialized"});

    return terms;
}

Ciphertext<DCRTPoly> SumPreparedTerms(CC cc,
                                      const PrivateKey<DCRTPoly>& sk,
                                      const std::string& strategy,
                                      const std::vector<PowerTerm>& terms,
                                      std::vector<TraceRow>& rows,
                                      EvalStats& stats) {
    size_t targetLevel = 0;
    size_t targetNoiseScaleDeg = 0;
    for (const auto& term : terms) {
        targetLevel = std::max(targetLevel, term.ct->GetLevel());
        targetNoiseScaleDeg = std::max(targetNoiseScaleDeg, term.ct->GetNoiseScaleDeg());
    }

    std::vector<Ciphertext<DCRTPoly>> prepared;
    prepared.reserve(terms.size());
    for (const auto& term : terms) {
        auto ct = term.ct->Clone();
        RaiseNoiseScaleDegTo(cc, ct, targetNoiseScaleDeg, stats);
        ReduceToLevel(cc, ct, targetLevel, stats);
        rows.push_back(CaptureTrace(
            strategy, "x^" + std::to_string(term.exponent) + " prepared for sum",
            cc, sk, ct, BuildRefPower(term.exponent), 0.0));
        prepared.push_back(ct);
    }

    auto t0 = Clock::now();
    auto acc = cc->EvalAdd(prepared[0], prepared[1]);
    ++stats.addCount;
    for (size_t i = 2; i < prepared.size(); ++i) {
        acc = cc->EvalAdd(acc, prepared[i]);
        ++stats.addCount;
    }
    auto t1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();

    rows.push_back(CaptureTrace(
        strategy, "q(x)=x+x^2+x^3+x^4",
        cc, sk, acc, BuildRefBlockPoly(),
        std::chrono::duration<double>(t1 - t0).count()));

    return acc;
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] baby-step linear block eval\n";
    std::cout << "[BLOCK POLY] q(x) = x + x^2 + x^3 + x^4\n";
    std::cout << "[KEYGEN] SetMaxRelinSkDeg(4) + EvalMultKeysGen\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildLinearBlockContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeysGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    std::vector<TraceRow> rows;
    rows.reserve(48);

    EvalStats eagerStats;
    auto eagerTerms = BuildEagerTerms(cc, kp.secretKey, ct, rows, eagerStats);
    auto eagerOut = SumPreparedTerms(cc, kp.secretKey, "eager", eagerTerms, rows, eagerStats);

    EvalStats rawStats;
    auto rawTerms = BuildRawDelayedTerms(cc, kp.secretKey, ct, rows, rawStats);
    auto rawOut = SumPreparedTerms(cc, kp.secretKey, "raw-delay", rawTerms, rows, rawStats);

    const auto ref = BuildRefBlockPoly();
    const auto eagerErr = MaxAbsErrReal(DecryptVec(cc, kp.secretKey, eagerOut, kSlots), ref);
    const auto rawErr = MaxAbsErrReal(DecryptVec(cc, kp.secretKey, rawOut, kSlots), ref);

    std::cout << '\n';
    PrintTraceHeader();
    for (const auto& row : rows) {
        PrintTraceRow(row);
    }

    std::cout << "\nsummary:\n";
    PrintStats("eager    ", eagerStats);
    PrintStats("raw-delay", rawStats);
    std::cout << "  eager     final max_abs_err = " << std::scientific << eagerErr << '\n';
    std::cout << "  raw-delay final max_abs_err = " << std::scientific << rawErr << '\n';

    std::cout << "\ncomparison:\n";
    std::cout << "  same block polynomial     = q(x)=x+x^2+x^3+x^4\n";
    std::cout << "  tensor products           = "
              << eagerStats.tensorProducts << " vs " << rawStats.tensorProducts << '\n';
    std::cout << "  relin count               = "
              << eagerStats.relinCount << " vs " << rawStats.relinCount << '\n';
    std::cout << "  rescale count             = "
              << eagerStats.rescaleCount << " vs " << rawStats.rescaleCount << '\n';
    std::cout << "  note                      = raw terms still need per-term materialization for this linear sum\n";
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
