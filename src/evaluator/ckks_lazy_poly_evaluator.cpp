#include "ckks_lazy_poly_evaluator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace fhe_eval {

using Clock = std::chrono::high_resolution_clock;

namespace {

struct BlockCoeffs {
    double x2 = 0.0;
    double x3 = 0.0;
    double x4 = 0.0;
};

struct TailCoeffs {
    double c0 = 0.0;
    double c1 = 0.0;
    double c5 = 0.0;
};

struct Degree8ExecutionPlan {
    BlockCoeffs block0;
    BlockCoeffs block1;
    TailCoeffs tail;
};

struct Basis {
    Ciphertext<DCRTPoly> input;
    Ciphertext<DCRTPoly> x;
    Ciphertext<DCRTPoly> x2;
    Ciphertext<DCRTPoly> z;
};

constexpr double kZeroTol = 1e-14;

double CoeffAt(const std::vector<double>& coeffs, size_t i) {
    return i < coeffs.size() ? coeffs[i] : 0.0;
}

Degree8ExecutionPlan BuildDegree8ExecutionPlan(const std::vector<double>& coeffs) {
    return Degree8ExecutionPlan{
        BlockCoeffs{CoeffAt(coeffs, 2), CoeffAt(coeffs, 3), CoeffAt(coeffs, 4)},
        BlockCoeffs{CoeffAt(coeffs, 6), CoeffAt(coeffs, 7), CoeffAt(coeffs, 8)},
        TailCoeffs{CoeffAt(coeffs, 0), CoeffAt(coeffs, 1), CoeffAt(coeffs, 5)}
    };
}

std::vector<double> RefPower(const std::vector<double>& input, size_t power) {
    std::vector<double> ref;
    ref.reserve(input.size());
    for (double x : input) {
        double y = 1.0;
        for (size_t i = 0; i < power; ++i) {
            y *= x;
        }
        ref.push_back(y);
    }
    return ref;
}

std::vector<double> ScaleRef(std::vector<double> ref, double scalar) {
    for (double& v : ref) {
        v *= scalar;
    }
    return ref;
}

double PlainBlock(double x, const BlockCoeffs& coeffs) {
    const double x2 = x * x;
    const double x3 = x2 * x;
    const double x4 = x2 * x2;
    return coeffs.x2 * x2 + coeffs.x3 * x3 + coeffs.x4 * x4;
}

std::vector<double> RefBlock(const std::vector<double>& input, const BlockCoeffs& coeffs) {
    std::vector<double> ref;
    ref.reserve(input.size());
    for (double x : input) {
        ref.push_back(PlainBlock(x, coeffs));
    }
    return ref;
}

std::vector<double> RefOuterProduct(const std::vector<double>& input, const BlockCoeffs& block1) {
    std::vector<double> ref;
    ref.reserve(input.size());
    for (double x : input) {
        ref.push_back(PlainBlock(x, block1) * x * x * x * x);
    }
    return ref;
}

std::vector<double> RefOuterBase(const std::vector<double>& input,
                                 const BlockCoeffs& block0,
                                 const BlockCoeffs& block1) {
    std::vector<double> ref;
    ref.reserve(input.size());
    for (double x : input) {
        ref.push_back(PlainBlock(x, block0) + PlainBlock(x, block1) * x * x * x * x);
    }
    return ref;
}

std::vector<double> AddScaledPowerRef(std::vector<double> ref,
                                      const std::vector<double>& input,
                                      size_t power,
                                      double scalar) {
    const auto scaled = ScaleRef(RefPower(input, power), scalar);
    for (size_t i = 0; i < ref.size(); ++i) {
        ref[i] += scaled[i];
    }
    return ref;
}

TraceRow Capture(const std::string& strategy,
                 const std::string& step,
                 CC cc,
                 const PrivateKey<DCRTPoly>& sk,
                 const Ciphertext<DCRTPoly>& ct,
                 const std::vector<double>& ref,
                 size_t slots,
                 double timeSec) {
    const auto dec = fhe_smoke::DecryptVec(cc, sk, ct, slots);
    return TraceRow{
        strategy,
        step,
        ct->GetLevel(),
        ct->GetNoiseScaleDeg(),
        ct->NumberCiphertextElements(),
        timeSec,
        fhe_smoke::MaxAbsErrReal(dec, ref)
    };
}

void AddTrace(std::vector<TraceRow>& trace,
              const std::string& strategy,
              const std::string& step,
              CC cc,
              const PrivateKey<DCRTPoly>& sk,
              const Ciphertext<DCRTPoly>& ct,
              const std::vector<double>& ref,
              size_t slots,
              double timeSec) {
    trace.push_back(Capture(strategy, step, cc, sk, ct, ref, slots, timeSec));
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
                                const RestrictedDegree8Plan& plan,
                                EvalResult& result) {
    auto t0 = Clock::now();
    auto raw = cc->EvalMultNoRelin(lhs, rhs);
    auto t1 = Clock::now();
    ++result.stats.tensorProducts;
    const double dt = std::chrono::duration<double>(t1 - t0).count();
    result.stats.totalOpSec += dt;
    AddTrace(result.trace, strategy, step + " raw product", cc, sk, raw, ref, plan.slots, dt);
    return raw;
}

Ciphertext<DCRTPoly> MaterializedProduct(CC cc,
                                         const PrivateKey<DCRTPoly>& sk,
                                         const std::string& strategy,
                                         const std::string& step,
                                         const Ciphertext<DCRTPoly>& lhs,
                                         const Ciphertext<DCRTPoly>& rhs,
                                         const std::vector<double>& ref,
                                         const RestrictedDegree8Plan& plan,
                                         EvalResult& result) {
    auto raw = RawProduct(cc, sk, strategy, step, lhs, rhs, ref, plan, result);
    auto t0 = Clock::now();
    auto out = Materialize(cc, raw, result.stats);
    auto t1 = Clock::now();
    AddTrace(result.trace, strategy, step + " materialized", cc, sk, out, ref, plan.slots,
             std::chrono::duration<double>(t1 - t0).count());
    return out;
}

void AlignForAdd(CC cc, Ciphertext<DCRTPoly>& lhs, Ciphertext<DCRTPoly>& rhs, EvalStats& stats) {
    const size_t targetNoiseScaleDeg = std::max(lhs->GetNoiseScaleDeg(), rhs->GetNoiseScaleDeg());
    RaiseNoiseScaleDegTo(cc, lhs, targetNoiseScaleDeg, stats);
    RaiseNoiseScaleDegTo(cc, rhs, targetNoiseScaleDeg, stats);

    const size_t targetLevel = std::max(lhs->GetLevel(), rhs->GetLevel());
    ReduceToLevel(cc, lhs, targetLevel, stats);
    ReduceToLevel(cc, rhs, targetLevel, stats);
}

Basis BuildBasis(CC cc,
                 const PrivateKey<DCRTPoly>& sk,
                 const std::string& strategy,
                 const Ciphertext<DCRTPoly>& inputCt,
                 const std::vector<double>& inputPlain,
                 const RestrictedDegree8Plan& plan,
                 EvalResult& result) {
    AddTrace(result.trace, strategy, "x input", cc, sk, inputCt, inputPlain, plan.slots, 0.0);

    auto x2 = MaterializedProduct(cc, sk, strategy, "precompute x^2", inputCt, inputCt,
                                  RefPower(inputPlain, 2), plan, result);

    auto xAligned = inputCt->Clone();
    RaiseNoiseScaleDegTo(cc, xAligned, x2->GetNoiseScaleDeg(), result.stats);
    ReduceToLevel(cc, xAligned, x2->GetLevel(), result.stats);
    AddTrace(result.trace, strategy, "x aligned with x^2", cc, sk, xAligned,
             inputPlain, plan.slots, 0.0);

    auto z = MaterializedProduct(cc, sk, strategy, "precompute z=x^4", x2, x2,
                                 RefPower(inputPlain, 4), plan, result);

    return Basis{inputCt->Clone(), xAligned, x2, z};
}

Ciphertext<DCRTPoly> AddPreparedTerms(CC cc,
                                      const PrivateKey<DCRTPoly>& sk,
                                      const std::string& strategy,
                                      const std::string& step,
                                      const std::vector<Ciphertext<DCRTPoly>>& terms,
                                      const std::vector<double>& ref,
                                      const RestrictedDegree8Plan& plan,
                                      EvalResult& result) {
    if (terms.empty()) {
        throw std::runtime_error("AddPreparedTerms: empty term list");
    }

    auto t0 = Clock::now();
    auto acc = terms[0];
    for (size_t i = 1; i < terms.size(); ++i) {
        acc = cc->EvalAdd(acc, terms[i]);
        ++result.stats.addCount;
    }
    auto t1 = Clock::now();
    const double dt = std::chrono::duration<double>(t1 - t0).count();
    result.stats.totalOpSec += dt;
    AddTrace(result.trace, strategy, step, cc, sk, acc, ref, plan.slots, dt);
    return acc;
}

Ciphertext<DCRTPoly> EvalBlockExpandedEager(CC cc,
                                            const PrivateKey<DCRTPoly>& sk,
                                            const std::string& blockName,
                                            const BlockCoeffs& coeffs,
                                            const Basis& basis,
                                            const std::vector<double>& inputPlain,
                                            const RestrictedDegree8Plan& plan,
                                            EvalResult& result) {
    std::vector<Ciphertext<DCRTPoly>> terms;
    terms.reserve(4);

    auto x2Term = MaterializedProduct(cc, sk, "expanded-eager", blockName + " x*x",
                                      basis.x, basis.x, RefPower(inputPlain, 2), plan, result);
    x2Term = Scale(cc, x2Term, coeffs.x2, result.stats);
    AddTrace(result.trace, "expanded-eager", blockName + " coeff*x^2", cc, sk, x2Term,
             ScaleRef(RefPower(inputPlain, 2), coeffs.x2), plan.slots, 0.0);
    terms.push_back(x2Term);

    auto x3Left = MaterializedProduct(cc, sk, "expanded-eager", blockName + " x*x^2",
                                      basis.x, basis.x2, RefPower(inputPlain, 3), plan, result);
    x3Left = Scale(cc, x3Left, coeffs.x3 * 0.5, result.stats);
    AddTrace(result.trace, "expanded-eager", blockName + " coeff/2*x*x^2", cc, sk, x3Left,
             ScaleRef(RefPower(inputPlain, 3), coeffs.x3 * 0.5), plan.slots, 0.0);
    terms.push_back(x3Left);

    auto x3Right = MaterializedProduct(cc, sk, "expanded-eager", blockName + " x^2*x",
                                       basis.x2, basis.x, RefPower(inputPlain, 3), plan, result);
    x3Right = Scale(cc, x3Right, coeffs.x3 * 0.5, result.stats);
    AddTrace(result.trace, "expanded-eager", blockName + " coeff/2*x^2*x", cc, sk, x3Right,
             ScaleRef(RefPower(inputPlain, 3), coeffs.x3 * 0.5), plan.slots, 0.0);
    terms.push_back(x3Right);

    auto x4Term = MaterializedProduct(cc, sk, "expanded-eager", blockName + " x^2*x^2",
                                      basis.x2, basis.x2, RefPower(inputPlain, 4), plan, result);
    x4Term = Scale(cc, x4Term, coeffs.x4, result.stats);
    AddTrace(result.trace, "expanded-eager", blockName + " coeff*x^4", cc, sk, x4Term,
             ScaleRef(RefPower(inputPlain, 4), coeffs.x4), plan.slots, 0.0);
    terms.push_back(x4Term);

    return AddPreparedTerms(cc, sk, "expanded-eager", blockName + " materialized coeff block",
                            terms, RefBlock(inputPlain, coeffs), plan, result);
}

Ciphertext<DCRTPoly> EvalBlockGroupedLazy(CC cc,
                                          const PrivateKey<DCRTPoly>& sk,
                                          const std::string& blockName,
                                          const BlockCoeffs& coeffs,
                                          const Basis& basis,
                                          const std::vector<double>& inputPlain,
                                          const RestrictedDegree8Plan& plan,
                                          EvalResult& result) {
    std::vector<Ciphertext<DCRTPoly>> terms;
    terms.reserve(4);

    auto x2Term = RawProduct(cc, sk, "grouped-lazy", blockName + " x*x",
                             basis.x, basis.x, RefPower(inputPlain, 2), plan, result);
    x2Term = Scale(cc, x2Term, coeffs.x2, result.stats);
    AddTrace(result.trace, "grouped-lazy", blockName + " raw coeff*x^2", cc, sk, x2Term,
             ScaleRef(RefPower(inputPlain, 2), coeffs.x2), plan.slots, 0.0);
    terms.push_back(x2Term);

    auto x3Left = RawProduct(cc, sk, "grouped-lazy", blockName + " x*x^2",
                             basis.x, basis.x2, RefPower(inputPlain, 3), plan, result);
    x3Left = Scale(cc, x3Left, coeffs.x3 * 0.5, result.stats);
    AddTrace(result.trace, "grouped-lazy", blockName + " raw coeff/2*x*x^2", cc, sk, x3Left,
             ScaleRef(RefPower(inputPlain, 3), coeffs.x3 * 0.5), plan.slots, 0.0);
    terms.push_back(x3Left);

    auto x3Right = RawProduct(cc, sk, "grouped-lazy", blockName + " x^2*x",
                              basis.x2, basis.x, RefPower(inputPlain, 3), plan, result);
    x3Right = Scale(cc, x3Right, coeffs.x3 * 0.5, result.stats);
    AddTrace(result.trace, "grouped-lazy", blockName + " raw coeff/2*x^2*x", cc, sk, x3Right,
             ScaleRef(RefPower(inputPlain, 3), coeffs.x3 * 0.5), plan.slots, 0.0);
    terms.push_back(x3Right);

    auto x4Term = RawProduct(cc, sk, "grouped-lazy", blockName + " x^2*x^2",
                             basis.x2, basis.x2, RefPower(inputPlain, 4), plan, result);
    x4Term = Scale(cc, x4Term, coeffs.x4, result.stats);
    AddTrace(result.trace, "grouped-lazy", blockName + " raw coeff*x^4", cc, sk, x4Term,
             ScaleRef(RefPower(inputPlain, 4), coeffs.x4), plan.slots, 0.0);
    terms.push_back(x4Term);

    auto rawBlock = AddPreparedTerms(cc, sk, "grouped-lazy", blockName + " raw coeff block folded",
                                     terms, RefBlock(inputPlain, coeffs), plan, result);

    auto t0 = Clock::now();
    auto out = Materialize(cc, rawBlock, result.stats);
    auto t1 = Clock::now();
    AddTrace(result.trace, "grouped-lazy", blockName + " materialized coeff block",
             cc, sk, out, RefBlock(inputPlain, coeffs), plan.slots,
             std::chrono::duration<double>(t1 - t0).count());
    return out;
}

Ciphertext<DCRTPoly> AssembleOuter(CC cc,
                                   const PrivateKey<DCRTPoly>& sk,
                                   const std::string& strategy,
                                   const Ciphertext<DCRTPoly>& block0,
                                   const Ciphertext<DCRTPoly>& block1,
                                   const Basis& basis,
                                   const Degree8ExecutionPlan& execPlan,
                                   const std::vector<double>& inputPlain,
                                   const RestrictedDegree8Plan& plan,
                                   EvalResult& result) {
    const auto& block0Coeffs = execPlan.block0;
    const auto& block1Coeffs = execPlan.block1;
    const double c0 = execPlan.tail.c0;
    const double c1 = execPlan.tail.c1;
    const double c5 = execPlan.tail.c5;

    auto outer = MaterializedProduct(cc, sk, strategy, "outer block1*z",
                                     block1, basis.z, RefOuterProduct(inputPlain, block1Coeffs),
                                     plan, result);

    auto b0 = block0->Clone();
    AlignForAdd(cc, b0, outer, result.stats);
    AddTrace(result.trace, strategy, "block0 aligned for outer sum", cc, sk, b0,
             RefBlock(inputPlain, block0Coeffs), plan.slots, 0.0);

    auto t0 = Clock::now();
    auto acc = cc->EvalAdd(b0, outer);
    auto t1 = Clock::now();
    ++result.stats.addCount;
    const double dt = std::chrono::duration<double>(t1 - t0).count();
    result.stats.totalOpSec += dt;
    auto runningRef = RefOuterBase(inputPlain, block0Coeffs, block1Coeffs);
    AddTrace(result.trace, strategy, "block0 + block1*z", cc, sk, acc, runningRef, plan.slots, dt);

    if (std::abs(c5) > kZeroTol) {
        auto xForX5 = basis.x->Clone();
        auto zForX5 = basis.z->Clone();
        AlignForAdd(cc, xForX5, zForX5, result.stats);
        auto x5Term = MaterializedProduct(cc, sk, strategy, "tail x*z=x^5",
                                          xForX5, zForX5, RefPower(inputPlain, 5), plan, result);
        x5Term = Scale(cc, x5Term, c5, result.stats);
        AddTrace(result.trace, strategy, "tail coeff*x^5", cc, sk, x5Term,
                 ScaleRef(RefPower(inputPlain, 5), c5), plan.slots, 0.0);
        AlignForAdd(cc, acc, x5Term, result.stats);
        auto t2 = Clock::now();
        acc = cc->EvalAdd(acc, x5Term);
        auto t3 = Clock::now();
        ++result.stats.addCount;
        result.stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
        runningRef = AddScaledPowerRef(runningRef, inputPlain, 5, c5);
        AddTrace(result.trace, strategy, "add tail c5*x^5", cc, sk, acc,
                 runningRef, plan.slots, std::chrono::duration<double>(t3 - t2).count());
    }

    if (std::abs(c1) > kZeroTol) {
        auto xTerm = basis.input->Clone();
        xTerm = Scale(cc, xTerm, c1, result.stats);
        AddTrace(result.trace, strategy, "tail coeff*x", cc, sk, xTerm,
                 ScaleRef(inputPlain, c1), plan.slots, 0.0);
        AlignForAdd(cc, acc, xTerm, result.stats);
        auto t4 = Clock::now();
        acc = cc->EvalAdd(acc, xTerm);
        auto t5 = Clock::now();
        ++result.stats.addCount;
        result.stats.totalOpSec += std::chrono::duration<double>(t5 - t4).count();
        runningRef = AddScaledPowerRef(runningRef, inputPlain, 1, c1);
        AddTrace(result.trace, strategy, "add tail c1*x", cc, sk, acc,
                 runningRef, plan.slots, std::chrono::duration<double>(t5 - t4).count());
    }

    if (std::abs(c0) > kZeroTol) {
        auto t6 = Clock::now();
        acc = cc->EvalAdd(acc, c0);
        auto t7 = Clock::now();
        ++result.stats.addCount;
        result.stats.totalOpSec += std::chrono::duration<double>(t7 - t6).count();
        for (double& v : runningRef) {
            v += c0;
        }
        AddTrace(result.trace, strategy, "add tail c0", cc, sk, acc,
                 runningRef, plan.slots, std::chrono::duration<double>(t7 - t6).count());
    }

    AddTrace(result.trace, strategy, "final degree<=8 result", cc, sk, acc,
             EvalRestrictedDegree8Plain(inputPlain, plan.coeffs), plan.slots, 0.0);
    return acc;
}

void ValidatePlan(const RestrictedDegree8Plan& plan) {
    if (plan.coeffs.size() > 9) {
        throw std::runtime_error("EvalRestrictedDegree8: coeffs size must be <= 9");
    }
    if (plan.plaintextInput.empty()) {
        throw std::runtime_error("EvalRestrictedDegree8: plaintextInput must be provided");
    }
    if (plan.plaintextInput.size() < plan.slots) {
        throw std::runtime_error("EvalRestrictedDegree8: plaintextInput must contain at least slots values");
    }
    if (plan.slots == 0) {
        throw std::runtime_error("EvalRestrictedDegree8: slots must be positive");
    }
}

EvalResult RunExpandedEager(CC cc,
                            const PrivateKey<DCRTPoly>& sk,
                            const Ciphertext<DCRTPoly>& input,
                            const RestrictedDegree8Plan& plan) {
    EvalResult result;
    result.trace.reserve(80);
    const auto execPlan = BuildDegree8ExecutionPlan(plan.coeffs);

    const auto basis = BuildBasis(cc, sk, "expanded-eager", input, plan.plaintextInput, plan, result);
    auto block0 = EvalBlockExpandedEager(cc, sk, "block0", execPlan.block0, basis,
                                         plan.plaintextInput, plan, result);
    auto block1 = EvalBlockExpandedEager(cc, sk, "block1", execPlan.block1, basis,
                                         plan.plaintextInput, plan, result);
    result.value = AssembleOuter(cc, sk, "expanded-eager", block0, block1, basis,
                                 execPlan, plan.plaintextInput, plan, result);
    return result;
}

EvalResult RunGroupedLazy(CC cc,
                          const PrivateKey<DCRTPoly>& sk,
                          const Ciphertext<DCRTPoly>& input,
                          const RestrictedDegree8Plan& plan) {
    EvalResult result;
    result.trace.reserve(80);
    const auto execPlan = BuildDegree8ExecutionPlan(plan.coeffs);

    const auto basis = BuildBasis(cc, sk, "grouped-lazy", input, plan.plaintextInput, plan, result);
    auto block0 = EvalBlockGroupedLazy(cc, sk, "block0", execPlan.block0, basis,
                                       plan.plaintextInput, plan, result);
    auto block1 = EvalBlockGroupedLazy(cc, sk, "block1", execPlan.block1, basis,
                                       plan.plaintextInput, plan, result);
    result.value = AssembleOuter(cc, sk, "grouped-lazy", block0, block1, basis,
                                 execPlan, plan.plaintextInput, plan, result);
    return result;
}

}  // namespace

std::vector<double> EvalRestrictedDegree8Plain(const std::vector<double>& input,
                                               const std::vector<double>& coeffs) {
    std::vector<double> ref;
    ref.reserve(input.size());
    for (double x : input) {
        double y = 0.0;
        double xp = 1.0;
        for (double coeff : coeffs) {
            y += coeff * xp;
            xp *= x;
        }
        ref.push_back(y);
    }
    return ref;
}

PairedEvalResult EvalRestrictedDegree8(CC cc,
                                       const PrivateKey<DCRTPoly>& sk,
                                       const Ciphertext<DCRTPoly>& input,
                                       const RestrictedDegree8Plan& plan) {
    ValidatePlan(plan);
    return PairedEvalResult{
        RunExpandedEager(cc, sk, input, plan),
        RunGroupedLazy(cc, sk, input, plan)
    };
}

}  // namespace fhe_eval
