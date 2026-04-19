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
    double timeSec;
    double maxAbsErr;
};

struct EvalStats {
    double totalOpSec = 0.0;
    size_t relinCount = 0;
    size_t rescaleCount = 0;
    size_t scalarRescaleCount = 0;
};

void PrintTraceHeader() {
    std::cout << std::left
              << std::setw(12) << "strategy"
              << std::setw(34) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';

    std::cout << std::string(104, '-') << '\n';
}

void PrintTraceRow(const TraceRow& row) {
    std::cout << std::left
              << std::setw(12) << row.strategy
              << std::setw(34) << row.step
              << std::setw(10) << row.level
              << std::setw(16) << row.noiseScaleDeg
              << std::setw(16) << std::scientific << row.timeSec
              << std::setw(16) << std::scientific << row.maxAbsErr
              << '\n';
}

std::vector<double> BuildRefInput() {
    return kInput;
}

std::vector<double> BuildRefSquare() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(x * x);
    }
    return ref;
}

std::vector<double> BuildRefQuadraticBlock() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(3.0 * x * x);
    }
    return ref;
}

std::vector<double> BuildRefLinear() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(0.5 * x);
    }
    return ref;
}

std::vector<double> BuildRefQuadraticPlusLinear() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(3.0 * x * x + 0.5 * x);
    }
    return ref;
}

std::vector<double> BuildRefPoly() {
    // p(x) = 1 + 0.5*x + 3*x^2.
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(1.0 + 0.5 * x + 3.0 * x * x);
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

Ciphertext<DCRTPoly> BuildMaterializedSquare(CC cc,
                                             const PrivateKey<DCRTPoly>& sk,
                                             const Ciphertext<DCRTPoly>& ct,
                                             const std::string& branch,
                                             std::vector<TraceRow>& rows,
                                             EvalStats& stats) {
    const auto refSq = BuildRefSquare();

    auto t0 = Clock::now();
    auto ctSq = cc->EvalMultNoRelin(ct, ct);
    auto t1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "naive", branch + ": EvalMultNoRelin", cc, sk, ctSq, refSq,
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    cc->RelinearizeInPlace(ctSq);
    auto t3 = Clock::now();
    ++stats.relinCount;
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
    rows.push_back(CaptureTrace(
        "naive", branch + ": RelinearizeInPlace", cc, sk, ctSq, refSq,
        std::chrono::duration<double>(t3 - t2).count()));

    auto t4 = Clock::now();
    cc->RescaleInPlace(ctSq);
    auto t5 = Clock::now();
    ++stats.rescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(t5 - t4).count();
    rows.push_back(CaptureTrace(
        "naive", branch + ": RescaleInPlace", cc, sk, ctSq, refSq,
        std::chrono::duration<double>(t5 - t4).count()));

    return ctSq;
}

Ciphertext<DCRTPoly> EvaluateNaive(CC cc,
                                   const PrivateKey<DCRTPoly>& sk,
                                   const Ciphertext<DCRTPoly>& ct,
                                   std::vector<TraceRow>& rows,
                                   EvalStats& stats) {
    const auto refQuadratic = BuildRefQuadraticBlock();
    const auto refLinear = BuildRefLinear();
    const auto refQuadLinear = BuildRefQuadraticPlusLinear();
    const auto refPoly = BuildRefPoly();

    auto ctA = BuildMaterializedSquare(cc, sk, ct, "A", rows, stats);
    auto ctB = BuildMaterializedSquare(cc, sk, ct, "B", rows, stats);
    auto ctC = BuildMaterializedSquare(cc, sk, ct, "C", rows, stats);

    auto t0 = Clock::now();
    auto ctAB = cc->EvalAdd(ctA, ctB);
    auto ctQuad = cc->EvalAdd(ctAB, ctC);
    auto t1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "naive", "Fold materialized x^2 block", cc, sk, ctQuad, refQuadratic,
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    auto ctLinear = cc->EvalMult(ct, 0.5);
    auto t3 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
    rows.push_back(CaptureTrace(
        "naive", "EvalMult(ct, 0.5)", cc, sk, ctLinear, refLinear,
        std::chrono::duration<double>(t3 - t2).count()));

    auto tLinearRescale0 = Clock::now();
    cc->RescaleInPlace(ctLinear);
    auto tLinearRescale1 = Clock::now();
    ++stats.scalarRescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(tLinearRescale1 - tLinearRescale0).count();
    rows.push_back(CaptureTrace(
        "naive", "RescaleInPlace(linear)", cc, sk, ctLinear, refLinear,
        std::chrono::duration<double>(tLinearRescale1 - tLinearRescale0).count()));

    auto tLevel0 = Clock::now();
    ReduceToLevel(cc, ctLinear, ctQuad->GetLevel());
    auto tLevel1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(tLevel1 - tLevel0).count();
    rows.push_back(CaptureTrace(
        "naive", "LevelReduce(linear -> quadratic)", cc, sk, ctLinear, refLinear,
        std::chrono::duration<double>(tLevel1 - tLevel0).count()));

    auto t4 = Clock::now();
    auto ctQuadLinear = cc->EvalAdd(ctQuad, ctLinear);
    auto t5 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t5 - t4).count();
    rows.push_back(CaptureTrace(
        "naive", "EvalAdd(quadratic, linear)", cc, sk, ctQuadLinear, refQuadLinear,
        std::chrono::duration<double>(t5 - t4).count()));

    auto t6 = Clock::now();
    auto ctOut = cc->EvalAdd(ctQuadLinear, 1.0);
    auto t7 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t7 - t6).count();
    rows.push_back(CaptureTrace(
        "naive", "EvalAdd(+ constant 1.0)", cc, sk, ctOut, refPoly,
        std::chrono::duration<double>(t7 - t6).count()));

    return ctOut;
}

Ciphertext<DCRTPoly> EvaluateLazy(CC cc,
                                  const PrivateKey<DCRTPoly>& sk,
                                  const Ciphertext<DCRTPoly>& ct,
                                  std::vector<TraceRow>& rows,
                                  EvalStats& stats) {
    const auto refSq = BuildRefSquare();
    const auto refQuadratic = BuildRefQuadraticBlock();
    const auto refLinear = BuildRefLinear();
    const auto refQuadLinear = BuildRefQuadraticPlusLinear();
    const auto refPoly = BuildRefPoly();

    auto t0 = Clock::now();
    auto ctA = cc->EvalMultNoRelin(ct, ct);
    auto t1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "lazy", "A: EvalMultNoRelin", cc, sk, ctA, refSq,
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    auto ctB = cc->EvalMultNoRelin(ct, ct);
    auto t3 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
    rows.push_back(CaptureTrace(
        "lazy", "B: EvalMultNoRelin", cc, sk, ctB, refSq,
        std::chrono::duration<double>(t3 - t2).count()));

    auto t4 = Clock::now();
    auto ctC = cc->EvalMultNoRelin(ct, ct);
    auto t5 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t5 - t4).count();
    rows.push_back(CaptureTrace(
        "lazy", "C: EvalMultNoRelin", cc, sk, ctC, refSq,
        std::chrono::duration<double>(t5 - t4).count()));

    auto t6 = Clock::now();
    auto ctAB = cc->EvalAdd(ctA, ctB);
    auto ctQuad = cc->EvalAdd(ctAB, ctC);
    auto t7 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t7 - t6).count();
    rows.push_back(CaptureTrace(
        "lazy", "Fold raw x^2 block", cc, sk, ctQuad, refQuadratic,
        std::chrono::duration<double>(t7 - t6).count()));

    auto t8 = Clock::now();
    cc->RelinearizeInPlace(ctQuad);
    auto t9 = Clock::now();
    ++stats.relinCount;
    stats.totalOpSec += std::chrono::duration<double>(t9 - t8).count();
    rows.push_back(CaptureTrace(
        "lazy", "RelinearizeInPlace(block)", cc, sk, ctQuad, refQuadratic,
        std::chrono::duration<double>(t9 - t8).count()));

    auto t10 = Clock::now();
    cc->RescaleInPlace(ctQuad);
    auto t11 = Clock::now();
    ++stats.rescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(t11 - t10).count();
    rows.push_back(CaptureTrace(
        "lazy", "RescaleInPlace(block)", cc, sk, ctQuad, refQuadratic,
        std::chrono::duration<double>(t11 - t10).count()));

    auto t12 = Clock::now();
    auto ctLinear = cc->EvalMult(ct, 0.5);
    auto t13 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t13 - t12).count();
    rows.push_back(CaptureTrace(
        "lazy", "EvalMult(ct, 0.5)", cc, sk, ctLinear, refLinear,
        std::chrono::duration<double>(t13 - t12).count()));

    auto tLinearRescale0 = Clock::now();
    cc->RescaleInPlace(ctLinear);
    auto tLinearRescale1 = Clock::now();
    ++stats.scalarRescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(tLinearRescale1 - tLinearRescale0).count();
    rows.push_back(CaptureTrace(
        "lazy", "RescaleInPlace(linear)", cc, sk, ctLinear, refLinear,
        std::chrono::duration<double>(tLinearRescale1 - tLinearRescale0).count()));

    auto tLevel0 = Clock::now();
    ReduceToLevel(cc, ctLinear, ctQuad->GetLevel());
    auto tLevel1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(tLevel1 - tLevel0).count();
    rows.push_back(CaptureTrace(
        "lazy", "LevelReduce(linear -> quadratic)", cc, sk, ctLinear, refLinear,
        std::chrono::duration<double>(tLevel1 - tLevel0).count()));

    auto t14 = Clock::now();
    auto ctQuadLinear = cc->EvalAdd(ctQuad, ctLinear);
    auto t15 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t15 - t14).count();
    rows.push_back(CaptureTrace(
        "lazy", "EvalAdd(quadratic, linear)", cc, sk, ctQuadLinear, refQuadLinear,
        std::chrono::duration<double>(t15 - t14).count()));

    auto t16 = Clock::now();
    auto ctOut = cc->EvalAdd(ctQuadLinear, 1.0);
    auto t17 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t17 - t16).count();
    rows.push_back(CaptureTrace(
        "lazy", "EvalAdd(+ constant 1.0)", cc, sk, ctOut, refPoly,
        std::chrono::duration<double>(t17 - t16).count()));

    return ctOut;
}

void PrintSummary(const std::string& label,
                  const Ciphertext<DCRTPoly>& ctOut,
                  double finalErr,
                  const EvalStats& stats) {
    std::cout << "  " << label << " final level          = " << ctOut->GetLevel() << '\n';
    std::cout << "  " << label << " final noiseScaleDeg  = " << ctOut->GetNoiseScaleDeg() << '\n';
    std::cout << "  " << label << " final max_abs_err    = " << std::scientific << finalErr << '\n';
    std::cout << "  " << label << " relin count          = " << stats.relinCount << '\n';
    std::cout << "  " << label << " quadratic rescale ct = " << stats.rescaleCount << '\n';
    std::cout << "  " << label << " scalar rescale count = " << stats.scalarRescaleCount << '\n';
    std::cout << "  " << label << " total operator time  = " << std::scientific << stats.totalOpSec << " s\n";
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] affine quadratic compare\n";
    std::cout << "[POLY] p(x) = 1 + 0.5*x + 3*x^2\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    const auto refInput = BuildRefInput();
    const auto refPoly = BuildRefPoly();

    std::vector<TraceRow> rows;
    rows.reserve(32);
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
    std::cout << "  relin reduction          = "
              << naiveStats.relinCount << " -> " << lazyStats.relinCount << '\n';
    std::cout << "  rescale reduction        = "
              << naiveStats.rescaleCount << " -> " << lazyStats.rescaleCount << '\n';
    std::cout << "  scalar rescale common    = "
              << naiveStats.scalarRescaleCount << " vs " << lazyStats.scalarRescaleCount << '\n';
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
