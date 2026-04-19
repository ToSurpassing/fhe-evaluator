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
    std::string entry;
    std::string state;
    size_t level;
    size_t noiseScaleDeg;
    size_t elements;
    double timeSec;
    double maxAbsErr;
};

struct CacheStats {
    size_t tensorProducts = 0;
    size_t relinCount = 0;
    size_t rescaleCount = 0;
    size_t levelReduceCount = 0;
    double totalOpSec = 0.0;
};

void PrintTraceHeader() {
    std::cout << std::left
              << std::setw(14) << "strategy"
              << std::setw(10) << "entry"
              << std::setw(36) << "state"
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
              << std::setw(14) << row.strategy
              << std::setw(10) << row.entry
              << std::setw(36) << row.state
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

CC BuildPowerCacheContext(ScalingTechnique scalTech) {
    CCParams<CryptoContextCKKSRNS> parameters;

    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(1 << 12);
    parameters.SetMultiplicativeDepth(6);
    parameters.SetBatchSize(kSlots);
    parameters.SetFirstModSize(60);
    parameters.SetScalingModSize(50);
    parameters.SetScalingTechnique(scalTech);

    // The raw cache path keeps x^4 as a degree-4 secret-key intermediate.
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
                      const std::string& entry,
                      const std::string& state,
                      CC cc,
                      const PrivateKey<DCRTPoly>& sk,
                      const Ciphertext<DCRTPoly>& ct,
                      size_t refPower,
                      double timeSec) {
    std::vector<std::complex<double>> dec;
    try {
        dec = DecryptVec(cc, sk, ct, kSlots);
    }
    catch (const std::exception& e) {
        throw std::runtime_error("CaptureTrace failed at [" + strategy + "] " + entry + " / " + state + ": " +
                                 e.what());
    }

    return TraceRow{
        strategy,
        entry,
        state,
        ct->GetLevel(),
        ct->GetNoiseScaleDeg(),
        ct->NumberCiphertextElements(),
        timeSec,
        MaxAbsErrReal(dec, BuildRefPower(refPower))
    };
}

void ReduceToLevel(CC cc,
                   Ciphertext<DCRTPoly>& ct,
                   size_t targetLevel,
                   CacheStats& stats) {
    const size_t currentLevel = ct->GetLevel();
    if (currentLevel > targetLevel) {
        throw std::runtime_error("ReduceToLevel: ciphertext is already deeper than target level");
    }
    if (currentLevel < targetLevel) {
        auto t0 = Clock::now();
        cc->LevelReduceInPlace(ct, nullptr, targetLevel - currentLevel);
        auto t1 = Clock::now();
        ++stats.levelReduceCount;
        stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    }
}

Ciphertext<DCRTPoly> BuildEagerNextPower(CC cc,
                                         const PrivateKey<DCRTPoly>& sk,
                                         const Ciphertext<DCRTPoly>& prevPower,
                                         size_t prevPowerDegree,
                                         const Ciphertext<DCRTPoly>& ctX,
                                         std::vector<TraceRow>& rows,
                                         CacheStats& stats) {
    auto xAligned = ctX->Clone();
    ReduceToLevel(cc, xAligned, prevPower->GetLevel(), stats);
    rows.push_back(CaptureTrace(
        "eager", "x", "prepared for x^" + std::to_string(prevPowerDegree + 1),
        cc, sk, xAligned, 1, 0.0));

    auto t0 = Clock::now();
    auto rawNext = cc->EvalMultNoRelin(prevPower, xAligned);
    auto t1 = Clock::now();
    ++stats.tensorProducts;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "eager", "x^" + std::to_string(prevPowerDegree + 1), "raw multiply",
        cc, sk, rawNext, prevPowerDegree + 1,
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    cc->RelinearizeInPlace(rawNext);
    auto t3 = Clock::now();
    ++stats.relinCount;
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
    rows.push_back(CaptureTrace(
        "eager", "x^" + std::to_string(prevPowerDegree + 1), "after relin",
        cc, sk, rawNext, prevPowerDegree + 1,
        std::chrono::duration<double>(t3 - t2).count()));

    auto t4 = Clock::now();
    cc->RescaleInPlace(rawNext);
    auto t5 = Clock::now();
    ++stats.rescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(t5 - t4).count();
    rows.push_back(CaptureTrace(
        "eager", "x^" + std::to_string(prevPowerDegree + 1), "after rescale/materialized",
        cc, sk, rawNext, prevPowerDegree + 1,
        std::chrono::duration<double>(t5 - t4).count()));

    return rawNext;
}

void BuildEagerCache(CC cc,
                     const PrivateKey<DCRTPoly>& sk,
                     const Ciphertext<DCRTPoly>& ctX,
                     std::vector<TraceRow>& rows,
                     CacheStats& stats) {
    rows.push_back(CaptureTrace("eager", "x", "input/materialized", cc, sk, ctX, 1, 0.0));

    auto x2 = BuildEagerNextPower(cc, sk, ctX, 1, ctX, rows, stats);
    auto x3 = BuildEagerNextPower(cc, sk, x2, 2, ctX, rows, stats);
    (void)BuildEagerNextPower(cc, sk, x3, 3, ctX, rows, stats);
}

Ciphertext<DCRTPoly> BuildRawNextPower(CC cc,
                                       const PrivateKey<DCRTPoly>& sk,
                                       const Ciphertext<DCRTPoly>& prevRaw,
                                       size_t prevPowerDegree,
                                       const Ciphertext<DCRTPoly>& ctX,
                                       std::vector<TraceRow>& rows,
                                       CacheStats& stats) {
    auto t0 = Clock::now();
    auto rawNext = cc->EvalMultNoRelin(prevRaw, ctX);
    auto t1 = Clock::now();
    ++stats.tensorProducts;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "raw-chain", "x^" + std::to_string(prevPowerDegree + 1), "raw retained",
        cc, sk, rawNext, prevPowerDegree + 1,
        std::chrono::duration<double>(t1 - t0).count()));

    return rawNext;
}

void BuildRawChainCache(CC cc,
                        const PrivateKey<DCRTPoly>& sk,
                        const Ciphertext<DCRTPoly>& ctX,
                        std::vector<TraceRow>& rows,
                        CacheStats& stats) {
    rows.push_back(CaptureTrace("raw-chain", "x", "input/materialized", cc, sk, ctX, 1, 0.0));

    auto x2Raw = BuildRawNextPower(cc, sk, ctX, 1, ctX, rows, stats);
    auto x3Raw = BuildRawNextPower(cc, sk, x2Raw, 2, ctX, rows, stats);
    auto x4Raw = BuildRawNextPower(cc, sk, x3Raw, 3, ctX, rows, stats);

    std::cout << "[raw-chain] materializing only x^4 boundary: elements="
              << x4Raw->NumberCiphertextElements()
              << ", level=" << x4Raw->GetLevel()
              << ", noiseScaleDeg=" << x4Raw->GetNoiseScaleDeg()
              << '\n';

    auto t0 = Clock::now();
    cc->RelinearizeInPlace(x4Raw);
    auto t1 = Clock::now();
    ++stats.relinCount;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(CaptureTrace(
        "raw-chain", "x^4", "after relin boundary",
        cc, sk, x4Raw, 4,
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    cc->RescaleInPlace(x4Raw);
    auto t3 = Clock::now();
    ++stats.rescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
    rows.push_back(CaptureTrace(
        "raw-chain", "x^4", "after rescale/materialized",
        cc, sk, x4Raw, 4,
        std::chrono::duration<double>(t3 - t2).count()));
}

void PrintSummary(const std::string& label,
                  const CacheStats& stats) {
    std::cout << "  " << label << " tensor products     = " << stats.tensorProducts << '\n';
    std::cout << "  " << label << " relin count         = " << stats.relinCount << '\n';
    std::cout << "  " << label << " rescale count       = " << stats.rescaleCount << '\n';
    std::cout << "  " << label << " level-align attempts = " << stats.levelReduceCount << '\n';
    std::cout << "  " << label << " total operator time = " << std::scientific << stats.totalOpSec << " s\n";
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] power cache trace\n";
    std::cout << "[RANGE] x, x^2, x^3, x^4\n";
    std::cout << "[KEYGEN] SetMaxRelinSkDeg(4) + EvalMultKeysGen\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildPowerCacheContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeysGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    std::vector<TraceRow> rows;
    rows.reserve(24);

    CacheStats eagerStats;
    BuildEagerCache(cc, kp.secretKey, ct, rows, eagerStats);

    CacheStats rawStats;
    BuildRawChainCache(cc, kp.secretKey, ct, rows, rawStats);

    std::cout << '\n';
    PrintTraceHeader();
    for (const auto& row : rows) {
        PrintTraceRow(row);
    }

    std::cout << "\nsummary:\n";
    PrintSummary("eager    ", eagerStats);
    PrintSummary("raw-chain", rawStats);

    std::cout << "\ncomparison:\n";
    std::cout << "  tensor products          = "
              << eagerStats.tensorProducts << " vs " << rawStats.tensorProducts << '\n';
    std::cout << "  materialized powers      = eager x^2,x^3,x^4 vs raw-chain x^4 only\n";
    std::cout << "  relin reduction          = "
              << eagerStats.relinCount << " -> " << rawStats.relinCount << '\n';
    std::cout << "  rescale reduction        = "
              << eagerStats.rescaleCount << " -> " << rawStats.rescaleCount << '\n';
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
