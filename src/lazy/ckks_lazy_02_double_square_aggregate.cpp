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

struct StepMetrics {
    std::string step;
    size_t level;
    size_t noiseScaleDeg;
    double timeSec;
    double maxAbsErr;
};

void PrintMetricsHeader() {
    std::cout << std::left
              << std::setw(28) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';

    std::cout << std::string(86, '-') << '\n';
}

void PrintMetricsRow(const StepMetrics& row) {
    std::cout << std::left
              << std::setw(28) << row.step
              << std::setw(10) << row.level
              << std::setw(16) << row.noiseScaleDeg
              << std::setw(16) << std::scientific << row.timeSec
              << std::setw(16) << std::scientific << row.maxAbsErr
              << '\n';
}

std::vector<double> BuildRefSquare() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(x * x);
    }
    return ref;
}

std::vector<double> BuildRefDoubleSquare() {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(2.0 * x * x);
    }
    return ref;
}

StepMetrics CaptureMetrics(const std::string& step,
                           CC cc,
                           const PrivateKey<DCRTPoly>& sk,
                           const Ciphertext<DCRTPoly>& ct,
                           const std::vector<double>& ref,
                           double timeSec) {
    auto dec = DecryptVec(cc, sk, ct, kSlots);
    return StepMetrics{
        step,
        ct->GetLevel(),
        ct->GetNoiseScaleDeg(),
        timeSec,
        MaxAbsErrReal(dec, ref)
    };
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[LAZY] double-square aggregate with deferred materialization\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    const auto refSq = BuildRefSquare();
    const auto ref2Sq = BuildRefDoubleSquare();

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    std::vector<StepMetrics> rows;
    rows.reserve(7);

    rows.push_back(CaptureMetrics("input", cc, kp.secretKey, ct, kInput, 0.0));

    // Branch A raw
    auto t0 = Clock::now();
    auto ct_a_raw = cc->EvalMultNoRelin(ct, ct);
    auto t1 = Clock::now();
    rows.push_back(CaptureMetrics(
        "A: EvalMultNoRelin",
        cc,
        kp.secretKey,
        ct_a_raw,
        refSq,
        std::chrono::duration<double>(t1 - t0).count()));

    // Branch B raw
    auto t2 = Clock::now();
    auto ct_b_raw = cc->EvalMultNoRelin(ct, ct);
    auto t3 = Clock::now();
    rows.push_back(CaptureMetrics(
        "B: EvalMultNoRelin",
        cc,
        kp.secretKey,
        ct_b_raw,
        refSq,
        std::chrono::duration<double>(t3 - t2).count()));

    // Lazy hold before materialization
    rows.push_back(CaptureMetrics(
        "LazyHold(two raw squares)",
        cc,
        kp.secretKey,
        ct_a_raw,
        refSq,
        0.0));

    // First aggregate the two raw intermediates
    auto t4 = Clock::now();
    auto ct_sum_raw = cc->EvalAdd(ct_a_raw, ct_b_raw);
    auto t5 = Clock::now();
    rows.push_back(CaptureMetrics(
        "EvalAdd(raw A, raw B)",
        cc,
        kp.secretKey,
        ct_sum_raw,
        ref2Sq,
        std::chrono::duration<double>(t5 - t4).count()));

    // Then materialize once
    auto t6 = Clock::now();
    cc->RelinearizeInPlace(ct_sum_raw);
    auto t7 = Clock::now();
    rows.push_back(CaptureMetrics(
        "RelinearizeInPlace(sum)",
        cc,
        kp.secretKey,
        ct_sum_raw,
        ref2Sq,
        std::chrono::duration<double>(t7 - t6).count()));

    auto t8 = Clock::now();
    cc->RescaleInPlace(ct_sum_raw);
    auto t9 = Clock::now();
    rows.push_back(CaptureMetrics(
        "RescaleInPlace(sum)",
        cc,
        kp.secretKey,
        ct_sum_raw,
        ref2Sq,
        std::chrono::duration<double>(t9 - t8).count()));

    double totalTime =
        std::chrono::duration<double>(t1 - t0).count() +
        std::chrono::duration<double>(t3 - t2).count() +
        std::chrono::duration<double>(t5 - t4).count() +
        std::chrono::duration<double>(t7 - t6).count() +
        std::chrono::duration<double>(t9 - t8).count();

    std::cout << '\n';
    PrintMetricsHeader();
    for (const auto& row : rows) {
        PrintMetricsRow(row);
    }

    std::cout << "\nsummary:\n";
    std::cout << "  lazy aggregate total time  = " << std::scientific << totalTime << " s\n";
    std::cout << "  relin count                = 1\n";
    std::cout << "  rescale count              = 1\n";
    std::cout << "  final level                = " << ct_sum_raw->GetLevel() << '\n';
    std::cout << "  final noiseScaleDeg        = " << ct_sum_raw->GetNoiseScaleDeg() << '\n';
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