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

    std::vector<double> coeffs = {0.30, -0.10, 0.05, 0.00, 0.02, 0.00, -0.01, 0.00, 0.005};

    auto t0 = Clock::now();
    auto ct_poly = cc->EvalPolyPS(ct, coeffs);
    auto t1 = Clock::now();

    auto dec_poly = DecryptVec(cc, kp.secretKey, ct_poly, kSlots);

    std::vector<double> ref_poly;
    for (double x : kInput) {
        ref_poly.push_back(PlainPolyEval(x, coeffs));
    }

    std::cout << "\n[Test] EvalPolyPS\n";
    PrintCtState("after EvalPolyPS", ct_poly);
    std::cout << "time = "
              << std::chrono::duration<double>(t1 - t0).count()
              << " s\n";
    std::cout << "max_abs_err vs plain poly = "
              << std::scientific << MaxAbsErrReal(dec_poly, ref_poly) << "\n";
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