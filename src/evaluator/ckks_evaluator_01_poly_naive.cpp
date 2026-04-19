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
              << std::setw(30) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';

    std::cout << std::string(88, '-') << '\n';
}

void PrintMetricsRow(const StepMetrics& row) {
    std::cout << std::left
              << std::setw(30) << row.step
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

std::vector<double> BuildRefPoly() {
    // p(x) = 3 * x^2
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(3.0 * x * x);
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

Ciphertext<DCRTPoly> EvaluateSparseQuadraticNaive(
    CC cc,
    const PrivateKey<DCRTPoly>& sk,
    const Ciphertext<DCRTPoly>& ct,
    std::vector<StepMetrics>& rows) {
    const auto refSq = BuildRefSquare();
    const auto refPoly = BuildRefPoly();

    // term 1: x^2
    auto t0 = Clock::now();
    auto ct_a = cc->EvalMultNoRelin(ct, ct);
    auto t1 = Clock::now();
    rows.push_back(CaptureMetrics(
        "A: EvalMultNoRelin",
        cc, sk, ct_a, refSq,
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    cc->RelinearizeInPlace(ct_a);
    auto t3 = Clock::now();
    rows.push_back(CaptureMetrics(
        "A: RelinearizeInPlace",
        cc, sk, ct_a, refSq,
        std::chrono::duration<double>(t3 - t2).count()));

    auto t4 = Clock::now();
    cc->RescaleInPlace(ct_a);
    auto t5 = Clock::now();
    rows.push_back(CaptureMetrics(
        "A: RescaleInPlace",
        cc, sk, ct_a, refSq,
        std::chrono::duration<double>(t5 - t4).count()));

    // term 2: x^2
    auto t6 = Clock::now();
    auto ct_b = cc->EvalMultNoRelin(ct, ct);
    auto t7 = Clock::now();
    rows.push_back(CaptureMetrics(
        "B: EvalMultNoRelin",
        cc, sk, ct_b, refSq,
        std::chrono::duration<double>(t7 - t6).count()));

    auto t8 = Clock::now();
    cc->RelinearizeInPlace(ct_b);
    auto t9 = Clock::now();
    rows.push_back(CaptureMetrics(
        "B: RelinearizeInPlace",
        cc, sk, ct_b, refSq,
        std::chrono::duration<double>(t9 - t8).count()));

    auto t10 = Clock::now();
    cc->RescaleInPlace(ct_b);
    auto t11 = Clock::now();
    rows.push_back(CaptureMetrics(
        "B: RescaleInPlace",
        cc, sk, ct_b, refSq,
        std::chrono::duration<double>(t11 - t10).count()));

    // term 3: x^2
    auto t12 = Clock::now();
    auto ct_c = cc->EvalMultNoRelin(ct, ct);
    auto t13 = Clock::now();
    rows.push_back(CaptureMetrics(
        "C: EvalMultNoRelin",
        cc, sk, ct_c, refSq,
        std::chrono::duration<double>(t13 - t12).count()));

    auto t14 = Clock::now();
    cc->RelinearizeInPlace(ct_c);
    auto t15 = Clock::now();
    rows.push_back(CaptureMetrics(
        "C: RelinearizeInPlace",
        cc, sk, ct_c, refSq,
        std::chrono::duration<double>(t15 - t14).count()));

    auto t16 = Clock::now();
    cc->RescaleInPlace(ct_c);
    auto t17 = Clock::now();
    rows.push_back(CaptureMetrics(
        "C: RescaleInPlace",
        cc, sk, ct_c, refSq,
        std::chrono::duration<double>(t17 - t16).count()));

    // fold: ((A + B) + C)
    auto t18 = Clock::now();
    auto ct_ab = cc->EvalAdd(ct_a, ct_b);
    auto ct_abc = cc->EvalAdd(ct_ab, ct_c);
    auto t19 = Clock::now();
    rows.push_back(CaptureMetrics(
        "Fold: EvalAdd((A+B),C)",
        cc, sk, ct_abc, refPoly,
        std::chrono::duration<double>(t19 - t18).count()));

    return ct_abc;
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] naive sparse quadratic evaluator\n";
    std::cout << "[POLY] p(x) = 3*x^2\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    const auto refInput = kInput;
    const auto refPoly = BuildRefPoly();

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    std::vector<StepMetrics> rows;
    rows.reserve(11);

    rows.push_back(CaptureMetrics("input", cc, kp.secretKey, ct, refInput, 0.0));

    auto t0 = Clock::now();
    auto ct_out = EvaluateSparseQuadraticNaive(cc, kp.secretKey, ct, rows);
    auto t1 = Clock::now();

    std::cout << '\n';
    PrintMetricsHeader();
    for (const auto& row : rows) {
        PrintMetricsRow(row);
    }

    auto dec_out = DecryptVec(cc, kp.secretKey, ct_out, kSlots);
    double finalErr = MaxAbsErrReal(dec_out, refPoly);

    std::cout << "\nsummary:\n";
    std::cout << "  evaluator total time      = "
              << std::scientific
              << std::chrono::duration<double>(t1 - t0).count()
              << " s\n";
    std::cout << "  final level               = " << ct_out->GetLevel() << '\n';
    std::cout << "  final noiseScaleDeg       = " << ct_out->GetNoiseScaleDeg() << '\n';
    std::cout << "  final max_abs_err         = " << std::scientific << finalErr << '\n';
    std::cout << "  relin count               = 3\n";
    std::cout << "  rescale count             = 3\n";
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