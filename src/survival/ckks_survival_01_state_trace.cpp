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
    double timeSec;
    double maxAbsErr;
};

void PrintTraceHeader() {
    std::cout << std::left
              << std::setw(26) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';

    std::cout << std::string(84, '-') << '\n';
}

void PrintTraceRow(const TraceRow& row) {
    std::cout << std::left
              << std::setw(26) << row.step
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

TraceRow CaptureRow(const std::string& step,
                    CC cc,
                    const PrivateKey<DCRTPoly>& sk,
                    const Ciphertext<DCRTPoly>& ct,
                    const std::vector<double>& ref,
                    double timeSec) {
    auto dec = DecryptVec(cc, sk, ct, kSlots);
    return TraceRow{
        step,
        ct->GetLevel(),
        ct->GetNoiseScaleDeg(),
        timeSec,
        MaxAbsErrReal(dec, ref)
    };
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
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

    std::vector<TraceRow> rows;
    rows.reserve(5);

    rows.push_back(CaptureRow("input", cc, kp.secretKey, ct, kInput, 0.0));

    auto t0 = Clock::now();
    auto ct_nr = cc->EvalMultNoRelin(ct, ct);
    auto t1 = Clock::now();
    rows.push_back(CaptureRow(
        "after EvalMultNoRelin",
        cc,
        kp.secretKey,
        ct_nr,
        refSq,
        std::chrono::duration<double>(t1 - t0).count()));

    auto t2 = Clock::now();
    cc->RelinearizeInPlace(ct_nr);
    auto t3 = Clock::now();
    rows.push_back(CaptureRow(
        "after RelinearizeInPlace",
        cc,
        kp.secretKey,
        ct_nr,
        refSq,
        std::chrono::duration<double>(t3 - t2).count()));

    auto t4 = Clock::now();
    cc->RescaleInPlace(ct_nr);
    auto t5 = Clock::now();
    rows.push_back(CaptureRow(
        "after RescaleInPlace",
        cc,
        kp.secretKey,
        ct_nr,
        refSq,
        std::chrono::duration<double>(t5 - t4).count()));

    auto t6 = Clock::now();
    auto ct_add = cc->EvalAdd(ct_nr, ct_nr);
    auto t7 = Clock::now();
    rows.push_back(CaptureRow(
        "after EvalAdd(self,self)",
        cc,
        kp.secretKey,
        ct_add,
        ref2Sq,
        std::chrono::duration<double>(t7 - t6).count()));

    std::cout << '\n';
    PrintTraceHeader();
    for (const auto& row : rows) {
        PrintTraceRow(row);
    }
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