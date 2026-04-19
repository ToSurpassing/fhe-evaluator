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

struct BaseTerms {
    Ciphertext<DCRTPoly> x;
    Ciphertext<DCRTPoly> x2;
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

std::vector<double> BuildRefGroupedBlock() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        const double x2 = x * x;
        ref.push_back(x2 + 2.0 * x2 * x + x2 * x2);
    }
    return ref;
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
              << std::setw(16) << "strategy"
              << std::setw(36) << "step"
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
              << std::setw(16) << row.strategy
              << std::setw(36) << row.step
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

Ciphertext<DCRTPoly> Materialize(CC cc,
                                 Ciphertext<DCRTPoly> ct,
                                 EvalStats& stats) {
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

BaseTerms BuildBaseTerms(CC cc,
                         const PrivateKey<DCRTPoly>& sk,
                         const std::string& strategy,
                         const Ciphertext<DCRTPoly>& x,
                         std::vector<TraceRow>& rows,
                         EvalStats& stats) {
    rows.push_back(CaptureTrace(strategy, "x input", cc, sk, x, BuildRefPower(1), 0.0));

    auto t0 = Clock::now();
    auto x2Raw = cc->EvalMultNoRelin(x, x);
    auto t1 = Clock::now();
    ++stats.tensorProducts;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        strategy, "base x*x raw",
        cc, sk, x2Raw, BuildRefPower(2),
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    auto x2 = Materialize(cc, x2Raw, stats);
    auto t3 = Clock::now();
    rows.push_back(CaptureTrace(
        strategy, "base x^2 materialized",
        cc, sk, x2, BuildRefPower(2),
        std::chrono::duration<double>(t3 - t2).count()));

    auto xPrepared = x->Clone();
    RaiseNoiseScaleDegTo(cc, xPrepared, x2->GetNoiseScaleDeg(), stats);
    ReduceToLevel(cc, xPrepared, x2->GetLevel(), stats);
    rows.push_back(CaptureTrace(
        strategy, "x aligned with x^2",
        cc, sk, xPrepared, BuildRefPower(1), 0.0));

    return BaseTerms{xPrepared, x2};
}

Ciphertext<DCRTPoly> BuildMaterializedProduct(CC cc,
                                              const PrivateKey<DCRTPoly>& sk,
                                              const std::string& strategy,
                                              const std::string& label,
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
    rows.push_back(CaptureTrace(
        strategy, label + " raw product",
        cc, sk, raw, BuildRefPower(refPower),
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    auto mat = Materialize(cc, raw, stats);
    auto t3 = Clock::now();
    rows.push_back(CaptureTrace(
        strategy, label + " materialized",
        cc, sk, mat, BuildRefPower(refPower),
        std::chrono::duration<double>(t3 - t2).count()));

    return mat;
}

Ciphertext<DCRTPoly> RunExpandedEager(CC cc,
                                      const PrivateKey<DCRTPoly>& sk,
                                      const Ciphertext<DCRTPoly>& x,
                                      std::vector<TraceRow>& rows,
                                      EvalStats& stats) {
    const auto base = BuildBaseTerms(cc, sk, "expanded-eager", x, rows, stats);

    auto p00 = BuildMaterializedProduct(
        cc, sk, "expanded-eager", "x*x",
        base.x, base.x, 2, rows, stats);
    auto p01 = BuildMaterializedProduct(
        cc, sk, "expanded-eager", "x*x^2",
        base.x, base.x2, 3, rows, stats);
    auto p10 = BuildMaterializedProduct(
        cc, sk, "expanded-eager", "x^2*x",
        base.x2, base.x, 3, rows, stats);
    auto p11 = BuildMaterializedProduct(
        cc, sk, "expanded-eager", "x^2*x^2",
        base.x2, base.x2, 4, rows, stats);

    auto t0 = Clock::now();
    auto acc = cc->EvalAdd(p00, p01);
    ++stats.addCount;
    acc = cc->EvalAdd(acc, p10);
    ++stats.addCount;
    acc = cc->EvalAdd(acc, p11);
    ++stats.addCount;
    auto t1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "expanded-eager", "q(x)=expanded sum",
        cc, sk, acc, BuildRefGroupedBlock(),
        std::chrono::duration<double>(t1 - t0).count()));

    return acc;
}

Ciphertext<DCRTPoly> RawProduct(CC cc,
                                const PrivateKey<DCRTPoly>& sk,
                                const std::string& label,
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
    rows.push_back(CaptureTrace(
        "grouped-lazy", label + " raw product",
        cc, sk, raw, BuildRefPower(refPower),
        std::chrono::duration<double>(t1 - t0).count()));
    return raw;
}

Ciphertext<DCRTPoly> RunGroupedLazy(CC cc,
                                    const PrivateKey<DCRTPoly>& sk,
                                    const Ciphertext<DCRTPoly>& x,
                                    std::vector<TraceRow>& rows,
                                    EvalStats& stats) {
    const auto base = BuildBaseTerms(cc, sk, "grouped-lazy", x, rows, stats);

    auto p00 = RawProduct(cc, sk, "x*x", base.x, base.x, 2, rows, stats);
    auto p01 = RawProduct(cc, sk, "x*x^2", base.x, base.x2, 3, rows, stats);
    auto p10 = RawProduct(cc, sk, "x^2*x", base.x2, base.x, 3, rows, stats);
    auto p11 = RawProduct(cc, sk, "x^2*x^2", base.x2, base.x2, 4, rows, stats);

    auto t0 = Clock::now();
    auto rawSum = cc->EvalAdd(p00, p01);
    ++stats.addCount;
    rawSum = cc->EvalAdd(rawSum, p10);
    ++stats.addCount;
    rawSum = cc->EvalAdd(rawSum, p11);
    ++stats.addCount;
    auto t1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "grouped-lazy", "raw block folded",
        cc, sk, rawSum, BuildRefGroupedBlock(),
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    auto out = Materialize(cc, rawSum, stats);
    auto t3 = Clock::now();
    rows.push_back(CaptureTrace(
        "grouped-lazy", "folded block materialized",
        cc, sk, out, BuildRefGroupedBlock(),
        std::chrono::duration<double>(t3 - t2).count()));

    return out;
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] grouped same-state baby block eval\n";
    std::cout << "[BLOCK POLY] q(x) = x^2 + 2*x^3 + x^4 = (x + x^2)^2\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    std::vector<TraceRow> rows;
    rows.reserve(40);

    EvalStats eagerStats;
    auto eagerOut = RunExpandedEager(cc, kp.secretKey, ct, rows, eagerStats);

    EvalStats lazyStats;
    auto lazyOut = RunGroupedLazy(cc, kp.secretKey, ct, rows, lazyStats);

    const auto ref = BuildRefGroupedBlock();
    const auto eagerErr = MaxAbsErrReal(DecryptVec(cc, kp.secretKey, eagerOut, kSlots), ref);
    const auto lazyErr = MaxAbsErrReal(DecryptVec(cc, kp.secretKey, lazyOut, kSlots), ref);

    std::cout << '\n';
    PrintTraceHeader();
    for (const auto& row : rows) {
        PrintTraceRow(row);
    }

    std::cout << "\nsummary:\n";
    PrintStats("expanded-eager", eagerStats);
    PrintStats("grouped-lazy  ", lazyStats);
    std::cout << "  expanded-eager final max_abs_err = " << std::scientific << eagerErr << '\n';
    std::cout << "  grouped-lazy   final max_abs_err = " << std::scientific << lazyErr << '\n';

    std::cout << "\ncomparison:\n";
    std::cout << "  same block polynomial     = x^2 + 2*x^3 + x^4\n";
    std::cout << "  tensor products           = "
              << eagerStats.tensorProducts << " vs " << lazyStats.tensorProducts << '\n';
    std::cout << "  relin count               = "
              << eagerStats.relinCount << " vs " << lazyStats.relinCount << '\n';
    std::cout << "  rescale count             = "
              << eagerStats.rescaleCount << " vs " << lazyStats.rescaleCount << '\n';
    std::cout << "  note                      = same-state raw products can be folded before one materialization\n";
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
