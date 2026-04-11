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

    auto ct_nr = cc->EvalMultNoRelin(ct, ct);
    cc->RelinearizeInPlace(ct_nr);

    std::vector<double> ref_sq;
    for (double x : kInput) {
        ref_sq.push_back(x * x);
    }

    std::cout << "\n[Test] RescaleInPlace\n";
    PrintCtState("before RescaleInPlace", ct_nr);

    auto dec_before = DecryptVec(cc, kp.secretKey, ct_nr, kSlots);
    std::cout << "max_abs_err before rescale vs x^2 = "
              << std::scientific << MaxAbsErrReal(dec_before, ref_sq) << "\n";

    cc->RescaleInPlace(ct_nr);
    PrintCtState("after RescaleInPlace", ct_nr);

    auto dec_after = DecryptVec(cc, kp.secretKey, ct_nr, kSlots);
    std::cout << "max_abs_err after  rescale vs x^2 = "
              << std::scientific << MaxAbsErrReal(dec_after, ref_sq) << "\n";
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