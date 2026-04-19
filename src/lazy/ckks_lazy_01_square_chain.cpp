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
              << std::setw(24) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';

    std::cout << std::string(82, '-') << '\n';
}

void PrintMetricsRow(const StepMetrics& row) {
    std::cout << std::left
              << std::setw(24) << row.step
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
    std::cout << "[LAZY] square chain with deferred materialization\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    const auto refSq = BuildRefSquare();

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    std::vector<StepMetrics> rows;
    rows.reserve(5);

    rows.push_back(CaptureMetrics("input", cc, kp.secretKey, ct, kInput, 0.0));

    // Step 1: 先做原始乘法，但不立即后处理
    auto t0 = Clock::now();
    auto ct_sq_raw = cc->EvalMultNoRelin(ct, ct);
    auto t1 = Clock::now();
    rows.push_back(CaptureMetrics(
        "EvalMultNoRelin",
        cc,
        kp.secretKey,
        ct_sq_raw,
        refSq,
        std::chrono::duration<double>(t1 - t0).count()));

    // Step 2: lazy hold，显式表示“先保留原始中间态”
    // 这里不做任何后处理，只记录当前状态
    rows.push_back(CaptureMetrics(
        "LazyHold(raw square)",
        cc,
        kp.secretKey,
        ct_sq_raw,
        refSq,
        0.0));

    // Step 3: 到 materialization 边界再做 relinearize
    auto t2 = Clock::now();
    cc->RelinearizeInPlace(ct_sq_raw);
    auto t3 = Clock::now();
    rows.push_back(CaptureMetrics(
        "RelinearizeInPlace",
        cc,
        kp.secretKey,
        ct_sq_raw,
        refSq,
        std::chrono::duration<double>(t3 - t2).count()));

    // Step 4: 到 materialization 边界再做 rescale
    auto t4 = Clock::now();
    cc->RescaleInPlace(ct_sq_raw);
    auto t5 = Clock::now();
    rows.push_back(CaptureMetrics(
        "RescaleInPlace",
        cc,
        kp.secretKey,
        ct_sq_raw,
        refSq,
        std::chrono::duration<double>(t5 - t4).count()));

    double totalTime =
        std::chrono::duration<double>(t1 - t0).count() +
        std::chrono::duration<double>(t3 - t2).count() +
        std::chrono::duration<double>(t5 - t4).count();

    std::cout << '\n';
    PrintMetricsHeader();
    for (const auto& row : rows) {
        PrintMetricsRow(row);
    }

    std::cout << "\nsummary:\n";
    std::cout << "  lazy chain total time  = " << std::scientific << totalTime << " s\n";
    std::cout << "  final level            = " << ct_sq_raw->GetLevel() << '\n';
    std::cout << "  final noiseScaleDeg    = " << ct_sq_raw->GetNoiseScaleDeg() << '\n';
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