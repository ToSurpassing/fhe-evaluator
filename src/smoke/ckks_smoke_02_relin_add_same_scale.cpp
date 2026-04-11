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

    auto ct_nr_a = cc->EvalMultNoRelin(ct, ct);
    auto ct_nr_b = cc->EvalMultNoRelin(ct, ct);

    cc->RelinearizeInPlace(ct_nr_a);
    cc->RelinearizeInPlace(ct_nr_b);

    std::cout << "\n[Test] RelinearizeInPlace -> EvalAdd (same-scale)\n";
    PrintCtState("ct_nr_a", ct_nr_a);
    PrintCtState("ct_nr_b", ct_nr_b);

    auto ct_add_same = cc->EvalAdd(ct_nr_a, ct_nr_b);
    auto dec_add_same = DecryptVec(cc, kp.secretKey, ct_add_same, kSlots);

    std::vector<double> ref_add_same;
    for (double x : kInput) {
        ref_add_same.push_back(2.0 * x * x);
    }

    PrintCtState("after EvalAdd same-scale", ct_add_same);
    std::cout << "max_abs_err vs 2*x^2 = "
              << std::scientific << MaxAbsErrReal(dec_add_same, ref_add_same) << "\n";
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