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

struct Basis {
    Ciphertext<DCRTPoly> x;
    Ciphertext<DCRTPoly> x2;
    Ciphertext<DCRTPoly> z;
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

std::vector<double> BuildRefBlock() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        const double x2 = x * x;
        ref.push_back(x2 + 2.0 * x2 * x + x2 * x2);
    }
    return ref;
}

std::vector<double> BuildRefOuterProduct() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        const double x2 = x * x;
        const double block = x2 + 2.0 * x2 * x + x2 * x2;
        ref.push_back(block * x2 * x2);
    }
    return ref;
}

std::vector<double> BuildRefTwoBlockPoly() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        const double x2 = x * x;
        const double z = x2 * x2;
        const double block = x2 + 2.0 * x2 * x + z;
        ref.push_back(block + block * z);
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
              << std::setw(17) << "strategy"
              << std::setw(38) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(12) << "ctElems"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';

    std::cout << std::string(125, '-') << '\n';
}

void PrintTraceRow(const TraceRow& row) {
    std::cout << std::left
              << std::setw(17) << row.strategy
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

Ciphertext<DCRTPoly> MaterializedProduct(CC cc,
                                         const PrivateKey<DCRTPoly>& sk,
                                         const std::string& strategy,
                                         const std::string& label,
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
    rows.push_back(CaptureTrace(
        strategy, label + " raw product",
        cc, sk, raw, ref,
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    auto out = Materialize(cc, raw, stats);
    auto t3 = Clock::now();
    rows.push_back(CaptureTrace(
        strategy, label + " materialized",
        cc, sk, out, ref,
        std::chrono::duration<double>(t3 - t2).count()));
    return out;
}

Basis BuildBasis(CC cc,
                 const PrivateKey<DCRTPoly>& sk,
                 const std::string& strategy,
                 const Ciphertext<DCRTPoly>& input,
                 std::vector<TraceRow>& rows,
                 EvalStats& stats) {
    rows.push_back(CaptureTrace(strategy, "x input", cc, sk, input, BuildRefPower(1), 0.0));

    auto x2 = MaterializedProduct(
        cc, sk, strategy, "precompute x^2",
        input, input, BuildRefPower(2), rows, stats);

    auto xAligned = input->Clone();
    RaiseNoiseScaleDegTo(cc, xAligned, x2->GetNoiseScaleDeg(), stats);
    ReduceToLevel(cc, xAligned, x2->GetLevel(), stats);
    rows.push_back(CaptureTrace(
        strategy, "x aligned with x^2",
        cc, sk, xAligned, BuildRefPower(1), 0.0));

    auto z = MaterializedProduct(
        cc, sk, strategy, "precompute z=x^4",
        x2, x2, BuildRefPower(4), rows, stats);

    return Basis{xAligned, x2, z};
}

Ciphertext<DCRTPoly> RawProduct(CC cc,
                                const PrivateKey<DCRTPoly>& sk,
                                const std::string& strategy,
                                const std::string& label,
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
    rows.push_back(CaptureTrace(
        strategy, label + " raw product",
        cc, sk, raw, ref,
        std::chrono::duration<double>(t1 - t0).count()));
    return raw;
}

Ciphertext<DCRTPoly> EvalBlockExpandedEager(CC cc,
                                            const PrivateKey<DCRTPoly>& sk,
                                            const std::string& blockName,
                                            const Basis& basis,
                                            std::vector<TraceRow>& rows,
                                            EvalStats& stats) {
    auto p00 = MaterializedProduct(
        cc, sk, "expanded-eager", blockName + " x*x",
        basis.x, basis.x, BuildRefPower(2), rows, stats);
    auto p01 = MaterializedProduct(
        cc, sk, "expanded-eager", blockName + " x*x^2",
        basis.x, basis.x2, BuildRefPower(3), rows, stats);
    auto p10 = MaterializedProduct(
        cc, sk, "expanded-eager", blockName + " x^2*x",
        basis.x2, basis.x, BuildRefPower(3), rows, stats);
    auto p11 = MaterializedProduct(
        cc, sk, "expanded-eager", blockName + " x^2*x^2",
        basis.x2, basis.x2, BuildRefPower(4), rows, stats);

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
        "expanded-eager", blockName + " materialized block",
        cc, sk, acc, BuildRefBlock(),
        std::chrono::duration<double>(t1 - t0).count()));
    return acc;
}

Ciphertext<DCRTPoly> EvalBlockGroupedLazy(CC cc,
                                          const PrivateKey<DCRTPoly>& sk,
                                          const std::string& blockName,
                                          const Basis& basis,
                                          std::vector<TraceRow>& rows,
                                          EvalStats& stats) {
    auto p00 = RawProduct(
        cc, sk, "grouped-lazy", blockName + " x*x",
        basis.x, basis.x, BuildRefPower(2), rows, stats);
    auto p01 = RawProduct(
        cc, sk, "grouped-lazy", blockName + " x*x^2",
        basis.x, basis.x2, BuildRefPower(3), rows, stats);
    auto p10 = RawProduct(
        cc, sk, "grouped-lazy", blockName + " x^2*x",
        basis.x2, basis.x, BuildRefPower(3), rows, stats);
    auto p11 = RawProduct(
        cc, sk, "grouped-lazy", blockName + " x^2*x^2",
        basis.x2, basis.x2, BuildRefPower(4), rows, stats);

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
        "grouped-lazy", blockName + " raw block folded",
        cc, sk, rawSum, BuildRefBlock(),
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    auto out = Materialize(cc, rawSum, stats);
    auto t3 = Clock::now();
    rows.push_back(CaptureTrace(
        "grouped-lazy", blockName + " materialized block",
        cc, sk, out, BuildRefBlock(),
        std::chrono::duration<double>(t3 - t2).count()));
    return out;
}

void AlignForAdd(CC cc,
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

Ciphertext<DCRTPoly> AssembleOuter(CC cc,
                                   const PrivateKey<DCRTPoly>& sk,
                                   const std::string& strategy,
                                   const Ciphertext<DCRTPoly>& block0,
                                   const Ciphertext<DCRTPoly>& block1,
                                   const Ciphertext<DCRTPoly>& z,
                                   std::vector<TraceRow>& rows,
                                   EvalStats& stats) {
    auto outer = MaterializedProduct(
        cc, sk, strategy, "outer block1*z",
        block1, z, BuildRefOuterProduct(), rows, stats);

    auto b0 = block0->Clone();
    AlignForAdd(cc, b0, outer, stats);
    rows.push_back(CaptureTrace(
        strategy, "block0 aligned for outer sum",
        cc, sk, b0, BuildRefBlock(), 0.0));

    auto t0 = Clock::now();
    auto result = cc->EvalAdd(b0, outer);
    auto t1 = Clock::now();
    ++stats.addCount;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        strategy, "block0 + block1*z",
        cc, sk, result, BuildRefTwoBlockPoly(),
        std::chrono::duration<double>(t1 - t0).count()));
    return result;
}

Ciphertext<DCRTPoly> RunExpandedEager(CC cc,
                                      const PrivateKey<DCRTPoly>& sk,
                                      const Ciphertext<DCRTPoly>& input,
                                      std::vector<TraceRow>& rows,
                                      EvalStats& stats) {
    const auto basis = BuildBasis(cc, sk, "expanded-eager", input, rows, stats);
    auto block0 = EvalBlockExpandedEager(cc, sk, "block0", basis, rows, stats);
    auto block1 = EvalBlockExpandedEager(cc, sk, "block1", basis, rows, stats);
    return AssembleOuter(cc, sk, "expanded-eager", block0, block1, basis.z, rows, stats);
}

Ciphertext<DCRTPoly> RunGroupedLazy(CC cc,
                                    const PrivateKey<DCRTPoly>& sk,
                                    const Ciphertext<DCRTPoly>& input,
                                    std::vector<TraceRow>& rows,
                                    EvalStats& stats) {
    const auto basis = BuildBasis(cc, sk, "grouped-lazy", input, rows, stats);
    auto block0 = EvalBlockGroupedLazy(cc, sk, "block0", basis, rows, stats);
    auto block1 = EvalBlockGroupedLazy(cc, sk, "block1", basis, rows, stats);
    return AssembleOuter(cc, sk, "grouped-lazy", block0, block1, basis.z, rows, stats);
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] two-block outer assembly\n";
    std::cout << "[POLY] P(x)=b0(x)+b1(x)*z, b0=b1=(x+x^2)^2, z=x^4\n";
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
    rows.reserve(80);

    EvalStats eagerStats;
    auto eagerOut = RunExpandedEager(cc, kp.secretKey, ct, rows, eagerStats);

    EvalStats lazyStats;
    auto lazyOut = RunGroupedLazy(cc, kp.secretKey, ct, rows, lazyStats);

    const auto ref = BuildRefTwoBlockPoly();
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
    std::cout << "  same polynomial           = b0(x)+b1(x)*x^4, b0=b1=(x+x^2)^2\n";
    std::cout << "  tensor products           = "
              << eagerStats.tensorProducts << " vs " << lazyStats.tensorProducts << '\n';
    std::cout << "  relin count               = "
              << eagerStats.relinCount << " vs " << lazyStats.relinCount << '\n';
    std::cout << "  rescale count             = "
              << eagerStats.rescaleCount << " vs " << lazyStats.rescaleCount << '\n';
    std::cout << "  note                      = grouped internal blocks feed a materialized outer assembly\n";
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
