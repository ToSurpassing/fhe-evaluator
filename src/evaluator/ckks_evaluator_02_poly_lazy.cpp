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

struct CheckpointRow {
    std::string step;
    size_t level;
    size_t noiseScaleDeg;
    double maxAbsErr;
};

struct OperatorTimes {
    double multTotalSec   = 0.0;
    double foldSec        = 0.0;
    double relinSec       = 0.0;
    double rescaleSec     = 0.0;
    double totalOpSec     = 0.0;
};

void PrintCheckpointHeader() {
    std::cout << std::left
              << std::setw(28) << "checkpoint"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(16) << "max_abs_err"
              << '\n';

    std::cout << std::string(70, '-') << '\n';
}

void PrintCheckpointRow(const CheckpointRow& row) {
    std::cout << std::left
              << std::setw(28) << row.step
              << std::setw(10) << row.level
              << std::setw(16) << row.noiseScaleDeg
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

CheckpointRow CaptureCheckpoint(const std::string& step,
                                CC cc,
                                const PrivateKey<DCRTPoly>& sk,
                                const Ciphertext<DCRTPoly>& ct,
                                const std::vector<double>& ref) {
    auto dec = DecryptVec(cc, sk, ct, kSlots);
    return CheckpointRow{
        step,
        ct->GetLevel(),
        ct->GetNoiseScaleDeg(),
        MaxAbsErrReal(dec, ref)
    };
}

Ciphertext<DCRTPoly> EvaluateSparseQuadraticLazy(
    CC cc,
    const PrivateKey<DCRTPoly>& sk,
    const Ciphertext<DCRTPoly>& ct,
    std::vector<CheckpointRow>& checkpoints,
    OperatorTimes& opTimes) {

    const auto refSq   = BuildRefSquare();
    const auto refPoly = BuildRefPoly();

    // Three raw x^2 branches
    auto t0 = Clock::now();
    auto ct_a_raw = cc->EvalMultNoRelin(ct, ct);
    auto t1 = Clock::now();

    auto t2 = Clock::now();
    auto ct_b_raw = cc->EvalMultNoRelin(ct, ct);
    auto t3 = Clock::now();

    auto t4 = Clock::now();
    auto ct_c_raw = cc->EvalMultNoRelin(ct, ct);
    auto t5 = Clock::now();

    opTimes.multTotalSec =
        std::chrono::duration<double>(t1 - t0).count() +
        std::chrono::duration<double>(t3 - t2).count() +
        std::chrono::duration<double>(t5 - t4).count();

    // Fold raw block first
    auto t6 = Clock::now();
    auto ct_ab_raw  = cc->EvalAdd(ct_a_raw, ct_b_raw);
    auto ct_abc_raw = cc->EvalAdd(ct_ab_raw, ct_c_raw);
    auto t7 = Clock::now();
    opTimes.foldSec = std::chrono::duration<double>(t7 - t6).count();

    checkpoints.push_back(CaptureCheckpoint(
        "raw folded block", cc, sk, ct_abc_raw, refPoly));

    // Single materialization at block boundary
    auto t8 = Clock::now();
    cc->RelinearizeInPlace(ct_abc_raw);
    auto t9 = Clock::now();
    opTimes.relinSec = std::chrono::duration<double>(t9 - t8).count();

    checkpoints.push_back(CaptureCheckpoint(
        "after relin(block)", cc, sk, ct_abc_raw, refPoly));

    auto t10 = Clock::now();
    cc->RescaleInPlace(ct_abc_raw);
    auto t11 = Clock::now();
    opTimes.rescaleSec = std::chrono::duration<double>(t11 - t10).count();

    checkpoints.push_back(CaptureCheckpoint(
        "after rescale(block)", cc, sk, ct_abc_raw, refPoly));

    opTimes.totalOpSec =
        opTimes.multTotalSec +
        opTimes.foldSec +
        opTimes.relinSec +
        opTimes.rescaleSec;

    return ct_abc_raw;
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] lazy sparse quadratic evaluator\n";
    std::cout << "[POLY] p(x) = 3*x^2\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    const auto refInput = kInput;
    const auto refPoly  = BuildRefPoly();

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    std::vector<CheckpointRow> checkpoints;
    checkpoints.reserve(4);

    checkpoints.push_back(CaptureCheckpoint("input", cc, kp.secretKey, ct, refInput));

    OperatorTimes opTimes;
    auto ct_out = EvaluateSparseQuadraticLazy(cc, kp.secretKey, ct, checkpoints, opTimes);

    auto dec_out  = DecryptVec(cc, kp.secretKey, ct_out, kSlots);
    double finalErr = MaxAbsErrReal(dec_out, refPoly);

    std::cout << '\n';
    PrintCheckpointHeader();
    for (const auto& row : checkpoints) {
        PrintCheckpointRow(row);
    }

    std::cout << "\noperator timing summary:\n";
    std::cout << "  EvalMultNoRelin total      = " << std::scientific << opTimes.multTotalSec << " s\n";
    std::cout << "  Fold raw block total       = " << std::scientific << opTimes.foldSec << " s\n";
    std::cout << "  RelinearizeInPlace total   = " << std::scientific << opTimes.relinSec << " s\n";
    std::cout << "  RescaleInPlace total       = " << std::scientific << opTimes.rescaleSec << " s\n";
    std::cout << "  total operator time        = " << std::scientific << opTimes.totalOpSec << " s\n";

    std::cout << "\nsummary:\n";
    std::cout << "  final level               = " << ct_out->GetLevel() << '\n';
    std::cout << "  final noiseScaleDeg       = " << ct_out->GetNoiseScaleDeg() << '\n';
    std::cout << "  final max_abs_err         = " << std::scientific << finalErr << '\n';
    std::cout << "  relin count               = 1\n";
    std::cout << "  rescale count             = 1\n";
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