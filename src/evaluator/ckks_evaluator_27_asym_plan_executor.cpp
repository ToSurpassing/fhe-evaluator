#include "smoke_common.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fhe_smoke;
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

struct T3Term {
    size_t a = 0;
    size_t b = 0;
    size_t c = 0;
    double coeff = 0.0;
};

struct BlockSpec {
    std::string name;
    size_t outerMultiplierPower = 0;
    std::vector<T3Term> terms;
};

struct OuterAssemblyPlan {
    std::string name;
    std::vector<size_t> componentPowers;
    std::vector<BlockSpec> blocks;
};

struct EvalOutput {
    Ciphertext<DCRTPoly> value;
    EvalStats stats;
    std::vector<TraceRow> trace;
};

OuterAssemblyPlan MakeOuterAssembly24Plan() {
    return OuterAssemblyPlan{
        "outer_assembly_24_shape",
        {1, 2, 4, 8},
        {
            BlockSpec{
                "block0",
                0,
                {
                    T3Term{1, 2, 4, 0.15},    // x^7
                    T3Term{2, 2, 4, -0.25},   // x^8
                    T3Term{1, 4, 4, 0.35},    // x^9
                    T3Term{1, 2, 8, -0.20},   // x^11
                },
            },
            BlockSpec{
                "block1",
                8,
                {
                    T3Term{1, 2, 4, -0.18},   // x^7, then outer *x^8 => x^15
                    T3Term{2, 2, 4, 0.22},    // x^8, then outer *x^8 => x^16
                    T3Term{1, 4, 4, -0.12},   // x^9, then outer *x^8 => x^17
                },
            },
        },
    };
}

size_t TermPower(const T3Term& term) {
    return term.a + term.b + term.c;
}

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
    for (double& v : ref) {
        v *= scalar;
    }
    return ref;
}

std::vector<double> RefBlock(const BlockSpec& block) {
    std::vector<double> ref(kInput.size(), 0.0);
    for (const auto& term : block.terms) {
        const size_t power = TermPower(term);
        for (size_t i = 0; i < kInput.size(); ++i) {
            ref[i] += term.coeff * PlainPower(kInput[i], power);
        }
    }
    return ref;
}

std::vector<double> RefOuterBlock(const BlockSpec& block) {
    std::vector<double> ref(kInput.size(), 0.0);
    for (const auto& term : block.terms) {
        const size_t power = TermPower(term) + block.outerMultiplierPower;
        for (size_t i = 0; i < kInput.size(); ++i) {
            ref[i] += term.coeff * PlainPower(kInput[i], power);
        }
    }
    return ref;
}

std::vector<double> RefPolynomial(const OuterAssemblyPlan& plan) {
    std::vector<double> ref(kInput.size(), 0.0);
    for (const auto& block : plan.blocks) {
        const auto blockRef = block.outerMultiplierPower == 0 ? RefBlock(block) : RefOuterBlock(block);
        for (size_t i = 0; i < ref.size(); ++i) {
            ref[i] += blockRef[i];
        }
    }
    return ref;
}

bool HasPower(const std::vector<size_t>& powers, size_t power) {
    return std::find(powers.begin(), powers.end(), power) != powers.end();
}

void ValidatePlan(const OuterAssemblyPlan& plan) {
    if (plan.blocks.size() != 2) {
        throw std::runtime_error("ValidatePlan: this conservative executor requires exactly two blocks");
    }
    if (plan.blocks[0].outerMultiplierPower != 0) {
        throw std::runtime_error("ValidatePlan: first block must be the unshifted block");
    }
    for (const auto power : plan.componentPowers) {
        if (power == 0) {
            throw std::runtime_error("ValidatePlan: component power 0 is not supported");
        }
    }
    for (const auto& block : plan.blocks) {
        if (block.terms.empty()) {
            throw std::runtime_error("ValidatePlan: empty block");
        }
        if (block.outerMultiplierPower != 0 && !HasPower(plan.componentPowers, block.outerMultiplierPower)) {
            throw std::runtime_error("ValidatePlan: outer multiplier is not a component power");
        }
        for (const auto& term : block.terms) {
            if (!HasPower(plan.componentPowers, term.a) ||
                !HasPower(plan.componentPowers, term.b) ||
                !HasPower(plan.componentPowers, term.c)) {
                throw std::runtime_error("ValidatePlan: term uses a missing component power");
            }
        }
    }
}

CC BuildT3Context(ScalingTechnique scalTech) {
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(1 << 12);
    parameters.SetMultiplicativeDepth(12);
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
              << std::setw(48) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(12) << "ctElems"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';
    std::cout << std::string(135, '-') << '\n';
}

void PrintTraceRow(const TraceRow& row) {
    std::cout << std::left
              << std::setw(17) << row.strategy
              << std::setw(48) << row.step
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

void AlignToState(CC cc,
                  Ciphertext<DCRTPoly>& ct,
                  size_t targetLevel,
                  size_t targetNoiseScaleDeg,
                  EvalStats& stats) {
    RaiseNoiseScaleDegTo(cc, ct, targetNoiseScaleDeg, stats);
    ReduceToLevel(cc, ct, targetLevel, stats);
}

void AlignForAdd(CC cc, Ciphertext<DCRTPoly>& lhs, Ciphertext<DCRTPoly>& rhs, EvalStats& stats) {
    const size_t targetNoiseScaleDeg = std::max(lhs->GetNoiseScaleDeg(), rhs->GetNoiseScaleDeg());
    RaiseNoiseScaleDegTo(cc, lhs, targetNoiseScaleDeg, stats);
    RaiseNoiseScaleDegTo(cc, rhs, targetNoiseScaleDeg, stats);

    const size_t targetLevel = std::max(lhs->GetLevel(), rhs->GetLevel());
    ReduceToLevel(cc, lhs, targetLevel, stats);
    ReduceToLevel(cc, rhs, targetLevel, stats);
}

void AlignPairForProduct(CC cc,
                         Ciphertext<DCRTPoly>& lhs,
                         Ciphertext<DCRTPoly>& rhs,
                         EvalStats& stats) {
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

std::vector<PowerCt> BuildComponentSets(CC cc,
                                        const PrivateKey<DCRTPoly>& sk,
                                        const std::string& strategy,
                                        const Ciphertext<DCRTPoly>& input,
                                        std::vector<TraceRow>& rows,
                                        EvalStats& stats) {
    rows.push_back(Capture(strategy, "S1 component x", cc, sk, input, RefPower(1), 0.0));

    auto x2 = MaterializedProduct(
        cc, sk, strategy, "build x^2", input, input, RefPower(2), rows, stats);
    auto x4 = MaterializedProduct(
        cc, sk, strategy, "build x^4", x2, x2, RefPower(4), rows, stats);
    auto x8 = MaterializedProduct(
        cc, sk, strategy, "build z=x^8", x4, x4, RefPower(8), rows, stats);

    return {
        PowerCt{1, input->Clone()},
        PowerCt{2, x2},
        PowerCt{4, x4},
        PowerCt{8, x8},
    };
}

std::vector<PowerCt> AlignComponents(CC cc,
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
        AlignToState(cc, ct, targetLevel, targetNoiseScaleDeg, stats);
        rows.push_back(Capture(strategy, "aligned component x^" + std::to_string(item.power),
                               cc, sk, ct, RefPower(item.power), 0.0));
        aligned.push_back(PowerCt{item.power, ct});
    }
    return aligned;
}

Ciphertext<DCRTPoly> AddTerms(CC cc,
                              const PrivateKey<DCRTPoly>& sk,
                              const std::string& strategy,
                              const std::string& step,
                              const std::vector<Ciphertext<DCRTPoly>>& terms,
                              const std::vector<double>& ref,
                              std::vector<TraceRow>& rows,
                              EvalStats& stats) {
    auto t0 = Clock::now();
    auto acc = terms[0]->Clone();
    for (size_t i = 1; i < terms.size(); ++i) {
        acc = cc->EvalAdd(acc, terms[i]);
        ++stats.addCount;
    }
    auto t1 = Clock::now();
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(Capture(strategy, step, cc, sk, acc, ref,
                           std::chrono::duration<double>(t1 - t0).count()));
    return acc;
}

Ciphertext<DCRTPoly> EvalBlockExpandedEager(CC cc,
                                            const PrivateKey<DCRTPoly>& sk,
                                            const BlockSpec& block,
                                            const std::vector<PowerCt>& components,
                                            std::vector<TraceRow>& rows,
                                            EvalStats& stats) {
    std::vector<Ciphertext<DCRTPoly>> materializedTerms;
    materializedTerms.reserve(block.terms.size());
    for (const auto& term : block.terms) {
        auto lhs = FindPower(components, term.a)->Clone();
        auto rhs = FindPower(components, term.b)->Clone();
        AlignPairForProduct(cc, lhs, rhs, stats);
        auto prefix = MaterializedProduct(
            cc, sk, "expanded-eager",
            block.name + " prefix x^" + std::to_string(term.a) + "*x^" + std::to_string(term.b),
            lhs, rhs, RefPower(term.a + term.b), rows, stats);

        auto prefixForFinal = prefix->Clone();
        auto finalRhs = FindPower(components, term.c)->Clone();
        AlignPairForProduct(cc, prefixForFinal, finalRhs, stats);
        auto product = MaterializedProduct(
            cc, sk, "expanded-eager",
            block.name + " prefix*x^" + std::to_string(term.c),
            prefixForFinal, finalRhs, RefPower(TermPower(term)), rows, stats);
        product = Scale(cc, product, term.coeff, stats);
        rows.push_back(Capture("expanded-eager",
                               block.name + " scaled c*x^" + std::to_string(TermPower(term)),
                               cc, sk, product,
                               RefScaledPower(TermPower(term), term.coeff), 0.0));
        materializedTerms.push_back(product);
    }

    return AddTerms(cc, sk, "expanded-eager", block.name + " materialized block sum",
                    materializedTerms, RefBlock(block), rows, stats);
}

Ciphertext<DCRTPoly> EvalBlockGroupedLazy(CC cc,
                                          const PrivateKey<DCRTPoly>& sk,
                                          const BlockSpec& block,
                                          const std::vector<PowerCt>& components,
                                          std::vector<TraceRow>& rows,
                                          EvalStats& stats) {
    std::vector<Ciphertext<DCRTPoly>> rawTerms;
    rawTerms.reserve(block.terms.size());
    for (const auto& term : block.terms) {
        auto prefix = RawProduct(
            cc, sk, "grouped-lazy",
            block.name + " prefix x^" + std::to_string(term.a) + "*x^" + std::to_string(term.b),
            FindPower(components, term.a), FindPower(components, term.b),
            RefPower(term.a + term.b), rows, stats);

        auto raw = RawProduct(
            cc, sk, "grouped-lazy",
            block.name + " raw prefix*x^" + std::to_string(term.c),
            prefix, FindPower(components, term.c),
            RefPower(TermPower(term)), rows, stats);
        raw = Scale(cc, raw, term.coeff, stats);
        rows.push_back(Capture("grouped-lazy",
                               block.name + " raw scaled c*x^" + std::to_string(TermPower(term)),
                               cc, sk, raw,
                               RefScaledPower(TermPower(term), term.coeff), 0.0));
        rawTerms.push_back(raw);
    }

    auto rawSum = AddTerms(cc, sk, "grouped-lazy", block.name + " raw t=3 terms folded",
                           rawTerms, RefBlock(block), rows, stats);
    auto t0 = Clock::now();
    auto out = Materialize(cc, rawSum, stats);
    auto t1 = Clock::now();
    rows.push_back(Capture("grouped-lazy", block.name + " folded block materialized",
                           cc, sk, out, RefBlock(block),
                           std::chrono::duration<double>(t1 - t0).count()));
    return out;
}

Ciphertext<DCRTPoly> AssembleOuter(CC cc,
                                   const PrivateKey<DCRTPoly>& sk,
                                   const std::string& strategy,
                                   const OuterAssemblyPlan& plan,
                                   const Ciphertext<DCRTPoly>& block0,
                                   const Ciphertext<DCRTPoly>& block1,
                                   const Ciphertext<DCRTPoly>& z,
                                   std::vector<TraceRow>& rows,
                                   EvalStats& stats) {
    const auto& shiftedBlock = plan.blocks[1];
    auto b1 = block1->Clone();
    auto zForOuter = z->Clone();
    AlignPairForProduct(cc, b1, zForOuter, stats);
    auto outer = MaterializedProduct(
        cc, sk, strategy, "outer block1*z", b1, zForOuter,
        RefOuterBlock(shiftedBlock), rows, stats);

    auto b0 = block0->Clone();
    AlignForAdd(cc, b0, outer, stats);
    auto t0 = Clock::now();
    auto result = cc->EvalAdd(b0, outer);
    auto t1 = Clock::now();
    ++stats.addCount;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(Capture(strategy, "block0 + block1*z", cc, sk, result, RefPolynomial(plan),
                           std::chrono::duration<double>(t1 - t0).count()));
    return result;
}

EvalOutput RunExpandedEager(CC cc,
                            const PrivateKey<DCRTPoly>& sk,
                            const OuterAssemblyPlan& plan,
                            const Ciphertext<DCRTPoly>& input) {
    ValidatePlan(plan);
    EvalOutput out;
    out.trace.reserve(120);
    const auto components = BuildComponentSets(cc, sk, "expanded-eager", input, out.trace, out.stats);
    auto block0 = EvalBlockExpandedEager(cc, sk, plan.blocks[0], components, out.trace, out.stats);
    auto block1 = EvalBlockExpandedEager(cc, sk, plan.blocks[1], components, out.trace, out.stats);
    out.value = AssembleOuter(cc, sk, "expanded-eager", plan, block0, block1,
                              FindPower(components, plan.blocks[1].outerMultiplierPower),
                              out.trace, out.stats);
    return out;
}

EvalOutput RunGroupedLazy(CC cc,
                          const PrivateKey<DCRTPoly>& sk,
                          const OuterAssemblyPlan& plan,
                          const Ciphertext<DCRTPoly>& input) {
    ValidatePlan(plan);
    EvalOutput out;
    out.trace.reserve(120);
    auto components = BuildComponentSets(cc, sk, "grouped-lazy", input, out.trace, out.stats);
    components = AlignComponents(cc, sk, "grouped-lazy", components, out.trace, out.stats);
    auto block0 = EvalBlockGroupedLazy(cc, sk, plan.blocks[0], components, out.trace, out.stats);
    auto block1 = EvalBlockGroupedLazy(cc, sk, plan.blocks[1], components, out.trace, out.stats);
    out.value = AssembleOuter(cc, sk, "grouped-lazy", plan, block0, block1,
                              FindPower(components, plan.blocks[1].outerMultiplierPower),
                              out.trace, out.stats);
    return out;
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] asymmetric plan-driven t=3 executor\n";
    std::cout << "[PROJECT DECISION] execute one validated fixed plan, not a general planner\n";
    const auto plan = MakeOuterAssembly24Plan();
    ValidatePlan(plan);
    std::cout << "[PLAN] " << plan.name << '\n';
    std::cout << "[POLY] P(x)=block0(x)+block1(x)*z, z=x^8\n";
    std::cout << "[block0] powers x^7,x^8,x^9,x^11\n";
    std::cout << "[block1*z] powers x^15,x^16,x^17\n";
    std::cout << "[KEYGEN] SetMaxRelinSkDeg(3) + EvalMultKeysGen\n";
    std::cout << "[MODE] " << modeName << "\n";

    auto cc = BuildT3Context(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeysGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    std::cout << "ringDim = " << cc->GetRingDimension() << "\n";
    PrintCtState("input", ct);

    auto eager = RunExpandedEager(cc, kp.secretKey, plan, ct);
    auto lazy = RunGroupedLazy(cc, kp.secretKey, plan, ct);

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
    PrintStats("expanded-eager", eager.stats);
    PrintStats("grouped-lazy  ", lazy.stats);
    std::cout << "  expanded-eager final max_abs_err = " << std::scientific << eagerErr << '\n';
    std::cout << "  grouped-lazy   final max_abs_err = " << std::scientific << lazyErr << '\n';

    std::cout << "\ncomparison:\n";
    std::cout << "  tensor products           = " << eager.stats.tensorProducts << " vs "
              << lazy.stats.tensorProducts << '\n';
    std::cout << "  relin count               = " << eager.stats.relinCount << " vs "
              << lazy.stats.relinCount << '\n';
    std::cout << "  rescale count             = " << eager.stats.rescaleCount << " vs "
              << lazy.stats.rescaleCount << '\n';
    std::cout << "  note                      = two fixed t=3 grouped blocks plus one outer product\n";

    if (eagerErr >= 1e-8 || lazyErr >= 1e-8) {
        throw std::runtime_error("asym plan executor error threshold failed");
    }
    if (lazy.stats.relinCount > eager.stats.relinCount ||
        lazy.stats.rescaleCount > eager.stats.rescaleCount) {
        throw std::runtime_error("asym plan executor lazy switch count is worse than eager");
    }
}

}  // namespace

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
