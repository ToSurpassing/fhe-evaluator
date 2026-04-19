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

struct PowerEntry {
    std::string strategy;
    size_t exponent;
    std::string construction;
    bool materialized;
    size_t level;
    size_t noiseScaleDeg;
    size_t elements;
    double maxAbsErr;
};

struct BuildStats {
    size_t tensorProducts = 0;
    size_t relinCount = 0;
    size_t rescaleCount = 0;
    size_t levelAlignCount = 0;
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

CC BuildBabyStepContext(ScalingTechnique scalTech) {
    CCParams<CryptoContextCKKSRNS> parameters;

    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(1 << 12);
    parameters.SetMultiplicativeDepth(6);
    parameters.SetBatchSize(kSlots);
    parameters.SetFirstModSize(60);
    parameters.SetScalingModSize(50);
    parameters.SetScalingTechnique(scalTech);

    // This block keeps x^4 raw in the lazy path, so it needs s^2..s^4 keys.
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

PowerEntry CaptureEntry(const std::string& strategy,
                        size_t exponent,
                        const std::string& construction,
                        bool materialized,
                        CC cc,
                        const PrivateKey<DCRTPoly>& sk,
                        const Ciphertext<DCRTPoly>& ct) {
    std::vector<std::complex<double>> dec;
    try {
        dec = DecryptVec(cc, sk, ct, kSlots);
    }
    catch (const std::exception& e) {
        throw std::runtime_error("CaptureEntry failed at [" + strategy + "] x^" + std::to_string(exponent) +
                                 " / " + construction + ": " + e.what());
    }

    return PowerEntry{
        strategy,
        exponent,
        construction,
        materialized,
        ct->GetLevel(),
        ct->GetNoiseScaleDeg(),
        ct->NumberCiphertextElements(),
        MaxAbsErrReal(dec, BuildRefPower(exponent))
    };
}

void PrintEntryHeader() {
    std::cout << std::left
              << std::setw(14) << "strategy"
              << std::setw(8) << "power"
              << std::setw(34) << "construction"
              << std::setw(14) << "materialized"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(12) << "ctElems"
              << std::setw(16) << "max_abs_err"
              << '\n';

    std::cout << std::string(124, '-') << '\n';
}

void PrintEntry(const PowerEntry& entry) {
    std::cout << std::left
              << std::setw(14) << entry.strategy
              << std::setw(8) << ("x^" + std::to_string(entry.exponent))
              << std::setw(34) << entry.construction
              << std::setw(14) << (entry.materialized ? "yes" : "no")
              << std::setw(10) << entry.level
              << std::setw(16) << entry.noiseScaleDeg
              << std::setw(12) << entry.elements
              << std::setw(16) << std::scientific << entry.maxAbsErr
              << '\n';
}

void PrintStats(const std::string& label,
                const BuildStats& stats) {
    std::cout << "  " << label << " tensor products    = " << stats.tensorProducts << '\n';
    std::cout << "  " << label << " relin count        = " << stats.relinCount << '\n';
    std::cout << "  " << label << " rescale count      = " << stats.rescaleCount << '\n';
    std::cout << "  " << label << " level-align count  = " << stats.levelAlignCount << '\n';
    std::cout << "  " << label << " total operator sec = " << std::scientific << stats.totalOpSec << '\n';
}

void ReduceToLevel(CC cc,
                   Ciphertext<DCRTPoly>& ct,
                   size_t targetLevel,
                   BuildStats& stats) {
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

Ciphertext<DCRTPoly> BuildEagerNext(CC cc,
                                    const PrivateKey<DCRTPoly>& sk,
                                    const Ciphertext<DCRTPoly>& prev,
                                    size_t prevExponent,
                                    const Ciphertext<DCRTPoly>& x,
                                    std::vector<PowerEntry>& entries,
                                    BuildStats& stats) {
    auto xAligned = x->Clone();
    ReduceToLevel(cc, xAligned, prev->GetLevel(), stats);

    auto t0 = Clock::now();
    auto raw = cc->EvalMultNoRelin(prev, xAligned);
    auto t1 = Clock::now();
    ++stats.tensorProducts;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();

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

    const size_t exponent = prevExponent + 1;
    entries.push_back(CaptureEntry(
        "eager", exponent, "multiply x then materialize", true, cc, sk, raw));

    return raw;
}

std::vector<PowerEntry> BuildEagerBlock(CC cc,
                                        const PrivateKey<DCRTPoly>& sk,
                                        const Ciphertext<DCRTPoly>& x,
                                        BuildStats& stats) {
    std::vector<PowerEntry> entries;
    entries.reserve(4);
    entries.push_back(CaptureEntry("eager", 1, "input", true, cc, sk, x));

    auto x2 = BuildEagerNext(cc, sk, x, 1, x, entries, stats);
    auto x3 = BuildEagerNext(cc, sk, x2, 2, x, entries, stats);
    (void)BuildEagerNext(cc, sk, x3, 3, x, entries, stats);

    return entries;
}

Ciphertext<DCRTPoly> BuildRawNext(CC cc,
                                  const PrivateKey<DCRTPoly>& sk,
                                  const Ciphertext<DCRTPoly>& prev,
                                  size_t prevExponent,
                                  const Ciphertext<DCRTPoly>& x,
                                  std::vector<PowerEntry>& entries,
                                  BuildStats& stats) {
    auto t0 = Clock::now();
    auto raw = cc->EvalMultNoRelin(prev, x);
    auto t1 = Clock::now();
    ++stats.tensorProducts;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();

    const size_t exponent = prevExponent + 1;
    entries.push_back(CaptureEntry(
        "raw-block", exponent, "raw retained", false, cc, sk, raw));

    return raw;
}

std::vector<PowerEntry> BuildRawBlock(CC cc,
                                      const PrivateKey<DCRTPoly>& sk,
                                      const Ciphertext<DCRTPoly>& x,
                                      BuildStats& stats) {
    std::vector<PowerEntry> entries;
    entries.reserve(5);
    entries.push_back(CaptureEntry("raw-block", 1, "input", true, cc, sk, x));

    auto x2 = BuildRawNext(cc, sk, x, 1, x, entries, stats);
    auto x3 = BuildRawNext(cc, sk, x2, 2, x, entries, stats);
    auto x4 = BuildRawNext(cc, sk, x3, 3, x, entries, stats);

    auto t0 = Clock::now();
    cc->RelinearizeInPlace(x4);
    auto t1 = Clock::now();
    ++stats.relinCount;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();

    auto t2 = Clock::now();
    cc->RescaleInPlace(x4);
    auto t3 = Clock::now();
    ++stats.rescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();

    entries.push_back(CaptureEntry(
        "raw-block", 4, "boundary materialized", true, cc, sk, x4));

    return entries;
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] baby-step block trace\n";
    std::cout << "[BLOCK] S = {x, x^2, x^3, x^4}\n";
    std::cout << "[KEYGEN] SetMaxRelinSkDeg(4) + EvalMultKeysGen\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildBabyStepContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeysGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    BuildStats eagerStats;
    const auto eagerEntries = BuildEagerBlock(cc, kp.secretKey, ct, eagerStats);

    BuildStats rawStats;
    const auto rawEntries = BuildRawBlock(cc, kp.secretKey, ct, rawStats);

    std::cout << '\n';
    PrintEntryHeader();
    for (const auto& entry : eagerEntries) {
        PrintEntry(entry);
    }
    for (const auto& entry : rawEntries) {
        PrintEntry(entry);
    }

    std::cout << "\nsummary:\n";
    PrintStats("eager    ", eagerStats);
    PrintStats("raw-block", rawStats);

    std::cout << "\ncomparison:\n";
    std::cout << "  tensor products          = "
              << eagerStats.tensorProducts << " vs " << rawStats.tensorProducts << '\n';
    std::cout << "  materialized entries     = eager x,x^2,x^3,x^4 vs raw x and boundary x^4\n";
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
