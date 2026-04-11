#ifndef FHE_EVALUATOR_SMOKE_COMMON_H
#define FHE_EVALUATOR_SMOKE_COMMON_H

#include "openfhe.h"

#include <complex>
#include <string>
#include <vector>

namespace fhe_smoke {

using namespace lbcrypto;
using CC = CryptoContext<DCRTPoly>;

inline const std::vector<double> kInput = {
    0.10, -0.20, 0.30, -0.40, 0.05, -0.15, 0.25, -0.35
};

inline constexpr size_t kSlots = 8;

double MaxAbsErrReal(const std::vector<std::complex<double>>& got,
                     const std::vector<double>& ref);

std::vector<std::complex<double>> DecryptVec(CC cc,
                                             const PrivateKey<DCRTPoly>& sk,
                                             const Ciphertext<DCRTPoly>& ct,
                                             size_t slots);

void PrintCtState(const std::string& tag,
                  const Ciphertext<DCRTPoly>& ct);

CC BuildContext(ScalingTechnique scalTech);

double PlainPolyEval(double x, const std::vector<double>& coeffs);

}  // namespace fhe_smoke

#endif  // FHE_EVALUATOR_SMOKE_COMMON_H