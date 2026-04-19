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
    std::string step;
    size_t level;
    size_t noiseScaleDeg;
    size_t elements;
    double timeSec;
    double maxAbsErr;
};

void PrintTraceHeader() {
    std::cout << std::left
              << std::setw(42) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(12) << "ctElems"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';

    std::cout << std::string(112, '-') << '\n';
}

void PrintTraceRow(const TraceRow& row) {
    std::cout << std::left
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

CC BuildRawQuarticContext(ScalingTechnique scalTech) {
    CCParams<CryptoContextCKKSRNS> parameters;

    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(1 << 12);
    parameters.SetMultiplicativeDepth(6);
    parameters.SetBatchSize(kSlots);
    parameters.SetFirstModSize(60);
    parameters.SetScalingModSize(50);
    parameters.SetScalingTechnique(scalTech);

    // This survival test intentionally creates a degree-4 secret-key intermediate.
    // It needs a vector of relinearization keys up through s^4 -> s.
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

TraceRow CaptureRow(const std::string& step,
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
        throw std::runtime_error("CaptureRow failed at [" + step + "]: " + e.what());
    }

    return TraceRow{
        step,
        ct->GetLevel(),
        ct->GetNoiseScaleDeg(),
        ct->NumberCiphertextElements(),
        timeSec,
        MaxAbsErrReal(dec, ref)
    };
}

Ciphertext<DCRTPoly> TimedRawMult(CC cc,
                                  const PrivateKey<DCRTPoly>& sk,
                                  const Ciphertext<DCRTPoly>& lhs,
                                  const Ciphertext<DCRTPoly>& rhs,
                                  const std::string& step,
                                  size_t refPower,
                                  std::vector<TraceRow>& rows) {
    const auto ref = BuildRefPower(refPower);

    auto t0 = Clock::now();
    auto ctOut = cc->EvalMultNoRelin(lhs, rhs);
    auto t1 = Clock::now();

    rows.push_back(CaptureRow(
        step,
        cc,
        sk,
        ctOut,
        ref,
        std::chrono::duration<double>(t1 - t0).count()));

    return ctOut;
}

void MaterializeQuartic(CC cc,
                        const PrivateKey<DCRTPoly>& sk,
                        Ciphertext<DCRTPoly>& ctX4Raw,
                        std::vector<TraceRow>& rows) {
    const auto refX4 = BuildRefPower(4);

    auto t0 = Clock::now();
    std::cout << "[materialize] before relin: elements="
              << ctX4Raw->NumberCiphertextElements()
              << ", level=" << ctX4Raw->GetLevel()
              << ", noiseScaleDeg=" << ctX4Raw->GetNoiseScaleDeg()
              << '\n';
    cc->RelinearizeInPlace(ctX4Raw);
    auto t1 = Clock::now();
    rows.push_back(CaptureRow(
        "after RelinearizeInPlace(x4_raw)",
        cc,
        sk,
        ctX4Raw,
        refX4,
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    cc->RescaleInPlace(ctX4Raw);
    auto t3 = Clock::now();
    rows.push_back(CaptureRow(
        "after RescaleInPlace(x4)",
        cc,
        sk,
        ctX4Raw,
        refX4,
        std::chrono::duration<double>(t3 - t2).count()));
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[SURVIVAL] raw cube times x\n";
    std::cout << "[TEST] EvalMultNoRelin(EvalMultNoRelin(EvalMultNoRelin(x,x),x), x)\n";
    std::cout << "[KEYGEN] SetMaxRelinSkDeg(4) + EvalMultKeysGen\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildRawQuarticContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeysGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    std::vector<TraceRow> rows;
    rows.reserve(6);
    rows.push_back(CaptureRow("input", cc, kp.secretKey, ct, kInput, 0.0));

    auto ctX2Raw = TimedRawMult(
        cc, kp.secretKey, ct, ct,
        "x2_raw = EvalMultNoRelin(x,x)",
        2, rows);
    auto ctX3Raw = TimedRawMult(
        cc, kp.secretKey, ctX2Raw, ct,
        "x3_raw = EvalMultNoRelin(x2_raw,x)",
        3, rows);
    auto ctX4Raw = TimedRawMult(
        cc, kp.secretKey, ctX3Raw, ct,
        "x4_raw = EvalMultNoRelin(x3_raw,x)",
        4, rows);

    MaterializeQuartic(cc, kp.secretKey, ctX4Raw, rows);

    std::cout << '\n';
    PrintTraceHeader();
    for (const auto& row : rows) {
        PrintTraceRow(row);
    }

    std::cout << "\nsummary:\n";
    std::cout << "  raw chain length          = 3 tensor products\n";
    std::cout << "  final level               = " << ctX4Raw->GetLevel() << '\n';
    std::cout << "  final noiseScaleDeg       = " << ctX4Raw->GetNoiseScaleDeg() << '\n';
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
