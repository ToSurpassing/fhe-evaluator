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

struct TailTerm {
    size_t power = 0;
    double coeff = 0.0;
};

struct EvalOutput {
    Ciphertext<DCRTPoly> value;
    EvalStats stats;
    std::vector<TraceRow> trace;
};

const std::vector<T3Term> kT3Terms = {
    T3Term{1, 2, 4, 0.15},   // x^7
    T3Term{2, 2, 4, -0.25},  // x^8
    T3Term{1, 4, 4, 0.35},   // x^9
    T3Term{1, 2, 8, -0.20},  // x^11
};

const std::vector<TailTerm> kTailTerms = {
    TailTerm{0, 0.25},
    TailTerm{1, -0.30},
    TailTerm{4, 0.12},
};

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

std::vector<double> RefT3Block() {
    std::vector<double> ref(kInput.size(), 0.0);
    for (const auto& term : kT3Terms) {
        const size_t power = TermPower(term);
        for (size_t i = 0; i < kInput.size(); ++i) {
            ref[i] += term.coeff * PlainPower(kInput[i], power);
        }
    }
    return ref;
}

std::vector<double> RefPolynomial() {
    auto ref = RefT3Block();
    for (const auto& term : kTailTerms) {
        for (size_t i = 0; i < kInput.size(); ++i) {
            ref[i] += term.coeff * PlainPower(kInput[i], term.power);
        }
    }
    return ref;
}

CC BuildT3Context(ScalingTechnique scalTech) {
    CCParams<CryptoContextCKKSRNS> parameters;

    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_NotSet);
    parameters.SetRingDim(1 << 12);
    parameters.SetMultiplicativeDepth(6);
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
              << std::setw(45) << "step"
              << std::setw(10) << "level"
              << std::setw(16) << "noiseScaleDeg"
              << std::setw(12) << "ctElems"
              << std::setw(16) << "time_sec"
              << std::setw(16) << "max_abs_err"
              << '\n';
    std::cout << std::string(132, '-') << '\n';
}

void PrintTraceRow(const TraceRow& row) {
    std::cout << std::left
              << std::setw(17) << row.strategy
              << std::setw(45) << row.step
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
                                size_t refPower,
                                std::vector<TraceRow>& rows,
                                EvalStats& stats) {
    auto t0 = Clock::now();
    auto raw = cc->EvalMultNoRelin(lhs, rhs);
    auto t1 = Clock::now();
    ++stats.tensorProducts;
    stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
    rows.push_back(Capture(strategy, step + " raw product", cc, sk, raw, RefPower(refPower),
                           std::chrono::duration<double>(t1 - t0).count()));
    return raw;
}

Ciphertext<DCRTPoly> MaterializedProduct(CC cc,
                                         const PrivateKey<DCRTPoly>& sk,
                                         const std::string& strategy,
                                         const std::string& step,
                                         const Ciphertext<DCRTPoly>& lhs,
                                         const Ciphertext<DCRTPoly>& rhs,
                                         size_t refPower,
                                         std::vector<TraceRow>& rows,
                                         EvalStats& stats) {
    auto raw = RawProduct(cc, sk, strategy, step, lhs, rhs, refPower, rows, stats);
    auto t0 = Clock::now();
    auto out = Materialize(cc, raw, stats);
    auto t1 = Clock::now();
    rows.push_back(Capture(strategy, step + " materialized", cc, sk, out, RefPower(refPower),
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
        cc, sk, strategy, "build x^2", input, input, 2, rows, stats);

    auto x4 = MaterializedProduct(
        cc, sk, strategy, "build x^4", x2, x2, 4, rows, stats);

    auto x8 = MaterializedProduct(
        cc, sk, strategy, "build x^8", x4, x4, 8, rows, stats);

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
    if (terms.empty()) {
        throw std::runtime_error("AddTerms: empty terms");
    }
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

Ciphertext<DCRTPoly> AddTailTerms(CC cc,
                                  const PrivateKey<DCRTPoly>& sk,
                                  const std::string& strategy,
                                  Ciphertext<DCRTPoly> acc,
                                  const std::vector<PowerCt>& components,
                                  std::vector<TraceRow>& rows,
                                  EvalStats& stats) {
    std::vector<double> runningRef = RefT3Block();
    for (const auto& term : kTailTerms) {
        if (term.power == 0) {
            auto t0 = Clock::now();
            acc = cc->EvalAdd(acc, term.coeff);
            auto t1 = Clock::now();
            ++stats.addCount;
            stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
            for (double& v : runningRef) {
                v += term.coeff;
            }
            rows.push_back(Capture(strategy, "add tail c0", cc, sk, acc, runningRef,
                                   std::chrono::duration<double>(t1 - t0).count()));
            continue;
        }

        auto tail = FindPower(components, term.power)->Clone();
        tail = Scale(cc, tail, term.coeff, stats);
        auto accAligned = acc->Clone();
        AlignForAdd(cc, accAligned, tail, stats);
        auto t0 = Clock::now();
        acc = cc->EvalAdd(accAligned, tail);
        auto t1 = Clock::now();
        ++stats.addCount;
        stats.totalOpSec += std::chrono::duration<double>(t1 - t0).count();
        const auto tailRef = RefScaledPower(term.power, term.coeff);
        for (size_t i = 0; i < runningRef.size(); ++i) {
            runningRef[i] += tailRef[i];
        }
        rows.push_back(Capture(strategy, "add tail c*x^" + std::to_string(term.power),
                               cc, sk, acc, runningRef,
                               std::chrono::duration<double>(t1 - t0).count()));
    }
    return acc;
}

EvalOutput RunExpandedEager(CC cc,
                            const PrivateKey<DCRTPoly>& sk,
                            const Ciphertext<DCRTPoly>& input) {
    EvalOutput out;
    out.trace.reserve(80);
    const auto components = BuildComponentSets(cc, sk, "expanded-eager", input, out.trace, out.stats);

    std::vector<Ciphertext<DCRTPoly>> materializedTerms;
    materializedTerms.reserve(kT3Terms.size());
    for (const auto& term : kT3Terms) {
        auto lhs = FindPower(components, term.a)->Clone();
        auto rhs = FindPower(components, term.b)->Clone();
        AlignPairForProduct(cc, lhs, rhs, out.stats);
        auto prefix = MaterializedProduct(
            cc, sk, "expanded-eager",
            "term prefix x^" + std::to_string(term.a) + "*x^" + std::to_string(term.b),
            lhs, rhs, term.a + term.b, out.trace, out.stats);

        auto prefixForFinal = prefix->Clone();
        auto finalRhs = FindPower(components, term.c)->Clone();
        AlignPairForProduct(cc, prefixForFinal, finalRhs, out.stats);
        auto product = MaterializedProduct(
            cc, sk, "expanded-eager",
            "term prefix*x^" + std::to_string(term.c),
            prefixForFinal, finalRhs, TermPower(term), out.trace, out.stats);
        product = Scale(cc, product, term.coeff, out.stats);
        out.trace.push_back(Capture("expanded-eager",
                                    "scaled term c*x^" + std::to_string(TermPower(term)),
                                    cc, sk, product,
                                    RefScaledPower(TermPower(term), term.coeff), 0.0));
        materializedTerms.push_back(product);
    }

    auto block = AddTerms(cc, sk, "expanded-eager", "t=3 materialized block sum",
                          materializedTerms, RefT3Block(), out.trace, out.stats);
    out.value = AddTailTerms(cc, sk, "expanded-eager", block, components, out.trace, out.stats);
    return out;
}

EvalOutput RunGroupedLazy(CC cc,
                          const PrivateKey<DCRTPoly>& sk,
                          const Ciphertext<DCRTPoly>& input) {
    EvalOutput out;
    out.trace.reserve(80);
    auto components = BuildComponentSets(cc, sk, "grouped-lazy", input, out.trace, out.stats);
    components = AlignComponents(cc, sk, "grouped-lazy", components, out.trace, out.stats);

    std::vector<Ciphertext<DCRTPoly>> rawTerms;
    rawTerms.reserve(kT3Terms.size());
    for (const auto& term : kT3Terms) {
        auto prefix = RawProduct(
            cc, sk, "grouped-lazy",
            "term prefix x^" + std::to_string(term.a) + "*x^" + std::to_string(term.b),
            FindPower(components, term.a),
            FindPower(components, term.b),
            term.a + term.b,
            out.trace,
            out.stats);

        auto raw = RawProduct(
            cc, sk, "grouped-lazy",
            "term raw prefix*x^" + std::to_string(term.c),
            prefix,
            FindPower(components, term.c),
            TermPower(term),
            out.trace,
            out.stats);
        raw = Scale(cc, raw, term.coeff, out.stats);
        out.trace.push_back(Capture("grouped-lazy",
                                    "raw scaled term c*x^" + std::to_string(TermPower(term)),
                                    cc, sk, raw,
                                    RefScaledPower(TermPower(term), term.coeff), 0.0));
        rawTerms.push_back(raw);
    }

    auto rawSum = AddTerms(cc, sk, "grouped-lazy", "raw t=3 terms folded",
                           rawTerms, RefT3Block(), out.trace, out.stats);

    auto t0 = Clock::now();
    auto block = Materialize(cc, rawSum, out.stats);
    auto t1 = Clock::now();
    out.trace.push_back(Capture("grouped-lazy", "folded t=3 block materialized",
                                cc, sk, block, RefT3Block(),
                                std::chrono::duration<double>(t1 - t0).count()));

    out.value = AddTailTerms(cc, sk, "grouped-lazy", block, components, out.trace, out.stats);
    return out;
}

void RunOneMode(const std::string& modeName, ScalingTechnique scalTech) {
    std::cout << "\n============================================================\n";
    std::cout << "[EVALUATOR] asymmetric prototype t=3 fixed polynomial evaluator\n";
    std::cout << "[PROJECT DECISION] fixed t=3 polynomial path, not a general planner\n";
    std::cout << "[POLY] P(x)=0.25 -0.30*x +0.12*x^4 + t3_block(x)\n";
    std::cout << "[T3 BLOCK] 0.15*x^7 -0.25*x^8 +0.35*x^9 -0.20*x^11\n";
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

    auto eager = RunExpandedEager(cc, kp.secretKey, ct);
    auto lazy = RunGroupedLazy(cc, kp.secretKey, ct);

    const auto ref = RefPolynomial();
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
    std::cout << "  note                      = fixed polynomial evaluator around one t=3 grouped block\n";

    if (eagerErr >= 1e-8 || lazyErr >= 1e-8) {
        throw std::runtime_error("asym proto t3 poly eval error threshold failed");
    }
    if (lazy.stats.relinCount > eager.stats.relinCount ||
        lazy.stats.rescaleCount > eager.stats.rescaleCount) {
        throw std::runtime_error("asym proto t3 poly eval lazy switch count is worse than eager");
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
