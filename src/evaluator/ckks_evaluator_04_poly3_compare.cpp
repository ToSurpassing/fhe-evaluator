#include "smoke_common.h"

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
    double totalOpSec = 0.0;
    size_t tensorProductCount = 0;
    size_t relinCount = 0;
    size_t rescaleCount = 0;
    size_t scalarMultCount = 0;
};

void PrintTraceHeader() {
    std::cout << std::left
              << std::setw(12) << "strategy"
              << std::setw(42) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(12) << "ctElems"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';

    std::cout << std::string(124, '-') << '\n';
}

void PrintTraceRow(const TraceRow& row) {
    std::cout << std::left
              << std::setw(12) << row.strategy
              << std::setw(42) << row.step
              << std::setw(10) << row.level
              << std::setw(16) << row.noiseScaleDeg
              << std::setw(12) << row.elements
              << std::setw(16) << std::scientific << row.timeSec
              << std::setw(16) << std::scientific << row.maxAbsErr
              << '\n';
}

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

std::vector<double> BuildRefCubicBlock() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(3.0 * x * x * x);
    }
    return ref;
}

std::vector<double> BuildRefLinearCubic() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(x + 3.0 * x * x * x);
    }
    return ref;
}

std::vector<double> BuildRefPoly() {
    // p(x) = 1 + x + 3*x^3.
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(1.0 + x + 3.0 * x * x * x);
    }
    return ref;
}

CC BuildCubicContext(ScalingTechnique scalTech) {
    CCParams<CryptoContextCKKSRNS> parameters;

    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(1 << 12);
    parameters.SetMultiplicativeDepth(6);
    parameters.SetBatchSize(kSlots);
    parameters.SetFirstModSize(60);
    parameters.SetScalingModSize(50);
    parameters.SetScalingTechnique(scalTech);

    // Cubic raw chains create a degree-3 secret-key intermediate.
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

void ReduceToLevel(CC cc,
                   Ciphertext<DCRTPoly>& ct,
                   size_t targetLevel) {
    const size_t currentLevel = ct->GetLevel();
    if (currentLevel > targetLevel) {
        throw std::runtime_error("ReduceToLevel: ciphertext is already deeper than target level");
    }
    if (currentLevel < targetLevel) {
        cc->LevelReduceInPlace(ct, nullptr, targetLevel - currentLevel);
    }
}

Ciphertext<DCRTPoly> BuildLinearForTarget(CC cc,
                                          const PrivateKey<DCRTPoly>& sk,
                                          const Ciphertext<DCRTPoly>& ct,
                                          size_t targetLevel,
                                          size_t targetNoiseScaleDeg,
                                          const std::string& strategy,
                                          std::vector<TraceRow>& rows,
                                          EvalStats& stats) {
    const auto refX = BuildRefPower(1);

    Ciphertext<DCRTPoly> ctLinear;
    if (targetNoiseScaleDeg == ct->GetNoiseScaleDeg()) {
        ctLinear = ct->Clone();
        rows.push_back(CaptureTrace(strategy, "Clone linear term", cc, sk, ctLinear, refX, 0.0));
    }
    else if (targetNoiseScaleDeg == ct->GetNoiseScaleDeg() + 1) {
        auto t0 = Clock::now();
        ctLinear = cc->EvalMult(ct, 1.0);
        auto t1 = Clock::now();
        ++stats.scalarMultCount;
        stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
        rows.push_back(CaptureTrace(
            strategy, "EvalMult(ct, 1.0) for scale match", cc, sk, ctLinear, refX,
            std::chrono::duration<double>(t1 - t0).count()));
    }
    else {
        throw std::runtime_error("BuildLinearForTarget: unsupported target noiseScaleDeg");
    }

    auto t2 = Clock::now();
    ReduceToLevel(cc, ctLinear, targetLevel);
    auto t3 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
    rows.push_back(CaptureTrace(
        strategy, "LevelReduce(linear -> cubic)", cc, sk, ctLinear, refX,
        std::chrono::duration<double>(t3 - t2).count()));

    return ctLinear;
}

Ciphertext<DCRTPoly> BuildMaterializedCubeBranch(CC cc,
                                                 const PrivateKey<DCRTPoly>& sk,
                                                 const Ciphertext<DCRTPoly>& ct,
                                                 const std::string& branch,
                                                 std::vector<TraceRow>& rows,
                                                 EvalStats& stats) {
    const auto refX2 = BuildRefPower(2);
    const auto refX3 = BuildRefPower(3);

    auto t0 = Clock::now();
    auto ctX2 = cc->EvalMultNoRelin(ct, ct);
    auto t1 = Clock::now();
    ++stats.tensorProductCount;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "naive", branch + ": x2 raw", cc, sk, ctX2, refX2,
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    cc->RelinearizeInPlace(ctX2);
    auto t3 = Clock::now();
    ++stats.relinCount;
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
    rows.push_back(CaptureTrace(
        "naive", branch + ": relin x2", cc, sk, ctX2, refX2,
        std::chrono::duration<double>(t3 - t2).count()));

    auto t4 = Clock::now();
    cc->RescaleInPlace(ctX2);
    auto t5 = Clock::now();
    ++stats.rescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(t5 - t4).count();
    rows.push_back(CaptureTrace(
        "naive", branch + ": rescale x2", cc, sk, ctX2, refX2,
        std::chrono::duration<double>(t5 - t4).count()));

    auto ctXAligned = ct->Clone();
    auto t6 = Clock::now();
    ReduceToLevel(cc, ctXAligned, ctX2->GetLevel());
    auto t7 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t7 - t6).count();
    rows.push_back(CaptureTrace(
        "naive", branch + ": level-reduce x", cc, sk, ctXAligned, BuildRefPower(1),
        std::chrono::duration<double>(t7 - t6).count()));

    auto t8 = Clock::now();
    auto ctX3 = cc->EvalMultNoRelin(ctX2, ctXAligned);
    auto t9 = Clock::now();
    ++stats.tensorProductCount;
    stats.totalOpSec += std::chrono::duration<double>(t9 - t8).count();
    rows.push_back(CaptureTrace(
        "naive", branch + ": x3 raw", cc, sk, ctX3, refX3,
        std::chrono::duration<double>(t9 - t8).count()));

    auto t10 = Clock::now();
    cc->RelinearizeInPlace(ctX3);
    auto t11 = Clock::now();
    ++stats.relinCount;
    stats.totalOpSec += std::chrono::duration<double>(t11 - t10).count();
    rows.push_back(CaptureTrace(
        "naive", branch + ": relin x3", cc, sk, ctX3, refX3,
        std::chrono::duration<double>(t11 - t10).count()));

    auto t12 = Clock::now();
    cc->RescaleInPlace(ctX3);
    auto t13 = Clock::now();
    ++stats.rescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(t13 - t12).count();
    rows.push_back(CaptureTrace(
        "naive", branch + ": rescale x3", cc, sk, ctX3, refX3,
        std::chrono::duration<double>(t13 - t12).count()));

    return ctX3;
}

Ciphertext<DCRTPoly> BuildRawCubeBranch(CC cc,
                                        const PrivateKey<DCRTPoly>& sk,
                                        const Ciphertext<DCRTPoly>& ct,
                                        const std::string& branch,
                                        std::vector<TraceRow>& rows,
                                        EvalStats& stats) {
    const auto refX2 = BuildRefPower(2);
    const auto refX3 = BuildRefPower(3);

    auto t0 = Clock::now();
    auto ctX2Raw = cc->EvalMultNoRelin(ct, ct);
    auto t1 = Clock::now();
    ++stats.tensorProductCount;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "lazy", branch + ": x2 raw", cc, sk, ctX2Raw, refX2,
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    auto ctX3Raw = cc->EvalMultNoRelin(ctX2Raw, ct);
    auto t3 = Clock::now();
    ++stats.tensorProductCount;
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
    rows.push_back(CaptureTrace(
        "lazy", branch + ": x3 raw", cc, sk, ctX3Raw, refX3,
        std::chrono::duration<double>(t3 - t2).count()));

    return ctX3Raw;
}

Ciphertext<DCRTPoly> EvaluateNaive(CC cc,
                                   const PrivateKey<DCRTPoly>& sk,
                                   const Ciphertext<DCRTPoly>& ct,
                                   std::vector<TraceRow>& rows,
                                   EvalStats& stats) {
    const auto refCubicBlock = BuildRefCubicBlock();
    const auto refLinearCubic = BuildRefLinearCubic();
    const auto refPoly = BuildRefPoly();

    auto ctA = BuildMaterializedCubeBranch(cc, sk, ct, "A", rows, stats);
    auto ctB = BuildMaterializedCubeBranch(cc, sk, ct, "B", rows, stats);
    auto ctC = BuildMaterializedCubeBranch(cc, sk, ct, "C", rows, stats);

    auto t0 = Clock::now();
    auto ctAB = cc->EvalAdd(ctA, ctB);
    auto ctCubic = cc->EvalAdd(ctAB, ctC);
    auto t1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "naive", "Fold materialized x3 block", cc, sk, ctCubic, refCubicBlock,
        std::chrono::duration<double>(t1 - t0).count()));

    auto ctLinear = BuildLinearForTarget(
        cc, sk, ct, ctCubic->GetLevel(), ctCubic->GetNoiseScaleDeg(), "naive", rows, stats);

    auto t2 = Clock::now();
    auto ctLinearCubic = cc->EvalAdd(ctCubic, ctLinear);
    auto t3 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
    rows.push_back(CaptureTrace(
        "naive", "EvalAdd(cubic, linear)", cc, sk, ctLinearCubic, refLinearCubic,
        std::chrono::duration<double>(t3 - t2).count()));

    auto t4 = Clock::now();
    auto ctOut = cc->EvalAdd(ctLinearCubic, 1.0);
    auto t5 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t5 - t4).count();
    rows.push_back(CaptureTrace(
        "naive", "EvalAdd(+ constant 1.0)", cc, sk, ctOut, refPoly,
        std::chrono::duration<double>(t5 - t4).count()));

    return ctOut;
}

Ciphertext<DCRTPoly> EvaluateLazy(CC cc,
                                  const PrivateKey<DCRTPoly>& sk,
                                  const Ciphertext<DCRTPoly>& ct,
                                  std::vector<TraceRow>& rows,
                                  EvalStats& stats) {
    const auto refCubicBlock = BuildRefCubicBlock();
    const auto refLinearCubic = BuildRefLinearCubic();
    const auto refPoly = BuildRefPoly();

    auto ctA = BuildRawCubeBranch(cc, sk, ct, "A", rows, stats);
    auto ctB = BuildRawCubeBranch(cc, sk, ct, "B", rows, stats);
    auto ctC = BuildRawCubeBranch(cc, sk, ct, "C", rows, stats);

    auto t0 = Clock::now();
    auto ctAB = cc->EvalAdd(ctA, ctB);
    auto ctCubic = cc->EvalAdd(ctAB, ctC);
    auto t1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "lazy", "Fold raw x3 block", cc, sk, ctCubic, refCubicBlock,
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    cc->RelinearizeInPlace(ctCubic);
    auto t3 = Clock::now();
    ++stats.relinCount;
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
    rows.push_back(CaptureTrace(
        "lazy", "RelinearizeInPlace(block)", cc, sk, ctCubic, refCubicBlock,
        std::chrono::duration<double>(t3 - t2).count()));

    auto t4 = Clock::now();
    cc->RescaleInPlace(ctCubic);
    auto t5 = Clock::now();
    ++stats.rescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(t5 - t4).count();
    rows.push_back(CaptureTrace(
        "lazy", "RescaleInPlace(block)", cc, sk, ctCubic, refCubicBlock,
        std::chrono::duration<double>(t5 - t4).count()));

    auto ctLinear = BuildLinearForTarget(
        cc, sk, ct, ctCubic->GetLevel(), ctCubic->GetNoiseScaleDeg(), "lazy", rows, stats);

    auto t6 = Clock::now();
    auto ctLinearCubic = cc->EvalAdd(ctCubic, ctLinear);
    auto t7 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t7 - t6).count();
    rows.push_back(CaptureTrace(
        "lazy", "EvalAdd(cubic, linear)", cc, sk, ctLinearCubic, refLinearCubic,
        std::chrono::duration<double>(t7 - t6).count()));

    auto t8 = Clock::now();
    auto ctOut = cc->EvalAdd(ctLinearCubic, 1.0);
    auto t9 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t9 - t8).count();
    rows.push_back(CaptureTrace(
        "lazy", "EvalAdd(+ constant 1.0)", cc, sk, ctOut, refPoly,
        std::chrono::duration<double>(t9 - t8).count()));

    return ctOut;
}

void PrintSummary(const std::string& label,
                  const Ciphertext<DCRTPoly>& ctOut,
                  double finalErr,
                  const EvalStats& stats) {
    std::cout << "  " << label << " final level          = " << ctOut->GetLevel() << '\n';
    std::cout << "  " << label << " final noiseScaleDeg  = " << ctOut->GetNoiseScaleDeg() << '\n';
    std::cout << "  " << label << " final ctElems        = " << ctOut->NumberCiphertextElements() << '\n';
    std::cout << "  " << label << " final max_abs_err    = " << std::scientific << finalErr << '\n';
    std::cout << "  " << label << " tensor products      = " << stats.tensorProductCount << '\n';
    std::cout << "  " << label << " relin count          = " << stats.relinCount << '\n';
    std::cout << "  " << label << " rescale count        = " << stats.rescaleCount << '\n';
    std::cout << "  " << label << " scalar mult count    = " << stats.scalarMultCount << '\n';
    std::cout << "  " << label << " total operator time  = " << std::scientific << stats.totalOpSec << " s\n";
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] cubic lazy-materialization compare\n";
    std::cout << "[POLY] p(x) = 1 + x + 3*x^3\n";
    std::cout << "[KEYGEN] SetMaxRelinSkDeg(3) + EvalMultKeysGen\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildCubicContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeysGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    const auto refInput = BuildRefPower(1);
    const auto refPoly = BuildRefPoly();

    std::vector<TraceRow> rows;
    rows.reserve(64);
    rows.push_back(CaptureTrace("both", "input", cc, kp.secretKey, ct, refInput, 0.0));

    EvalStats naiveStats;
    auto ctNaive = EvaluateNaive(cc, kp.secretKey, ct, rows, naiveStats);

    EvalStats lazyStats;
    auto ctLazy = EvaluateLazy(cc, kp.secretKey, ct, rows, lazyStats);

    auto decNaive = DecryptVec(cc, kp.secretKey, ctNaive, kSlots);
    auto decLazy = DecryptVec(cc, kp.secretKey, ctLazy, kSlots);
    const double naiveErr = MaxAbsErrReal(decNaive, refPoly);
    const double lazyErr = MaxAbsErrReal(decLazy, refPoly);

    std::cout << '\n';
    PrintTraceHeader();
    for (const auto& row : rows) {
        PrintTraceRow(row);
    }

    std::cout << "\nsummary:\n";
    PrintSummary("naive", ctNaive, naiveErr, naiveStats);
    PrintSummary("lazy ", ctLazy, lazyErr, lazyStats);

    std::cout << "\ncomparison:\n";
    std::cout << "  tensor products          = "
              << naiveStats.tensorProductCount << " vs " << lazyStats.tensorProductCount << '\n';
    std::cout << "  relin reduction          = "
              << naiveStats.relinCount << " -> " << lazyStats.relinCount << '\n';
    std::cout << "  rescale reduction        = "
              << naiveStats.rescaleCount << " -> " << lazyStats.rescaleCount << '\n';
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
