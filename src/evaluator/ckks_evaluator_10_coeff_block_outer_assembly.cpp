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

struct BlockCoeffs {
    double x2;
    double x3;
    double x4;
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

struct Basis {
    Ciphertext<DCRTPoly> x;
    Ciphertext<DCRTPoly> x2;
    Ciphertext<DCRTPoly> z;
};

const BlockCoeffs kBlock0{0.7, -1.2, 0.5};
const BlockCoeffs kBlock1{-0.4, 0.9, 1.1};

std::vector<double> RefPower(size_t power) {
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

std::vector<double> RefScaledPower(size_t power, double scalar) {
    auto ref = RefPower(power);
    for (double& v : ref) {
        v *= scalar;
    }
    return ref;
}

double PlainBlock(double x, const BlockCoeffs& coeffs) {
    const double x2 = x * x;
    const double x3 = x2 * x;
    const double x4 = x2 * x2;
    return coeffs.x2 * x2 + coeffs.x3 * x3 + coeffs.x4 * x4;
}

std::vector<double> RefBlock(const BlockCoeffs& coeffs) {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(PlainBlock(x, coeffs));
    }
    return ref;
}

std::vector<double> RefOuterProduct() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        const double z = x * x * x * x;
        ref.push_back(PlainBlock(x, kBlock1) * z);
    }
    return ref;
}

std::vector<double> RefPolynomial() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        const double z = x * x * x * x;
        ref.push_back(PlainBlock(x, kBlock0) + PlainBlock(x, kBlock1) * z);
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
    std::vector<std::complex<double>> dec;
    try {
        dec = DecryptVec(cc, sk, ct, kSlots);
    }
    catch (const std::exception& e) {
        throw std::runtime_error("Capture failed at [" + strategy + "] " + step + ": " + e.what());
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
              << std::setw(40) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(12) << "ctElems"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';
    std::cout << std::string(127, '-') << '\n';
}

void PrintTraceRow(const TraceRow& row) {
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

Basis BuildBasis(CC cc,
                 const PrivateKey<DCRTPoly>& sk,
                 const std::string& strategy,
                 const Ciphertext<DCRTPoly>& input,
                 std::vector<TraceRow>& rows,
                 EvalStats& stats) {
    rows.push_back(Capture(strategy, "x input", cc, sk, input, RefPower(1), 0.0));

    auto x2 = MaterializedProduct(
        cc, sk, strategy, "precompute x^2", input, input, RefPower(2), rows, stats);

    auto xAligned = input->Clone();
    RaiseNoiseScaleDegTo(cc, xAligned, x2->GetNoiseScaleDeg(), stats);
    ReduceToLevel(cc, xAligned, x2->GetLevel(), stats);
    rows.push_back(Capture(strategy, "x aligned with x^2", cc, sk, xAligned, RefPower(1), 0.0));

    auto z = MaterializedProduct(
        cc, sk, strategy, "precompute z=x^4", x2, x2, RefPower(4), rows, stats);
    return Basis{xAligned, x2, z};
}

Ciphertext<DCRTPoly> AddPreparedTerms(CC cc,
                                      const PrivateKey<DCRTPoly>& sk,
                                      const std::string& strategy,
                                      const std::string& step,
                                      const std::vector<Ciphertext<DCRTPoly>>& terms,
                                      const std::vector<double>& ref,
                                      std::vector<TraceRow>& rows,
                                      EvalStats& stats) {
    auto t0 = Clock::now();
    auto acc = cc->EvalAdd(terms[0], terms[1]);
    ++stats.addCount;
    for (size_t i = 2; i < terms.size(); ++i) {
        acc = cc->EvalAdd(acc, terms[i]);
        ++stats.addCount;
    }
    auto t1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(Capture(strategy, step, cc, sk, acc, ref,
                           std::chrono::duration<double>(t1 - t0).count()));
    return acc;
}

Ciphertext<DCRTPoly> EvalBlockExpandedEager(CC cc,
                                            const PrivateKey<DCRTPoly>& sk,
                                            const std::string& blockName,
                                            const BlockCoeffs& coeffs,
                                            const Basis& basis,
                                            std::vector<TraceRow>& rows,
                                            EvalStats& stats) {
    auto x2Term = MaterializedProduct(
        cc, sk, "expanded-eager", blockName + " x*x",
        basis.x, basis.x, RefPower(2), rows, stats);
    x2Term = Scale(cc, x2Term, coeffs.x2, stats);
    rows.push_back(Capture("expanded-eager", blockName + " coeff*x^2", cc, sk,
                           x2Term, RefScaledPower(2, coeffs.x2), 0.0));

    auto x3Left = MaterializedProduct(
        cc, sk, "expanded-eager", blockName + " x*x^2",
        basis.x, basis.x2, RefPower(3), rows, stats);
    x3Left = Scale(cc, x3Left, coeffs.x3 * 0.5, stats);
    rows.push_back(Capture("expanded-eager", blockName + " coeff/2*x*x^2", cc, sk,
                           x3Left, RefScaledPower(3, coeffs.x3 * 0.5), 0.0));

    auto x3Right = MaterializedProduct(
        cc, sk, "expanded-eager", blockName + " x^2*x",
        basis.x2, basis.x, RefPower(3), rows, stats);
    x3Right = Scale(cc, x3Right, coeffs.x3 * 0.5, stats);
    rows.push_back(Capture("expanded-eager", blockName + " coeff/2*x^2*x", cc, sk,
                           x3Right, RefScaledPower(3, coeffs.x3 * 0.5), 0.0));

    auto x4Term = MaterializedProduct(
        cc, sk, "expanded-eager", blockName + " x^2*x^2",
        basis.x2, basis.x2, RefPower(4), rows, stats);
    x4Term = Scale(cc, x4Term, coeffs.x4, stats);
    rows.push_back(Capture("expanded-eager", blockName + " coeff*x^4", cc, sk,
                           x4Term, RefScaledPower(4, coeffs.x4), 0.0));

    return AddPreparedTerms(cc, sk, "expanded-eager", blockName + " materialized coeff block",
                            {x2Term, x3Left, x3Right, x4Term}, RefBlock(coeffs), rows, stats);
}

Ciphertext<DCRTPoly> EvalBlockGroupedLazy(CC cc,
                                          const PrivateKey<DCRTPoly>& sk,
                                          const std::string& blockName,
                                          const BlockCoeffs& coeffs,
                                          const Basis& basis,
                                          std::vector<TraceRow>& rows,
                                          EvalStats& stats) {
    auto x2Term = RawProduct(
        cc, sk, "grouped-lazy", blockName + " x*x",
        basis.x, basis.x, RefPower(2), rows, stats);
    x2Term = Scale(cc, x2Term, coeffs.x2, stats);
    rows.push_back(Capture("grouped-lazy", blockName + " raw coeff*x^2", cc, sk,
                           x2Term, RefScaledPower(2, coeffs.x2), 0.0));

    auto x3Left = RawProduct(
        cc, sk, "grouped-lazy", blockName + " x*x^2",
        basis.x, basis.x2, RefPower(3), rows, stats);
    x3Left = Scale(cc, x3Left, coeffs.x3 * 0.5, stats);
    rows.push_back(Capture("grouped-lazy", blockName + " raw coeff/2*x*x^2", cc, sk,
                           x3Left, RefScaledPower(3, coeffs.x3 * 0.5), 0.0));

    auto x3Right = RawProduct(
        cc, sk, "grouped-lazy", blockName + " x^2*x",
        basis.x2, basis.x, RefPower(3), rows, stats);
    x3Right = Scale(cc, x3Right, coeffs.x3 * 0.5, stats);
    rows.push_back(Capture("grouped-lazy", blockName + " raw coeff/2*x^2*x", cc, sk,
                           x3Right, RefScaledPower(3, coeffs.x3 * 0.5), 0.0));

    auto x4Term = RawProduct(
        cc, sk, "grouped-lazy", blockName + " x^2*x^2",
        basis.x2, basis.x2, RefPower(4), rows, stats);
    x4Term = Scale(cc, x4Term, coeffs.x4, stats);
    rows.push_back(Capture("grouped-lazy", blockName + " raw coeff*x^4", cc, sk,
                           x4Term, RefScaledPower(4, coeffs.x4), 0.0));

    auto rawBlock = AddPreparedTerms(cc, sk, "grouped-lazy", blockName + " raw coeff block folded",
                                     {x2Term, x3Left, x3Right, x4Term}, RefBlock(coeffs), rows, stats);

    auto t0 = Clock::now();
    auto out = Materialize(cc, rawBlock, stats);
    auto t1 = Clock::now();
    rows.push_back(Capture("grouped-lazy", blockName + " materialized coeff block",
                           cc, sk, out, RefBlock(coeffs),
                           std::chrono::duration<double>(t1 - t0).count()));
    return out;
}

void AlignForAdd(CC cc, Ciphertext<DCRTPoly>& lhs, Ciphertext<DCRTPoly>& rhs, EvalStats& stats) {
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
        cc, sk, strategy, "outer block1*z", block1, z, RefOuterProduct(), rows, stats);

    auto b0 = block0->Clone();
    AlignForAdd(cc, b0, outer, stats);
    rows.push_back(Capture(strategy, "block0 aligned for outer sum", cc, sk, b0, RefBlock(kBlock0), 0.0));

    auto t0 = Clock::now();
    auto result = cc->EvalAdd(b0, outer);
    auto t1 = Clock::now();
    ++stats.addCount;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(Capture(strategy, "block0 + block1*z", cc, sk, result, RefPolynomial(),
                           std::chrono::duration<double>(t1 - t0).count()));
    return result;
}

Ciphertext<DCRTPoly> RunExpandedEager(CC cc,
                                      const PrivateKey<DCRTPoly>& sk,
                                      const Ciphertext<DCRTPoly>& input,
                                      std::vector<TraceRow>& rows,
                                      EvalStats& stats) {
    const auto basis = BuildBasis(cc, sk, "expanded-eager", input, rows, stats);
    auto block0 = EvalBlockExpandedEager(cc, sk, "block0", kBlock0, basis, rows, stats);
    auto block1 = EvalBlockExpandedEager(cc, sk, "block1", kBlock1, basis, rows, stats);
    return AssembleOuter(cc, sk, "expanded-eager", block0, block1, basis.z, rows, stats);
}

Ciphertext<DCRTPoly> RunGroupedLazy(CC cc,
                                    const PrivateKey<DCRTPoly>& sk,
                                    const Ciphertext<DCRTPoly>& input,
                                    std::vector<TraceRow>& rows,
                                    EvalStats& stats) {
    const auto basis = BuildBasis(cc, sk, "grouped-lazy", input, rows, stats);
    auto block0 = EvalBlockGroupedLazy(cc, sk, "block0", kBlock0, basis, rows, stats);
    auto block1 = EvalBlockGroupedLazy(cc, sk, "block1", kBlock1, basis, rows, stats);
    return AssembleOuter(cc, sk, "grouped-lazy", block0, block1, basis.z, rows, stats);
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] coefficient block outer assembly\n";
    std::cout << "[POLY] P(x)=b0(x)+b1(x)*x^4\n";
    std::cout << "[b0] 0.7*x^2 - 1.2*x^3 + 0.5*x^4\n";
    std::cout << "[b1] -0.4*x^2 + 0.9*x^3 + 1.1*x^4\n";
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
    rows.reserve(96);

    EvalStats eagerStats;
    auto eagerOut = RunExpandedEager(cc, kp.secretKey, ct, rows, eagerStats);

    EvalStats lazyStats;
    auto lazyOut = RunGroupedLazy(cc, kp.secretKey, ct, rows, lazyStats);

    const auto ref = RefPolynomial();
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
    std::cout << "  same polynomial           = coefficient two-block polynomial\n";
    std::cout << "  tensor products           = " << eagerStats.tensorProducts << " vs " << lazyStats.tensorProducts << '\n';
    std::cout << "  relin count               = " << eagerStats.relinCount << " vs " << lazyStats.relinCount << '\n';
    std::cout << "  rescale count             = " << eagerStats.rescaleCount << " vs " << lazyStats.rescaleCount << '\n';
    std::cout << "  note                      = raw scalar-weighted products fold before block materialization\n";
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
