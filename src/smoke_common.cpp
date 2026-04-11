#include "smoke_common.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace fhe_smoke {

double MaxAbsErrReal(const std::vector<std::complex<double>>& got,
                     const std::vector<double>& ref) {
    if (got.size() < ref.size()) {
        throw std::runtime_error("MaxAbsErrReal: decrypted vector is shorter than reference vector");
    }

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

void PrintCtState(const std::string& tag,
                  const Ciphertext<DCRTPoly>& ct) {
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

    // CKKS + 64-bit native integer 下，通常 scalingModSize 要小于 60
    parameters.SetFirstModSize(60);
    parameters.SetScalingModSize(50);

    parameters.SetScalingTechnique(scalTech);

    // 你当前 OpenFHE 版本已验证可接受这两个 composite 参数
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

double PlainPolyEval(double x, const std::vector<double>& coeffs) {
    double y = 0.0;
    double xp = 1.0;
    for (double c : coeffs) {
        y += c * xp;
        xp *= x;
    }
    return y;
}

}  // namespace fhe_smoke