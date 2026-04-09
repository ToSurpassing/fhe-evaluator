#include "openfhe.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lbcrypto;
using Clock = std::chrono::high_resolution_clock;
using CC = CryptoContext<DCRTPoly>;

static std::vector<double> kInput = {0.10, -0.20, 0.30, -0.40, 0.05, -0.15, 0.25, -0.35};
static constexpr size_t kSlots = 8;

double PlainPolyEval(double x, const std::vector<double>& coeffs) {
    double y = 0.0;
    double xp = 1.0;
    for (double c : coeffs) {
        y += c * xp;
        xp *= x;
    }
    return y;
}

double MaxAbsErrReal(const std::vector<std::complex<double>>& got, const std::vector<double>& ref) {
    double ans = 0.0;
    for (size_t i = 0; i < ref.size(); ++i) {
        ans = std::max(ans, std::abs(got[i].real() - ref[i]));
    }
    return ans;
}

std::vector<std::complex<double>> DecryptVec(CC cc, const PrivateKey<DCRTPoly>& sk,
                                             const Ciphertext<DCRTPoly>& ct, size_t slots) {
    Plaintext result;
    cc->Decrypt(sk, ct, &result);
    result->SetLength(slots);
    return result->GetCKKSPackedValue();
}

void PrintCtState(const std::string& tag, const Ciphertext<DCRTPoly>& ct) {
    std::cout << tag << " | level = " << ct->GetLevel()
              << ", noiseScaleDeg = " << ct->GetNoiseScaleDeg() << '\n';
}

CC BuildContext(ScalingTechnique scalTech) {
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
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

    std::vector<double> coeffs = {0.30, -0.10, 0.05, 0.00, 0.02, 0.00, -0.01, 0.00, 0.005};

    auto t0 = Clock::now();
    auto ct_poly = cc->EvalPolyPS(ct, coeffs);
    auto t1 = Clock::now();

    auto dec_poly = DecryptVec(cc, kp.secretKey, ct_poly, kSlots);

    std::vector<double> ref_poly;
    for (double x : kInput) ref_poly.push_back(PlainPolyEval(x, coeffs));

    std::cout << "\n[Test] EvalPolyPS\n";
    PrintCtState("after EvalPolyPS", ct_poly);
    std::cout << "time = " << std::chrono::duration<double>(t1 - t0).count() << " s\n";
    std::cout << "max_abs_err vs plain poly = " << std::scientific << MaxAbsErrReal(dec_poly, ref_poly) << "\n";
}

int main() {
    try {
        RunOneMode("FIXEDMANUAL", FIXEDMANUAL);
        RunOneMode("COMPOSITESCALINGMANUAL", COMPOSITESCALINGMANUAL);
    } catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << '\n';
        return 1;
    }
    return 0;
}