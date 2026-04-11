#include "smoke_common.h"

#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fhe_smoke;

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

    auto ct_sq = cc->EvalMultNoRelin(ct, ct);
    cc->RelinearizeInPlace(ct_sq);
    cc->RescaleInPlace(ct_sq);

    PrintCtState("after minimal square chain", ct_sq);

    auto dec_sq = DecryptVec(cc, kp.secretKey, ct_sq, kSlots);

    std::vector<double> ref_sq;
    for (double x : kInput) {
        ref_sq.push_back(x * x);
    }

    std::cout << "max_abs_err vs x^2 = "
              << std::scientific << MaxAbsErrReal(dec_sq, ref_sq) << "\n";
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