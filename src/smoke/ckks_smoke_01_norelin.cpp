#include "smoke_common.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fhe_smoke;
using Clock = std::chrono::high_resolution_clock;

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    auto t0 = Clock::now();
    auto ct_nr = cc->EvalMultNoRelin(ct, ct);
    auto t1 = Clock::now();

    auto dec_nr = DecryptVec(cc, kp.secretKey, ct_nr, kSlots);

    std::vector<double> ref_sq;
    for (double x : kInput) {
        ref_sq.push_back(x * x);
    }

    std::cout << "\n[Test] EvalMultNoRelin(ct, ct)\n";
    PrintCtState("after EvalMultNoRelin", ct_nr);
    std::cout << "time = "
              << std::chrono::duration<double>(t1 - t0).count()
              << " s\n";
    std::cout << "max_abs_err vs x^2 = "
              << std::scientific << MaxAbsErrReal(dec_nr, ref_sq) << "\n";
}

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