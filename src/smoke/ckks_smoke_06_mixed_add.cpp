#include "openfhe.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lbcrypto;
using CC = CryptoContext<DCRTPoly>;

static std::vector<double> kInput = {0.10, -0.20, 0.30, -0.40, 0.05, -0.15, 0.25, -0.35};
static constexpr size_t kSlots = 8;

double MaxAbsErrReal(const std::vector<std::complex<double>>& got,
                     const std::vector<double>& ref) {
    double ans = 0.0;
    for (size_t i = 0; i < ref.size(); ++i) {
        ans = std::max(ans, std::abs(got[i].real() - ref[i]));
    }
    return ans;
}

std::vector<std::complex<double>> DecryptVec(CC cc,
                                             const PrivateKey<DCRTPoly>& sk,
                                             const Ciphertext<DCRTPoly>& ct,
                                             size_t slots) {
    Plaintext result;
    cc->Decrypt(sk, ct, &result);
    result->SetLength(slots);
    return result->GetCKKSPackedValue();
}

void PrintCtState(const std::string& tag, const Ciphertext<DCRTPoly>& ct) {
    std::cout << tag
              << " | level = " << ct->GetLevel()
              << ", noiseScaleDeg = " << ct->GetNoiseScaleDeg()
              << '\n';
}

CC BuildContext(ScalingTechnique scalTech) {
    CCParams<CryptoContextCKKSRNS> parameters;

    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);

    // 当前只做 API / survival 验证，不是最终安全参数
    parameters.SetRingDim(1 << 12);
    parameters.SetMultiplicativeDepth(6);
    parameters.SetBatchSize(kSlots);

    parameters.SetFirstModSize(60);
    parameters.SetScalingModSize(50);
    parameters.SetScalingTechnique(scalTech);

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

    // A: 保持 NoRelin 状态
    auto ct_nr_a = cc->EvalMultNoRelin(ct, ct);

    // B: 先 NoRelin，再显式做 relin
    auto ct_nr_b = cc->EvalMultNoRelin(ct, ct);
    cc->RelinearizeInPlace(ct_nr_b);

    std::cout << "\n[Test] Mixed-state EvalAdd (NoRelin + Relinearized)\n";
    PrintCtState("ct_nr_a (NoRelin)", ct_nr_a);
    PrintCtState("ct_nr_b (Relin)", ct_nr_b);

    auto ct_add_mixed = cc->EvalAdd(ct_nr_a, ct_nr_b);
    PrintCtState("after EvalAdd mixed", ct_add_mixed);

    auto dec_add_mixed = DecryptVec(cc, kp.secretKey, ct_add_mixed, kSlots);

    std::vector<double> ref_add_mixed;
    for (double x : kInput) {
        ref_add_mixed.push_back(2.0 * x * x);
    }

    std::cout << "max_abs_err vs 2*x^2 = "
              << std::scientific << MaxAbsErrReal(dec_add_mixed, ref_add_mixed)
              << "\n";
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