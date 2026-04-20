#include "internal_bsgs_common.h"
#include "smoke_common.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fhe_smoke;
using namespace fhe_eval::internal_bsgs;

namespace {

constexpr double kCoeffTol = 1e-14;
constexpr double kErrThreshold = 1e-8;

struct EvalStats {
    size_t tensorProducts = 0;
    size_t relinCount = 0;
    size_t rescaleCount = 0;
    size_t scalarMultCount = 0;
    size_t levelAlignCount = 0;
    size_t addCount = 0;
};

struct PowerCt {
    size_t power = 0;
    Ciphertext<DCRTPoly> ct;
};

struct EvalOutput {
    Ciphertext<DCRTPoly> value;
    EvalStats stats;
};

struct CoeffListCase {
    std::string name;
    std::vector<double> coeffs;
};

double PlainPower(double x, size_t power) {
    double y = 1.0;
    for (size_t i = 0; i < power; ++i) {
        y *= x;
    }
    return y;
}

std::vector<double> RefPolynomial(const InternalBsgsPlan& plan) {
    std::vector<double> ref(kInput.size(), 0.0);
    for (const auto& block : plan.blocks) {
        for (const auto& term : block.terms) {
            const size_t power = block.outerPower + term.lowPower + term.highPower;
            for (size_t i = 0; i < ref.size(); ++i) {
                ref[i] += term.coeff * PlainPower(kInput[i], power);
            }
        }
    }
    return ref;
}

CC BuildInternalContext(ScalingTechnique scalTech) {
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(1 << 12);
    parameters.SetMultiplicativeDepth(14);
    parameters.SetBatchSize(kSlots);
    parameters.SetFirstModSize(60);
    parameters.SetScalingModSize(50);
    parameters.SetScalingTechnique(scalTech);
    parameters.SetMaxRelinSkDeg(3);

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

void ReduceToLevel(CC cc, Ciphertext<DCRTPoly>& ct, size_t targetLevel, EvalStats& stats) {
    const size_t currentLevel = ct->GetLevel();
    if (currentLevel > targetLevel) {
        throw std::runtime_error("ReduceToLevel: ciphertext is already deeper than target level");
    }
    if (currentLevel < targetLevel) {
        cc->LevelReduceInPlace(ct, nullptr, targetLevel - currentLevel);
        ++stats.levelAlignCount;
    }
}

void RaiseNoiseScaleDegTo(CC cc, Ciphertext<DCRTPoly>& ct, size_t target, EvalStats& stats) {
    while (ct->GetNoiseScaleDeg() < target) {
        ct = cc->EvalMult(ct, 1.0);
        ++stats.scalarMultCount;
    }
    if (ct->GetNoiseScaleDeg() > target) {
        throw std::runtime_error("RaiseNoiseScaleDegTo: term is already above target noiseScaleDeg");
    }
}

void AlignForAdd(CC cc, Ciphertext<DCRTPoly>& lhs, Ciphertext<DCRTPoly>& rhs, EvalStats& stats) {
    const size_t targetNoiseScaleDeg = std::max(lhs->GetNoiseScaleDeg(), rhs->GetNoiseScaleDeg());
    RaiseNoiseScaleDegTo(cc, lhs, targetNoiseScaleDeg, stats);
    RaiseNoiseScaleDegTo(cc, rhs, targetNoiseScaleDeg, stats);

    const size_t targetLevel = std::max(lhs->GetLevel(), rhs->GetLevel());
    ReduceToLevel(cc, lhs, targetLevel, stats);
    ReduceToLevel(cc, rhs, targetLevel, stats);
}

void AlignPairForProduct(CC cc, Ciphertext<DCRTPoly>& lhs, Ciphertext<DCRTPoly>& rhs, EvalStats& stats) {
    AlignForAdd(cc, lhs, rhs, stats);
}

Ciphertext<DCRTPoly> Materialize(CC cc, Ciphertext<DCRTPoly> ct, EvalStats& stats) {
    cc->RelinearizeInPlace(ct);
    ++stats.relinCount;
    cc->RescaleInPlace(ct);
    ++stats.rescaleCount;
    return ct;
}

Ciphertext<DCRTPoly> Scale(CC cc, Ciphertext<DCRTPoly> ct, double scalar, EvalStats& stats) {
    ct = cc->EvalMult(ct, scalar);
    ++stats.scalarMultCount;
    return ct;
}

Ciphertext<DCRTPoly> RawProduct(CC cc,
                                const Ciphertext<DCRTPoly>& lhs,
                                const Ciphertext<DCRTPoly>& rhs,
                                EvalStats& stats) {
    auto out = cc->EvalMultNoRelin(lhs, rhs);
    ++stats.tensorProducts;
    return out;
}

Ciphertext<DCRTPoly> MaterializedProduct(CC cc,
                                         const Ciphertext<DCRTPoly>& lhs,
                                         const Ciphertext<DCRTPoly>& rhs,
                                         EvalStats& stats) {
    return Materialize(cc, RawProduct(cc, lhs, rhs, stats), stats);
}

const Ciphertext<DCRTPoly>& FindPower(const std::vector<PowerCt>& powers, size_t power) {
    const auto it = std::find_if(powers.begin(), powers.end(), [power](const PowerCt& item) {
        return item.power == power;
    });
    if (it == powers.end()) {
        throw std::runtime_error("FindPower: missing power x^" + std::to_string(power));
    }
    return it->ct;
}

Ciphertext<DCRTPoly> AddAlignedTerms(CC cc,
                                     const std::vector<Ciphertext<DCRTPoly>>& terms,
                                     EvalStats& stats) {
    if (terms.empty()) {
        throw std::runtime_error("AddAlignedTerms: empty terms");
    }
    auto acc = terms[0]->Clone();
    for (size_t i = 1; i < terms.size(); ++i) {
        auto rhs = terms[i]->Clone();
        AlignForAdd(cc, acc, rhs, stats);
        acc = cc->EvalAdd(acc, rhs);
        ++stats.addCount;
    }
    return acc;
}

std::vector<PowerCt> BuildPowerBasis(CC cc, const Ciphertext<DCRTPoly>& input, EvalStats& stats) {
    auto x2 = MaterializedProduct(cc, input, input, stats);

    auto x2ForX3 = x2->Clone();
    auto xForX3 = input->Clone();
    AlignPairForProduct(cc, x2ForX3, xForX3, stats);
    auto x3 = MaterializedProduct(cc, x2ForX3, xForX3, stats);

    auto x4 = MaterializedProduct(cc, x2, x2, stats);
    auto x8 = MaterializedProduct(cc, x4, x4, stats);

    auto x4ForX12 = x4->Clone();
    auto x8ForX12 = x8->Clone();
    AlignPairForProduct(cc, x4ForX12, x8ForX12, stats);
    auto x12 = MaterializedProduct(cc, x4ForX12, x8ForX12, stats);

    auto x16 = MaterializedProduct(cc, x8, x8, stats);

    return {
        PowerCt{1, input->Clone()},
        PowerCt{2, x2},
        PowerCt{3, x3},
        PowerCt{4, x4},
        PowerCt{8, x8},
        PowerCt{12, x12},
        PowerCt{16, x16},
    };
}

std::vector<PowerCt> AlignBasis(CC cc, const std::vector<PowerCt>& powers, EvalStats& stats) {
    size_t targetLevel = 0;
    size_t targetNoiseScaleDeg = 0;
    for (const auto& item : powers) {
        targetLevel = std::max(targetLevel, item.ct->GetLevel());
        targetNoiseScaleDeg = std::max(targetNoiseScaleDeg, item.ct->GetNoiseScaleDeg());
    }

    std::vector<PowerCt> aligned;
    aligned.reserve(powers.size());
    for (const auto& item : powers) {
        auto ct = item.ct->Clone();
        RaiseNoiseScaleDegTo(cc, ct, targetNoiseScaleDeg, stats);
        ReduceToLevel(cc, ct, targetLevel, stats);
        aligned.push_back(PowerCt{item.power, ct});
    }
    return aligned;
}

std::vector<size_t> ActiveHighPowers(const OuterBlock& block) {
    std::vector<size_t> out;
    for (const auto& term : block.terms) {
        if (std::find(out.begin(), out.end(), term.highPower) == out.end()) {
            out.push_back(term.highPower);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

Ciphertext<DCRTPoly> BuildLowLinearGroup(CC cc,
                                         const OuterBlock& block,
                                         size_t highPower,
                                         const std::vector<PowerCt>& basis,
                                         EvalStats& stats) {
    std::vector<Ciphertext<DCRTPoly>> scaledTerms;
    for (const auto& term : block.terms) {
        if (term.highPower == highPower) {
            scaledTerms.push_back(Scale(cc, FindPower(basis, term.lowPower)->Clone(), term.coeff, stats));
        }
    }
    return AddAlignedTerms(cc, scaledTerms, stats);
}

Ciphertext<DCRTPoly> InternalBSGSEager(CC cc,
                                       const OuterBlock& block,
                                       const std::vector<PowerCt>& basis,
                                       EvalStats& stats) {
    std::vector<Ciphertext<DCRTPoly>> materializedGroups;
    for (const auto highPower : ActiveHighPowers(block)) {
        auto low = BuildLowLinearGroup(cc, block, highPower, basis, stats);
        auto lowForProduct = low->Clone();
        auto high = FindPower(basis, highPower)->Clone();
        AlignPairForProduct(cc, lowForProduct, high, stats);
        materializedGroups.push_back(MaterializedProduct(cc, lowForProduct, high, stats));
    }
    return AddAlignedTerms(cc, materializedGroups, stats);
}

Ciphertext<DCRTPoly> InternalBSGSGroupedLazy(CC cc,
                                             const OuterBlock& block,
                                             const std::vector<PowerCt>& basis,
                                             EvalStats& stats) {
    std::vector<Ciphertext<DCRTPoly>> rawGroups;
    for (const auto highPower : ActiveHighPowers(block)) {
        auto low = BuildLowLinearGroup(cc, block, highPower, basis, stats);
        auto high = FindPower(basis, highPower)->Clone();
        AlignPairForProduct(cc, low, high, stats);
        rawGroups.push_back(RawProduct(cc, low, high, stats));
    }

    auto rawSum = AddAlignedTerms(cc, rawGroups, stats);
    return Materialize(cc, rawSum, stats);
}

Ciphertext<DCRTPoly> AssembleOuter(CC cc,
                                   const InternalBsgsPlan& plan,
                                   const Ciphertext<DCRTPoly>& block0,
                                   const Ciphertext<DCRTPoly>& block1,
                                   const Ciphertext<DCRTPoly>& outer,
                                   EvalStats& stats) {
    auto shifted = block1->Clone();
    auto z = outer->Clone();
    AlignPairForProduct(cc, shifted, z, stats);
    auto shiftedBlock = MaterializedProduct(cc, shifted, z, stats);

    auto acc = block0->Clone();
    AlignForAdd(cc, acc, shiftedBlock, stats);
    auto out = cc->EvalAdd(acc, shiftedBlock);
    ++stats.addCount;
    (void)plan;
    return out;
}

EvalOutput RunInternalEager(CC cc,
                            const InternalBsgsPlan& plan,
                            const Ciphertext<DCRTPoly>& input) {
    EvalOutput out;
    const auto basis = BuildPowerBasis(cc, input, out.stats);
    auto block0 = InternalBSGSEager(cc, plan.blocks[0], basis, out.stats);
    auto block1 = InternalBSGSEager(cc, plan.blocks[1], basis, out.stats);
    out.value = AssembleOuter(cc, plan, block0, block1, FindPower(basis, plan.outerPower), out.stats);
    return out;
}

EvalOutput RunInternalLazy(CC cc,
                           const InternalBsgsPlan& plan,
                           const Ciphertext<DCRTPoly>& input) {
    EvalOutput out;
    auto basis = BuildPowerBasis(cc, input, out.stats);
    basis = AlignBasis(cc, basis, out.stats);
    auto block0 = InternalBSGSGroupedLazy(cc, plan.blocks[0], basis, out.stats);
    auto block1 = InternalBSGSGroupedLazy(cc, plan.blocks[1], basis, out.stats);
    out.value = AssembleOuter(cc, plan, block0, block1, FindPower(basis, plan.outerPower), out.stats);
    return out;
}

CoeffPattern CoeffPatternFromList(const std::string& name, const std::vector<double>& coeffs) {
    if (coeffs.size() > 32) {
        throw std::runtime_error("CoeffPatternFromList: this prototype only accepts degree <= 31");
    }

    CoeffPattern pattern{name, {}};
    for (size_t power = 0; power < coeffs.size(); ++power) {
        const double coeff = coeffs[power];
        if (std::abs(coeff) <= kCoeffTol) {
            continue;
        }
        InnerTerm ignoredTerm;
        size_t ignoredOuter = 0;
        if (!TryDecomposePower(power, ignoredTerm, ignoredOuter)) {
            throw std::runtime_error("CoeffPatternFromList: unsupported nonzero coefficient at x^" +
                                     std::to_string(power));
        }
        pattern.coeffsByPower[power] = coeff;
    }
    if (pattern.coeffsByPower.empty()) {
        throw std::runtime_error("CoeffPatternFromList: no supported nonzero coefficients");
    }
    return pattern;
}

std::vector<CoeffListCase> CoeffListCases() {
    std::vector<double> sparse(32, 0.0);
    sparse[5] = 0.10;
    sparse[11] = -0.04;
    sparse[22] = 0.08;
    sparse[31] = -0.03;

    std::vector<double> zeroSkipping(32, 0.0);
    zeroSkipping[5] = 0.05;
    zeroSkipping[6] = 0.0;
    zeroSkipping[10] = -0.02;
    zeroSkipping[14] = 0.07;
    zeroSkipping[21] = 0.0;
    zeroSkipping[25] = -0.04;
    zeroSkipping[30] = 0.09;

    std::vector<double> denseSupported(32, 0.0);
    const std::vector<size_t> supported = {5, 6, 7, 9, 10, 11, 13, 14, 15,
                                           21, 22, 23, 25, 26, 27, 29, 30, 31};
    for (size_t i = 0; i < supported.size(); ++i) {
        const double sign = (i % 2 == 0) ? 1.0 : -1.0;
        denseSupported[supported[i]] = sign * (0.0125 + 0.0025 * static_cast<double>(i));
    }

    return {
        CoeffListCase{"coeff_list_sparse_degree31", sparse},
        CoeffListCase{"coeff_list_zero_skipping", zeroSkipping},
        CoeffListCase{"coeff_list_dense_supported", denseSupported},
    };
}

std::string ModeName(ScalingTechnique scalTech) {
    return scalTech == FIXEDMANUAL ? "FIXEDMANUAL" : "COMPOSITESCALINGMANUAL";
}

double FinalErr(CC cc,
                const PrivateKey<DCRTPoly>& sk,
                const Ciphertext<DCRTPoly>& value,
                const InternalBsgsPlan& plan) {
    return MaxAbsErrReal(DecryptVec(cc, sk, value, kSlots), RefPolynomial(plan));
}

void PrintPlanSummary(const CoeffPattern& pattern, const InternalBsgsPlan& plan) {
    std::cout << "[COEFF_LIST] " << pattern.name << '\n';
    std::cout << "  active coeffs = " << pattern.coeffsByPower.size() << '\n';
    for (const auto& [power, coeff] : pattern.coeffsByPower) {
        std::cout << "    c[" << std::setw(2) << power << "] = " << std::scientific << coeff << '\n';
    }
    for (const auto& block : plan.blocks) {
        std::cout << "  " << block.name << " outer=x^" << block.outerPower
                  << " terms=" << block.terms.size() << '\n';
    }
}

void RunOne(const CoeffListCase& coeffCase, ScalingTechnique scalTech) {
    const auto pattern = CoeffPatternFromList(coeffCase.name, coeffCase.coeffs);
    const auto plan = GenerateInternalPlanFromCoeffs(pattern);
    ValidateInternalPlan(plan);
    ValidateGeneratedPlanMatchesCoeffs(plan, pattern);

    std::cout << "\n============================================================\n";
    PrintPlanSummary(pattern, plan);
    std::cout << "[MODE] " << ModeName(scalTech) << '\n';

    auto cc = BuildInternalContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeysGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    auto eager = RunInternalEager(cc, plan, ct);
    auto lazy = RunInternalLazy(cc, plan, ct);

    const double eagerErr = FinalErr(cc, kp.secretKey, eager.value, plan);
    const double lazyErr = FinalErr(cc, kp.secretKey, lazy.value, plan);

    std::cout << "summary:\n";
    std::cout << "  tensor products = " << eager.stats.tensorProducts << " vs "
              << lazy.stats.tensorProducts << '\n';
    std::cout << "  relin count     = " << eager.stats.relinCount << " vs "
              << lazy.stats.relinCount << '\n';
    std::cout << "  rescale count   = " << eager.stats.rescaleCount << " vs "
              << lazy.stats.rescaleCount << '\n';
    std::cout << "  scalar mult     = " << eager.stats.scalarMultCount << " vs "
              << lazy.stats.scalarMultCount << '\n';
    std::cout << "  level align     = " << eager.stats.levelAlignCount << " vs "
              << lazy.stats.levelAlignCount << '\n';
    std::cout << "  add count       = " << eager.stats.addCount << " vs "
              << lazy.stats.addCount << '\n';
    std::cout << "  eager err       = " << std::scientific << eagerErr << '\n';
    std::cout << "  inner-lazy err  = " << std::scientific << lazyErr << '\n';

    if (eagerErr >= kErrThreshold || lazyErr >= kErrThreshold) {
        throw std::runtime_error("coefficient-list evaluator error threshold failed");
    }
    if (lazy.stats.relinCount > eager.stats.relinCount ||
        lazy.stats.rescaleCount > eager.stats.rescaleCount) {
        throw std::runtime_error("coefficient-list lazy switch count is worse than eager");
    }
}

}  // namespace

int main() {
    try {
        std::cout << "[EVALUATOR] fixed t=2,B=4 Internal BSGS coefficient-list evaluator\n";
        std::cout << "[BOUNDARY] accepts degree <= 31, skips zero coefficients, "
                  << "rejects unsupported nonzero powers\n";
        std::cout << "[SUPPORTED_POWERS] x^5..x^15 and x^21..x^31 through "
                  << "outer+low+high decomposition\n";

        for (const auto& coeffCase : CoeffListCases()) {
            RunOne(coeffCase, FIXEDMANUAL);
            RunOne(coeffCase, COMPOSITESCALINGMANUAL);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << '\n';
        return 1;
    }
    return 0;
}
