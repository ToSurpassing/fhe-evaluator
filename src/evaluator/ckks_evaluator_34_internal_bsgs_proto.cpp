#include "smoke_common.h"
#include "internal_bsgs_common.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fhe_smoke;
using namespace fhe_eval::internal_bsgs;
using Clock = std::chrono::high_resolution_clock;

namespace {

struct TraceRow {
    std::string strategy;
    std::string step;
    size_t level = 0;
    size_t noiseScaleDeg = 0;
    size_t elements = 0;
    double timeSec = 0.0;
    double maxAbsErr = 0.0;
};

struct EvalStats {
    size_t tensorProducts = 0;
    size_t relinCount = 0;
    size_t rescaleCount = 0;
    size_t scalarMultCount = 0;
    size_t levelAlignCount = 0;
    size_t addCount = 0;
    double totalOpSec = 0.0;
};

struct PowerCt {
    size_t power = 0;
    Ciphertext<DCRTPoly> ct;
};

struct EvalOutput {
    Ciphertext<DCRTPoly> value;
    EvalStats stats;
    std::vector<TraceRow> trace;
};

double PlainPower(double x, size_t power) {
    double y = 1.0;
    for (size_t i = 0; i < power; ++i) {
        y *= x;
    }
    return y;
}

std::vector<double> RefPower(size_t power) {
    std::vector<double> ref;
    ref.reserve(kInput.size());
    for (double x : kInput) {
        ref.push_back(PlainPower(x, power));
    }
    return ref;
}

std::vector<double> RefScaledPower(size_t power, double scalar) {
    auto ref = RefPower(power);
    for (double& value : ref) {
        value *= scalar;
    }
    return ref;
}

std::vector<double> RefLowLinearGroup(const OuterBlock& block, size_t highPower) {
    std::vector<double> ref(kInput.size(), 0.0);
    for (const auto& term : block.terms) {
        if (term.highPower != highPower) {
            continue;
        }
        for (size_t i = 0; i < kInput.size(); ++i) {
            ref[i] += term.coeff * PlainPower(kInput[i], term.lowPower);
        }
    }
    return ref;
}

std::vector<double> RefInnerGroup(const OuterBlock& block, size_t highPower) {
    auto ref = RefLowLinearGroup(block, highPower);
    for (size_t i = 0; i < ref.size(); ++i) {
        ref[i] *= PlainPower(kInput[i], highPower);
    }
    return ref;
}

std::vector<double> RefBlock(const OuterBlock& block) {
    std::vector<double> ref(kInput.size(), 0.0);
    for (const auto& term : block.terms) {
        const size_t power = term.lowPower + term.highPower;
        for (size_t i = 0; i < kInput.size(); ++i) {
            ref[i] += term.coeff * PlainPower(kInput[i], power);
        }
    }
    return ref;
}

std::vector<double> RefOuterBlock(const OuterBlock& block) {
    auto ref = RefBlock(block);
    for (size_t i = 0; i < ref.size(); ++i) {
        ref[i] *= PlainPower(kInput[i], block.outerPower);
    }
    return ref;
}

std::vector<double> RefPolynomial(const InternalBsgsPlan& plan) {
    std::vector<double> ref(kInput.size(), 0.0);
    for (const auto& block : plan.blocks) {
        const auto blockRef = block.outerPower == 0 ? RefBlock(block) : RefOuterBlock(block);
        for (size_t i = 0; i < ref.size(); ++i) {
            ref[i] += blockRef[i];
        }
    }
    return ref;
}

std::vector<double> InternalBSGSPlain(const OuterBlock& block) {
    std::vector<double> acc(kInput.size(), 0.0);
    for (const size_t highPower : {4, 8, 12}) {
        const auto group = RefInnerGroup(block, highPower);
        for (size_t i = 0; i < acc.size(); ++i) {
            acc[i] += group[i];
        }
    }
    return acc;
}

std::vector<double> AsymmetricOuterPlain(const InternalBsgsPlan& plan) {
    std::vector<double> acc(kInput.size(), 0.0);
    for (const auto& block : plan.blocks) {
        auto inner = InternalBSGSPlain(block);
        if (block.outerPower != 0) {
            for (size_t i = 0; i < inner.size(); ++i) {
                inner[i] *= PlainPower(kInput[i], block.outerPower);
            }
        }
        for (size_t i = 0; i < acc.size(); ++i) {
            acc[i] += inner[i];
        }
    }
    return acc;
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

TraceRow Capture(const std::string& strategy,
                 const std::string& step,
                 CC cc,
                 const PrivateKey<DCRTPoly>& sk,
                 const Ciphertext<DCRTPoly>& ct,
                 const std::vector<double>& ref,
                 double timeSec) {
    const auto dec = DecryptVec(cc, sk, ct, kSlots);
    return TraceRow{
        strategy,
        step,
        ct->GetLevel(),
        ct->GetNoiseScaleDeg(),
        ct->NumberCiphertextElements(),
        timeSec,
        MaxAbsErrReal(dec, ref)
    };
}

void PrintTraceHeader() {
    std::cout << std::left
              << std::setw(17) << "strategy"
              << std::setw(52) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(12) << "ctElems"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';
    std::cout << std::string(139, '-') << '\n';
}

void PrintTraceRow(const TraceRow& row) {
    std::cout << std::left
              << std::setw(17) << row.strategy
              << std::setw(52) << row.step
              << std::setw(10) << row.level
              << std::setw(16) << row.noiseScaleDeg
              << std::setw(12) << row.elements
              << std::setw(16) << std::scientific << row.timeSec
              << std::setw(16) << std::scientific << row.maxAbsErr
              << '\n';
}

void PrintStats(const std::string& label, const EvalStats& stats) {
    std::cout << "  " << label << " tensor products    = " << stats.tensorProducts << '\n';
    std::cout << "  " << label << " relin count        = " << stats.relinCount << '\n';
    std::cout << "  " << label << " rescale count      = " << stats.rescaleCount << '\n';
    std::cout << "  " << label << " scalar mult count  = " << stats.scalarMultCount << '\n';
    std::cout << "  " << label << " level-align count  = " << stats.levelAlignCount << '\n';
    std::cout << "  " << label << " add count          = " << stats.addCount << '\n';
    std::cout << "  " << label << " total operator sec = " << std::scientific << stats.totalOpSec << '\n';
}

void ReduceToLevel(CC cc, Ciphertext<DCRTPoly>& ct, size_t targetLevel, EvalStats& stats) {
    const size_t currentLevel = ct->GetLevel();
    if (currentLevel > targetLevel) {
        throw std::runtime_error("ReduceToLevel: ciphertext is already deeper than target level");
    }
    if (currentLevel < targetLevel) {
        auto t0 = Clock::now();
        cc->LevelReduceInPlace(ct, nullptr, targetLevel - currentLevel);
        auto t1 = Clock::now();
        ++stats.levelAlignCount;
        stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    }
}

void RaiseNoiseScaleDegTo(CC cc, Ciphertext<DCRTPoly>& ct, size_t target, EvalStats& stats) {
    while (ct->GetNoiseScaleDeg() < target) {
        auto t0 = Clock::now();
        ct = cc->EvalMult(ct, 1.0);
        auto t1 = Clock::now();
        ++stats.scalarMultCount;
        stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
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
    auto t0 = Clock::now();
    cc->RelinearizeInPlace(ct);
    auto t1 = Clock::now();
    ++stats.relinCount;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();

    auto t2 = Clock::now();
    cc->RescaleInPlace(ct);
    auto t3 = Clock::now();
    ++stats.rescaleCount;
    stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
    return ct;
}

Ciphertext<DCRTPoly> Scale(CC cc, Ciphertext<DCRTPoly> ct, double scalar, EvalStats& stats) {
    auto t0 = Clock::now();
    ct = cc->EvalMult(ct, scalar);
    auto t1 = Clock::now();
    ++stats.scalarMultCount;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    return ct;
}

Ciphertext<DCRTPoly> RawProduct(CC cc,
                                const PrivateKey<DCRTPoly>& sk,
                                const std::string& strategy,
                                const std::string& step,
                                const Ciphertext<DCRTPoly>& lhs,
                                const Ciphertext<DCRTPoly>& rhs,
                                const std::vector<double>& ref,
                                std::vector<TraceRow>& rows,
                                EvalStats& stats) {
    auto t0 = Clock::now();
    auto raw = cc->EvalMultNoRelin(lhs, rhs);
    auto t1 = Clock::now();
    ++stats.tensorProducts;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(Capture(strategy, step + " raw product", cc, sk, raw, ref,
                           std::chrono::duration<double>(t1 - t0).count()));
    return raw;
}

Ciphertext<DCRTPoly> MaterializedProduct(CC cc,
                                         const PrivateKey<DCRTPoly>& sk,
                                         const std::string& strategy,
                                         const std::string& step,
                                         const Ciphertext<DCRTPoly>& lhs,
                                         const Ciphertext<DCRTPoly>& rhs,
                                         const std::vector<double>& ref,
                                         std::vector<TraceRow>& rows,
                                         EvalStats& stats) {
    auto raw = RawProduct(cc, sk, strategy, step, lhs, rhs, ref, rows, stats);
    auto t0 = Clock::now();
    auto out = Materialize(cc, raw, stats);
    auto t1 = Clock::now();
    rows.push_back(Capture(strategy, step + " materialized", cc, sk, out, ref,
                           std::chrono::duration<double>(t1 - t0).count()));
    return out;
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
                                     const PrivateKey<DCRTPoly>& sk,
                                     const std::string& strategy,
                                     const std::string& step,
                                     const std::vector<Ciphertext<DCRTPoly>>& terms,
                                     const std::vector<double>& ref,
                                     std::vector<TraceRow>& rows,
                                     EvalStats& stats) {
    if (terms.empty()) {
        throw std::runtime_error("AddAlignedTerms: empty terms");
    }
    auto acc = terms[0]->Clone();
    auto t0 = Clock::now();
    for (size_t i = 1; i < terms.size(); ++i) {
        auto rhs = terms[i]->Clone();
        AlignForAdd(cc, acc, rhs, stats);
        acc = cc->EvalAdd(acc, rhs);
        ++stats.addCount;
    }
    auto t1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(Capture(strategy, step, cc, sk, acc, ref,
                           std::chrono::duration<double>(t1 - t0).count()));
    return acc;
}

std::vector<PowerCt> BuildPowerBasis(CC cc,
                                     const PrivateKey<DCRTPoly>& sk,
                                     const std::string& strategy,
                                     const Ciphertext<DCRTPoly>& input,
                                     std::vector<TraceRow>& rows,
                                     EvalStats& stats) {
    rows.push_back(Capture(strategy, "bar(S1) component x", cc, sk, input, RefPower(1), 0.0));
    auto x2 = MaterializedProduct(cc, sk, strategy, "build x^2", input, input, RefPower(2), rows, stats);

    auto x2ForX3 = x2->Clone();
    auto xForX3 = input->Clone();
    AlignPairForProduct(cc, x2ForX3, xForX3, stats);
    auto x3 = MaterializedProduct(cc, sk, strategy, "build x^3", x2ForX3, xForX3, RefPower(3), rows, stats);

    auto x4 = MaterializedProduct(cc, sk, strategy, "build hat x^4", x2, x2, RefPower(4), rows, stats);
    auto x8 = MaterializedProduct(cc, sk, strategy, "build hat x^8", x4, x4, RefPower(8), rows, stats);

    auto x4ForX12 = x4->Clone();
    auto x8ForX12 = x8->Clone();
    AlignPairForProduct(cc, x4ForX12, x8ForX12, stats);
    auto x12 = MaterializedProduct(cc, sk, strategy, "build hat x^12", x4ForX12, x8ForX12,
                                   RefPower(12), rows, stats);

    auto x16 = MaterializedProduct(cc, sk, strategy, "build S2 x^16", x8, x8, RefPower(16), rows, stats);

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

std::vector<PowerCt> AlignBasis(CC cc,
                                const PrivateKey<DCRTPoly>& sk,
                                const std::string& strategy,
                                const std::vector<PowerCt>& powers,
                                std::vector<TraceRow>& rows,
                                EvalStats& stats) {
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
        rows.push_back(Capture(strategy, "aligned basis x^" + std::to_string(item.power),
                               cc, sk, ct, RefPower(item.power), 0.0));
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
                                         const PrivateKey<DCRTPoly>& sk,
                                         const std::string& strategy,
                                         const OuterBlock& block,
                                         size_t highPower,
                                         const std::vector<PowerCt>& basis,
                                         std::vector<TraceRow>& rows,
                                         EvalStats& stats) {
    std::vector<Ciphertext<DCRTPoly>> scaledTerms;
    for (const auto& term : block.terms) {
        if (term.highPower != highPower) {
            continue;
        }
        auto scaled = Scale(cc, FindPower(basis, term.lowPower)->Clone(), term.coeff, stats);
        rows.push_back(Capture(strategy,
                               block.name + " low coeff*x^" + std::to_string(term.lowPower) +
                                   " for high x^" + std::to_string(highPower),
                               cc, sk, scaled, RefScaledPower(term.lowPower, term.coeff), 0.0));
        scaledTerms.push_back(scaled);
    }
    return AddAlignedTerms(cc, sk, strategy,
                           block.name + " low linear group for high x^" + std::to_string(highPower),
                           scaledTerms, RefLowLinearGroup(block, highPower), rows, stats);
}

Ciphertext<DCRTPoly> InternalBSGSEager(CC cc,
                                       const PrivateKey<DCRTPoly>& sk,
                                       const OuterBlock& block,
                                       const std::vector<PowerCt>& basis,
                                       std::vector<TraceRow>& rows,
                                       EvalStats& stats) {
    std::vector<Ciphertext<DCRTPoly>> materializedGroups;
    for (const auto highPower : ActiveHighPowers(block)) {
        auto low = BuildLowLinearGroup(cc, sk, "internal-eager", block, highPower, basis, rows, stats);
        auto lowForProduct = low->Clone();
        auto high = FindPower(basis, highPower)->Clone();
        AlignPairForProduct(cc, lowForProduct, high, stats);
        auto group = MaterializedProduct(
            cc, sk, "internal-eager",
            block.name + " innerBSGS low(x)*x^" + std::to_string(highPower),
            lowForProduct, high, RefInnerGroup(block, highPower), rows, stats);
        materializedGroups.push_back(group);
    }
    return AddAlignedTerms(cc, sk, "internal-eager", block.name + " internal BSGS block sum",
                           materializedGroups, RefBlock(block), rows, stats);
}

Ciphertext<DCRTPoly> InternalBSGSGroupedLazy(CC cc,
                                             const PrivateKey<DCRTPoly>& sk,
                                             const OuterBlock& block,
                                             const std::vector<PowerCt>& basis,
                                             std::vector<TraceRow>& rows,
                                             EvalStats& stats) {
    std::vector<Ciphertext<DCRTPoly>> rawGroups;
    for (const auto highPower : ActiveHighPowers(block)) {
        auto low = BuildLowLinearGroup(cc, sk, "inner-lazy", block, highPower, basis, rows, stats);
        auto high = FindPower(basis, highPower)->Clone();
        AlignPairForProduct(cc, low, high, stats);
        auto raw = RawProduct(
            cc, sk, "inner-lazy",
            block.name + " raw innerBSGS low(x)*x^" + std::to_string(highPower),
            low, high, RefInnerGroup(block, highPower), rows, stats);
        rawGroups.push_back(raw);
    }

    auto rawSum = AddAlignedTerms(cc, sk, "inner-lazy", block.name + " raw inner groups folded",
                                  rawGroups, RefBlock(block), rows, stats);
    auto t0 = Clock::now();
    auto out = Materialize(cc, rawSum, stats);
    auto t1 = Clock::now();
    rows.push_back(Capture("inner-lazy", block.name + " folded inner block materialized",
                           cc, sk, out, RefBlock(block),
                           std::chrono::duration<double>(t1 - t0).count()));
    return out;
}

Ciphertext<DCRTPoly> AssembleOuter(CC cc,
                                   const PrivateKey<DCRTPoly>& sk,
                                   const std::string& strategy,
                                   const InternalBsgsPlan& plan,
                                   const Ciphertext<DCRTPoly>& block0,
                                   const Ciphertext<DCRTPoly>& block1,
                                   const Ciphertext<DCRTPoly>& outer,
                                   std::vector<TraceRow>& rows,
                                   EvalStats& stats) {
    auto shifted = block1->Clone();
    auto z = outer->Clone();
    AlignPairForProduct(cc, shifted, z, stats);
    auto shiftedBlock = MaterializedProduct(cc, sk, strategy, "outer block1*x^16",
                                            shifted, z, RefOuterBlock(plan.blocks[1]), rows, stats);

    auto acc = block0->Clone();
    AlignForAdd(cc, acc, shiftedBlock, stats);
    auto t0 = Clock::now();
    auto out = cc->EvalAdd(acc, shiftedBlock);
    auto t1 = Clock::now();
    ++stats.addCount;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(Capture(strategy, "outer0 + outer1*x^16", cc, sk, out,
                           RefPolynomial(plan), std::chrono::duration<double>(t1 - t0).count()));
    return out;
}

EvalOutput RunInternalEager(CC cc,
                            const PrivateKey<DCRTPoly>& sk,
                            const InternalBsgsPlan& plan,
                            const Ciphertext<DCRTPoly>& input) {
    EvalOutput out;
    out.trace.reserve(120);
    const auto basis = BuildPowerBasis(cc, sk, "internal-eager", input, out.trace, out.stats);
    auto block0 = InternalBSGSEager(cc, sk, plan.blocks[0], basis, out.trace, out.stats);
    auto block1 = InternalBSGSEager(cc, sk, plan.blocks[1], basis, out.trace, out.stats);
    out.value = AssembleOuter(cc, sk, "internal-eager", plan, block0, block1,
                              FindPower(basis, plan.outerPower), out.trace, out.stats);
    return out;
}

EvalOutput RunInternalLazy(CC cc,
                           const PrivateKey<DCRTPoly>& sk,
                           const InternalBsgsPlan& plan,
                           const Ciphertext<DCRTPoly>& input) {
    EvalOutput out;
    out.trace.reserve(120);
    auto basis = BuildPowerBasis(cc, sk, "inner-lazy", input, out.trace, out.stats);
    basis = AlignBasis(cc, sk, "inner-lazy", basis, out.trace, out.stats);
    auto block0 = InternalBSGSGroupedLazy(cc, sk, plan.blocks[0], basis, out.trace, out.stats);
    auto block1 = InternalBSGSGroupedLazy(cc, sk, plan.blocks[1], basis, out.trace, out.stats);
    out.value = AssembleOuter(cc, sk, "inner-lazy", plan, block0, block1,
                              FindPower(basis, plan.outerPower), out.trace, out.stats);
    return out;
}

void PrintPlainCheck(const InternalBsgsPlan& plan) {
    const auto direct = RefPolynomial(plan);
    const auto internal = AsymmetricOuterPlain(plan);
    double err = 0.0;
    for (size_t i = 0; i < direct.size(); ++i) {
        err = std::max(err, std::abs(direct[i] - internal[i]));
    }
    std::cout << "[PLAINTEXT] direct-vs-internal max_abs_err = "
              << std::scientific << err << '\n';
    if (err > 1e-14) {
        throw std::runtime_error("plaintext internal BSGS skeleton mismatch");
    }
}

void RunOneMode(const InternalBsgsPlan& plan, const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] fixed tiny internal-BSGS prototype\n";
    std::cout << "[PROJECT DECISION] t=2, B=4, explicit bar(S1)/hat(S1), conservative outer assembly\n";
    std::cout << "[PLAN] " << plan.name << '\n';
    std::cout << "[SETS] bar(S1)={x,x^2,x^3}, hat(S1)={x^4,x^8,x^12}, S2={x^16}\n";
    std::cout << "[POLY] P(x)=inner0(x)+inner1(x)*x^16\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildInternalContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeysGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    auto eager = RunInternalEager(cc, kp.secretKey, plan, ct);
    auto lazy = RunInternalLazy(cc, kp.secretKey, plan, ct);

    const auto ref = RefPolynomial(plan);
    const double eagerErr = MaxAbsErrReal(DecryptVec(cc, kp.secretKey, eager.value, kSlots), ref);
    const double lazyErr = MaxAbsErrReal(DecryptVec(cc, kp.secretKey, lazy.value, kSlots), ref);

    std::cout << '\n';
    PrintTraceHeader();
    for (const auto& row : eager.trace) {
        PrintTraceRow(row);
    }
    for (const auto& row : lazy.trace) {
        PrintTraceRow(row);
    }

    std::cout << "\nsummary:\n";
    PrintStats("internal-eager", eager.stats);
    PrintStats("inner-lazy    ", lazy.stats);
    std::cout << "  internal-eager final max_abs_err = " << std::scientific << eagerErr << '\n';
    std::cout << "  inner-lazy     final max_abs_err = " << std::scientific << lazyErr << '\n';

    std::cout << "\ncomparison:\n";
    std::cout << "  tensor products           = " << eager.stats.tensorProducts << " vs "
              << lazy.stats.tensorProducts << '\n';
    std::cout << "  relin count               = " << eager.stats.relinCount << " vs "
              << lazy.stats.relinCount << '\n';
    std::cout << "  rescale count             = " << eager.stats.rescaleCount << " vs "
              << lazy.stats.rescaleCount << '\n';
    std::cout << "  note                      = lazy only folds inner BSGS groups; outer assembly stays eager\n";

    if (eagerErr >= 1e-8 || lazyErr >= 1e-8) {
        throw std::runtime_error("internal BSGS prototype error threshold failed");
    }
    if (lazy.stats.relinCount > eager.stats.relinCount ||
        lazy.stats.rescaleCount > eager.stats.rescaleCount) {
        throw std::runtime_error("internal BSGS lazy switch count is worse than eager");
    }
}

}  // namespace

int main() {
    try {
        const auto plans = KnownInternalPlans();
        std::cout << "[KNOWN_INTERNAL_PLANS] " << plans.size() << "\n";
        for (const auto& plan : plans) {
            ValidateInternalPlan(plan);
            std::cout << "\n[INTERNAL_PLAN] " << plan.name << '\n';
            PrintPlainCheck(plan);
            RunOneMode(plan, "FIXEDMANUAL", FIXEDMANUAL);
            RunOneMode(plan, "COMPOSITESCALINGMANUAL", COMPOSITESCALINGMANUAL);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << '\n';
        return 1;
    }
    return 0;
}
