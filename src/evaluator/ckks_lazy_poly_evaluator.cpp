#include "ckks_lazy_poly_evaluator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace fhe_eval {

using Clock = std::chrono::high_resolution_clock;

namespace {

struct BlockTerm {
    size_t power = 0;
    double coeff = 0.0;
};

struct TailCoeffs {
    double c0 = 0.0;
    double c1 = 0.0;
    double c5 = 0.0;
};

enum class OuterMultiplier {
    One,
    Z,
};

enum class Degree8Layout {
    TwoBlockZ4,
    CompactActive,
};

struct BlockPlan {
    std::string name;
    std::vector<BlockTerm> terms;
    OuterMultiplier outerMultiplier = OuterMultiplier::One;
};

struct Degree8ExecutionPlan {
    Degree8Layout layout = Degree8Layout::TwoBlockZ4;
    std::vector<BlockPlan> blocks;
    TailCoeffs tail;
};

enum class EvaluationStrategy {
    ExpandedEager,
    GroupedLazy,
};

struct Basis {
    Ciphertext<DCRTPoly> input;
    Ciphertext<DCRTPoly> x;
    Ciphertext<DCRTPoly> x2;
    Ciphertext<DCRTPoly> z;
};

struct EvaluatedBlock {
    std::string name;
    std::vector<BlockTerm> terms;
    OuterMultiplier outerMultiplier = OuterMultiplier::One;
    Ciphertext<DCRTPoly> value;
};

constexpr double kZeroTol = 1e-14;

double CoeffAt(const std::vector<double>& coeffs, size_t i) {
    return i < coeffs.size() ? coeffs[i] : 0.0;
}

bool IsActiveCoeff(double coeff) {
    return std::abs(coeff) > kZeroTol;
}

bool IsActiveTerm(const BlockTerm& term) {
    return IsActiveCoeff(term.coeff);
}

size_t CountActiveTerms(const std::vector<BlockTerm>& terms) {
    return static_cast<size_t>(std::count_if(terms.begin(), terms.end(), IsActiveTerm));
}

bool IsActiveBlock(const std::vector<BlockTerm>& terms) {
    return CountActiveTerms(terms) > 0;
}

bool IsActiveBlock(const BlockPlan& block) {
    return IsActiveBlock(block.terms);
}

bool BlockNeedsX2(const BlockPlan& block) {
    return std::any_of(block.terms.begin(), block.terms.end(), [](const BlockTerm& term) {
        return IsActiveTerm(term) && term.power >= 2;
    });
}

bool AnyBlockNeedsX2(const Degree8ExecutionPlan& execPlan) {
    return std::any_of(execPlan.blocks.begin(), execPlan.blocks.end(), BlockNeedsX2);
}

bool NeedsZ(const Degree8ExecutionPlan& execPlan) {
    return std::any_of(execPlan.blocks.begin(), execPlan.blocks.end(), [](const BlockPlan& block) {
               return IsActiveBlock(block) && block.outerMultiplier == OuterMultiplier::Z;
           }) ||
           IsActiveCoeff(execPlan.tail.c5);
}

bool BlockUsesTensorProducts(const std::vector<BlockTerm>& terms) {
    return std::any_of(terms.begin(), terms.end(), [](const BlockTerm& term) {
        return IsActiveTerm(term) && term.power >= 2;
    });
}

void RejectMixedLinearBlock(const std::vector<BlockTerm>& terms, const std::string& blockName) {
    const bool hasLinear = std::any_of(terms.begin(), terms.end(), [](const BlockTerm& term) {
        return IsActiveTerm(term) && term.power == 1;
    });
    const bool hasNonlinear = std::any_of(terms.begin(), terms.end(), [](const BlockTerm& term) {
        return IsActiveTerm(term) && term.power >= 2;
    });
    if (hasLinear && hasNonlinear) {
        throw std::runtime_error(blockName + ": mixed linear/nonlinear block terms are not supported yet");
    }
}

std::vector<double> ZeroRef(size_t size) {
    return std::vector<double>(size, 0.0);
}

std::string StrategyName(EvaluationStrategy strategy) {
    switch (strategy) {
        case EvaluationStrategy::ExpandedEager:
            return "expanded-eager";
        case EvaluationStrategy::GroupedLazy:
            return "grouped-lazy";
    }
    throw std::runtime_error("StrategyName: unknown evaluation strategy");
}

std::string OuterMultiplierName(OuterMultiplier multiplier) {
    switch (multiplier) {
        case OuterMultiplier::One:
            return "One";
        case OuterMultiplier::Z:
            return "Z";
    }
    throw std::runtime_error("OuterMultiplierName: unknown outer multiplier");
}

std::string LayoutName(Degree8Layout layout) {
    switch (layout) {
        case Degree8Layout::TwoBlockZ4:
            return "two-block-z4";
        case Degree8Layout::CompactActive:
            return "compact-active";
    }
    throw std::runtime_error("LayoutName: unknown degree8 layout");
}

Degree8Layout SelectDegree8Layout(const std::vector<BlockTerm>& block0,
                                  const std::vector<BlockTerm>& block1) {
    return IsActiveBlock(block0) && IsActiveBlock(block1) ?
           Degree8Layout::TwoBlockZ4 :
           Degree8Layout::CompactActive;
}

std::vector<BlockTerm> MakeBlockTerms(double c2, double c3, double c4) {
    return std::vector<BlockTerm>{
        BlockTerm{2, c2},
        BlockTerm{3, c3},
        BlockTerm{4, c4},
    };
}

Degree8ExecutionPlan BuildDegree8ExecutionPlan(const std::vector<double>& coeffs) {
    const auto block0 = MakeBlockTerms(CoeffAt(coeffs, 2), CoeffAt(coeffs, 3), CoeffAt(coeffs, 4));
    const auto block1 = MakeBlockTerms(CoeffAt(coeffs, 6), CoeffAt(coeffs, 7), CoeffAt(coeffs, 8));
    const double c1 = CoeffAt(coeffs, 1);
    const double c5 = CoeffAt(coeffs, 5);
    const auto layout = SelectDegree8Layout(block0, block1);
    const bool useLinearBlock = IsActiveCoeff(c1) &&
                                !IsActiveBlock(block1) &&
                                !IsActiveCoeff(c5);

    std::vector<BlockPlan> blocks;
    if (useLinearBlock) {
        blocks.push_back(BlockPlan{"linear", std::vector<BlockTerm>{BlockTerm{1, c1}}, OuterMultiplier::One});
    }
    if (layout == Degree8Layout::TwoBlockZ4 || IsActiveBlock(block0)) {
        blocks.push_back(BlockPlan{"block0", block0, OuterMultiplier::One});
    }
    if (layout == Degree8Layout::TwoBlockZ4 || IsActiveBlock(block1)) {
        blocks.push_back(BlockPlan{"block1", block1, OuterMultiplier::Z});
    }

    return Degree8ExecutionPlan{
        layout,
        blocks,
        TailCoeffs{CoeffAt(coeffs, 0), useLinearBlock ? 0.0 : c1, c5}
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

double PlainPower(double x, size_t power) {
    double y = 1.0;
    for (size_t i = 0; i < power; ++i) {
        y *= x;
    }
    return y;
}

double PlainBlock(double x, const std::vector<BlockTerm>& terms) {
    double y = 0.0;
    for (const auto& term : terms) {
        y += term.coeff * PlainPower(x, term.power);
    }
    return y;
}

std::vector<double> RefBlock(const std::vector<double>& input, const std::vector<BlockTerm>& terms) {
    std::vector<double> ref;
    ref.reserve(input.size());
    for (double x : input) {
        ref.push_back(PlainBlock(x, terms));
    }
    return ref;
}

std::vector<double> RefOuterProduct(const std::vector<double>& input,
                                    const std::vector<BlockTerm>& terms) {
    std::vector<double> ref;
    ref.reserve(input.size());
    for (double x : input) {
        ref.push_back(PlainBlock(x, terms) * x * x * x * x);
    }
    return ref;
}

std::vector<double> AddRefs(std::vector<double> lhs, const std::vector<double>& rhs) {
    if (lhs.size() != rhs.size()) {
        throw std::runtime_error("AddRefs: reference vectors must have the same size");
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        lhs[i] += rhs[i];
    }
    return lhs;
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
                 const Degree8ExecutionPlan& execPlan,
                 const RestrictedDegree8Plan& plan,
                 EvalResult& result) {
    AddTrace(result.trace, strategy, "x input", cc, sk, inputCt, inputPlain, plan.slots, 0.0);

    const bool needX2 = AnyBlockNeedsX2(execPlan) || NeedsZ(execPlan);
    if (!needX2) {
        return Basis{inputCt->Clone(), inputCt->Clone(), nullptr, nullptr};
    }

    auto x2 = MaterializedProduct(cc, sk, strategy, "precompute x^2", inputCt, inputCt,
                                  RefPower(inputPlain, 2), plan, result);

    auto xAligned = inputCt->Clone();
    RaiseNoiseScaleDegTo(cc, xAligned, x2->GetNoiseScaleDeg(), result.stats);
    ReduceToLevel(cc, xAligned, x2->GetLevel(), result.stats);
    AddTrace(result.trace, strategy, "x aligned with x^2", cc, sk, xAligned,
             inputPlain, plan.slots, 0.0);

    if (!NeedsZ(execPlan)) {
        return Basis{inputCt->Clone(), xAligned, x2, nullptr};
    }

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
                                            const std::vector<BlockTerm>& blockTerms,
                                            const Basis& basis,
                                            const std::vector<double>& inputPlain,
                                            const RestrictedDegree8Plan& plan,
                                            EvalResult& result) {
    RejectMixedLinearBlock(blockTerms, "EvalBlockExpandedEager");

    std::vector<Ciphertext<DCRTPoly>> preparedTerms;
    preparedTerms.reserve(blockTerms.size() + 1);

    for (const auto& term : blockTerms) {
        if (!IsActiveTerm(term)) {
            continue;
        }

        if (term.power == 1) {
            auto xTerm = Scale(cc, basis.input->Clone(), term.coeff, result.stats);
            AddTrace(result.trace, "expanded-eager", blockName + " coeff*x", cc, sk, xTerm,
                     ScaleRef(inputPlain, term.coeff), plan.slots, 0.0);
            preparedTerms.push_back(xTerm);
        }
        else if (term.power == 2) {
            auto x2Term = MaterializedProduct(cc, sk, "expanded-eager", blockName + " x*x",
                                              basis.x, basis.x, RefPower(inputPlain, 2), plan, result);
            x2Term = Scale(cc, x2Term, term.coeff, result.stats);
            AddTrace(result.trace, "expanded-eager", blockName + " coeff*x^2", cc, sk, x2Term,
                     ScaleRef(RefPower(inputPlain, 2), term.coeff), plan.slots, 0.0);
            preparedTerms.push_back(x2Term);
        }
        else if (term.power == 3) {
            auto x3Left = MaterializedProduct(cc, sk, "expanded-eager", blockName + " x*x^2",
                                              basis.x, basis.x2, RefPower(inputPlain, 3), plan, result);
            x3Left = Scale(cc, x3Left, term.coeff * 0.5, result.stats);
            AddTrace(result.trace, "expanded-eager", blockName + " coeff/2*x*x^2", cc, sk, x3Left,
                     ScaleRef(RefPower(inputPlain, 3), term.coeff * 0.5), plan.slots, 0.0);
            preparedTerms.push_back(x3Left);

            auto x3Right = MaterializedProduct(cc, sk, "expanded-eager", blockName + " x^2*x",
                                               basis.x2, basis.x, RefPower(inputPlain, 3), plan, result);
            x3Right = Scale(cc, x3Right, term.coeff * 0.5, result.stats);
            AddTrace(result.trace, "expanded-eager", blockName + " coeff/2*x^2*x", cc, sk, x3Right,
                     ScaleRef(RefPower(inputPlain, 3), term.coeff * 0.5), plan.slots, 0.0);
            preparedTerms.push_back(x3Right);
        }
        else if (term.power == 4) {
            auto x4Term = MaterializedProduct(cc, sk, "expanded-eager", blockName + " x^2*x^2",
                                              basis.x2, basis.x2, RefPower(inputPlain, 4), plan, result);
            x4Term = Scale(cc, x4Term, term.coeff, result.stats);
            AddTrace(result.trace, "expanded-eager", blockName + " coeff*x^4", cc, sk, x4Term,
                     ScaleRef(RefPower(inputPlain, 4), term.coeff), plan.slots, 0.0);
            preparedTerms.push_back(x4Term);
        }
        else {
            throw std::runtime_error("EvalBlockExpandedEager: unsupported local block power");
        }
    }

    return AddPreparedTerms(cc, sk, "expanded-eager", blockName + " materialized coeff block",
                            preparedTerms, RefBlock(inputPlain, blockTerms), plan, result);
}

Ciphertext<DCRTPoly> EvalBlockGroupedLazy(CC cc,
                                          const PrivateKey<DCRTPoly>& sk,
                                          const std::string& blockName,
                                          const std::vector<BlockTerm>& blockTerms,
                                          const Basis& basis,
                                          const std::vector<double>& inputPlain,
                                          const RestrictedDegree8Plan& plan,
                                          EvalResult& result) {
    RejectMixedLinearBlock(blockTerms, "EvalBlockGroupedLazy");

    std::vector<Ciphertext<DCRTPoly>> preparedTerms;
    preparedTerms.reserve(blockTerms.size() + 1);

    for (const auto& term : blockTerms) {
        if (!IsActiveTerm(term)) {
            continue;
        }

        if (term.power == 1) {
            auto xTerm = Scale(cc, basis.input->Clone(), term.coeff, result.stats);
            AddTrace(result.trace, "grouped-lazy", blockName + " coeff*x", cc, sk, xTerm,
                     ScaleRef(inputPlain, term.coeff), plan.slots, 0.0);
            preparedTerms.push_back(xTerm);
        }
        else if (term.power == 2) {
            auto x2Term = RawProduct(cc, sk, "grouped-lazy", blockName + " x*x",
                                     basis.x, basis.x, RefPower(inputPlain, 2), plan, result);
            x2Term = Scale(cc, x2Term, term.coeff, result.stats);
            AddTrace(result.trace, "grouped-lazy", blockName + " raw coeff*x^2", cc, sk, x2Term,
                     ScaleRef(RefPower(inputPlain, 2), term.coeff), plan.slots, 0.0);
            preparedTerms.push_back(x2Term);
        }
        else if (term.power == 3) {
            auto x3Left = RawProduct(cc, sk, "grouped-lazy", blockName + " x*x^2",
                                     basis.x, basis.x2, RefPower(inputPlain, 3), plan, result);
            x3Left = Scale(cc, x3Left, term.coeff * 0.5, result.stats);
            AddTrace(result.trace, "grouped-lazy", blockName + " raw coeff/2*x*x^2", cc, sk, x3Left,
                     ScaleRef(RefPower(inputPlain, 3), term.coeff * 0.5), plan.slots, 0.0);
            preparedTerms.push_back(x3Left);

            auto x3Right = RawProduct(cc, sk, "grouped-lazy", blockName + " x^2*x",
                                      basis.x2, basis.x, RefPower(inputPlain, 3), plan, result);
            x3Right = Scale(cc, x3Right, term.coeff * 0.5, result.stats);
            AddTrace(result.trace, "grouped-lazy", blockName + " raw coeff/2*x^2*x", cc, sk, x3Right,
                     ScaleRef(RefPower(inputPlain, 3), term.coeff * 0.5), plan.slots, 0.0);
            preparedTerms.push_back(x3Right);
        }
        else if (term.power == 4) {
            auto x4Term = RawProduct(cc, sk, "grouped-lazy", blockName + " x^2*x^2",
                                     basis.x2, basis.x2, RefPower(inputPlain, 4), plan, result);
            x4Term = Scale(cc, x4Term, term.coeff, result.stats);
            AddTrace(result.trace, "grouped-lazy", blockName + " raw coeff*x^4", cc, sk, x4Term,
                     ScaleRef(RefPower(inputPlain, 4), term.coeff), plan.slots, 0.0);
            preparedTerms.push_back(x4Term);
        }
        else {
            throw std::runtime_error("EvalBlockGroupedLazy: unsupported local block power");
        }
    }

    auto rawBlock = AddPreparedTerms(cc, sk, "grouped-lazy", blockName + " raw coeff block folded",
                                     preparedTerms, RefBlock(inputPlain, blockTerms), plan, result);

    if (!BlockUsesTensorProducts(blockTerms)) {
        return rawBlock;
    }

    auto t0 = Clock::now();
    auto out = Materialize(cc, rawBlock, result.stats);
    auto t1 = Clock::now();
    AddTrace(result.trace, "grouped-lazy", blockName + " materialized coeff block",
             cc, sk, out, RefBlock(inputPlain, blockTerms), plan.slots,
             std::chrono::duration<double>(t1 - t0).count());
    return out;
}

Ciphertext<DCRTPoly> AssembleOuter(CC cc,
                                   const PrivateKey<DCRTPoly>& sk,
                                   const std::string& strategy,
                                   const std::vector<EvaluatedBlock>& blocks,
                                   const Basis& basis,
                                   const Degree8ExecutionPlan& execPlan,
                                   const std::vector<double>& inputPlain,
                                   const RestrictedDegree8Plan& plan,
                                   EvalResult& result) {
    const double c0 = execPlan.tail.c0;
    const double c1 = execPlan.tail.c1;
    const double c5 = execPlan.tail.c5;

    std::optional<Ciphertext<DCRTPoly>> acc;
    auto runningRef = ZeroRef(inputPlain.size());

    for (const auto& block : blocks) {
        std::vector<double> blockRef;
        Ciphertext<DCRTPoly> term;

        if (block.outerMultiplier == OuterMultiplier::One) {
            term = block.value->Clone();
            blockRef = RefBlock(inputPlain, block.terms);
        }
        else if (block.outerMultiplier == OuterMultiplier::Z) {
            blockRef = RefOuterProduct(inputPlain, block.terms);
            term = MaterializedProduct(cc, sk, strategy, "outer " + block.name + "*z",
                                       block.value, basis.z, blockRef, plan, result);
        }
        else {
            throw std::runtime_error("AssembleOuter: unknown block outer multiplier");
        }

        if (acc.has_value()) {
            AlignForAdd(cc, *acc, term, result.stats);
            const std::string alignStep = block.name == "block1" ?
                                          "block0 aligned for outer sum" :
                                          "acc aligned for " + block.name;
            AddTrace(result.trace, strategy, alignStep, cc, sk, *acc,
                     runningRef, plan.slots, 0.0);

            auto t0 = Clock::now();
            *acc = cc->EvalAdd(*acc, term);
            auto t1 = Clock::now();
            ++result.stats.addCount;
            const double dt = std::chrono::duration<double>(t1 - t0).count();
            result.stats.totalOpSec += dt;
            runningRef = AddRefs(runningRef, blockRef);
            const std::string addStep = block.name == "block1" ?
                                        "block0 + block1*z" :
                                        "add " + block.name;
            AddTrace(result.trace, strategy, addStep, cc, sk, *acc,
                     runningRef, plan.slots, dt);
        }
        else {
            acc = term;
            runningRef = blockRef;
            std::string startStep;
            if (block.outerMultiplier == OuterMultiplier::One) {
                startStep = block.name + " starts outer assembly";
            }
            else {
                startStep = "outer " + block.name + "*z starts assembly";
            }
            AddTrace(result.trace, strategy, startStep, cc, sk, *acc,
                     runningRef, plan.slots, 0.0);
        }
    }

    if (IsActiveCoeff(c5)) {
        auto xForX5 = basis.x->Clone();
        auto zForX5 = basis.z->Clone();
        AlignForAdd(cc, xForX5, zForX5, result.stats);
        auto x5Term = MaterializedProduct(cc, sk, strategy, "tail x*z=x^5",
                                          xForX5, zForX5, RefPower(inputPlain, 5), plan, result);
        x5Term = Scale(cc, x5Term, c5, result.stats);
        AddTrace(result.trace, strategy, "tail coeff*x^5", cc, sk, x5Term,
                 ScaleRef(RefPower(inputPlain, 5), c5), plan.slots, 0.0);
        if (acc.has_value()) {
            AlignForAdd(cc, *acc, x5Term, result.stats);
            auto t2 = Clock::now();
            *acc = cc->EvalAdd(*acc, x5Term);
            auto t3 = Clock::now();
            ++result.stats.addCount;
            result.stats.totalOpSec += std::chrono::duration<double>(t3 - t2).count();
            runningRef = AddScaledPowerRef(runningRef, inputPlain, 5, c5);
            AddTrace(result.trace, strategy, "add tail c5*x^5", cc, sk, *acc,
                     runningRef, plan.slots, std::chrono::duration<double>(t3 - t2).count());
        }
        else {
            acc = x5Term;
            runningRef = ScaleRef(RefPower(inputPlain, 5), c5);
            AddTrace(result.trace, strategy, "tail c5*x^5 starts assembly", cc, sk, *acc,
                     runningRef, plan.slots, 0.0);
        }
    }

    if (IsActiveCoeff(c1)) {
        auto xTerm = basis.input->Clone();
        xTerm = Scale(cc, xTerm, c1, result.stats);
        AddTrace(result.trace, strategy, "tail coeff*x", cc, sk, xTerm,
                 ScaleRef(inputPlain, c1), plan.slots, 0.0);
        if (acc.has_value()) {
            AlignForAdd(cc, *acc, xTerm, result.stats);
            auto t4 = Clock::now();
            *acc = cc->EvalAdd(*acc, xTerm);
            auto t5 = Clock::now();
            ++result.stats.addCount;
            result.stats.totalOpSec += std::chrono::duration<double>(t5 - t4).count();
            runningRef = AddScaledPowerRef(runningRef, inputPlain, 1, c1);
            AddTrace(result.trace, strategy, "add tail c1*x", cc, sk, *acc,
                     runningRef, plan.slots, std::chrono::duration<double>(t5 - t4).count());
        }
        else {
            acc = xTerm;
            runningRef = ScaleRef(inputPlain, c1);
            AddTrace(result.trace, strategy, "tail c1*x starts assembly", cc, sk, *acc,
                     runningRef, plan.slots, 0.0);
        }
    }

    if (!acc.has_value()) {
        acc = Scale(cc, basis.input->Clone(), 0.0, result.stats);
        AddTrace(result.trace, strategy, "zero accumulator", cc, sk, *acc,
                 runningRef, plan.slots, 0.0);
    }

    if (IsActiveCoeff(c0)) {
        auto t6 = Clock::now();
        *acc = cc->EvalAdd(*acc, c0);
        auto t7 = Clock::now();
        ++result.stats.addCount;
        result.stats.totalOpSec += std::chrono::duration<double>(t7 - t6).count();
        for (double& v : runningRef) {
            v += c0;
        }
        AddTrace(result.trace, strategy, "add tail c0", cc, sk, *acc,
                 runningRef, plan.slots, std::chrono::duration<double>(t7 - t6).count());
    }

    AddTrace(result.trace, strategy, "final degree<=8 result", cc, sk, *acc,
             EvalRestrictedDegree8Plain(inputPlain, plan.coeffs), plan.slots, 0.0);
    return *acc;
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

Ciphertext<DCRTPoly> EvalBlockForStrategy(CC cc,
                                          const PrivateKey<DCRTPoly>& sk,
                                          EvaluationStrategy strategy,
                                          const BlockPlan& block,
                                          const Basis& basis,
                                          const std::vector<double>& inputPlain,
                                          const RestrictedDegree8Plan& plan,
                                          EvalResult& result) {
    switch (strategy) {
        case EvaluationStrategy::ExpandedEager:
            return EvalBlockExpandedEager(cc, sk, block.name, block.terms, basis,
                                          inputPlain, plan, result);
        case EvaluationStrategy::GroupedLazy:
            return EvalBlockGroupedLazy(cc, sk, block.name, block.terms, basis,
                                        inputPlain, plan, result);
    }
    throw std::runtime_error("EvalBlockForStrategy: unknown evaluation strategy");
}

std::optional<EvaluatedBlock> EvalBlockIfActive(CC cc,
                                                const PrivateKey<DCRTPoly>& sk,
                                                EvaluationStrategy strategy,
                                                const BlockPlan& block,
                                                const Basis& basis,
                                                const std::vector<double>& inputPlain,
                                                const RestrictedDegree8Plan& plan,
                                                EvalResult& result) {
    if (!IsActiveBlock(block)) {
        return std::nullopt;
    }
    auto value = EvalBlockForStrategy(cc, sk, strategy, block, basis,
                                      inputPlain, plan, result);
    return EvaluatedBlock{block.name, block.terms, block.outerMultiplier, value};
}

EvalResult ExecuteDegree8Plan(CC cc,
                              const PrivateKey<DCRTPoly>& sk,
                              const Ciphertext<DCRTPoly>& input,
                              const RestrictedDegree8Plan& plan,
                              EvaluationStrategy strategy) {
    EvalResult result;
    result.trace.reserve(80);
    const auto execPlan = BuildDegree8ExecutionPlan(plan.coeffs);
    const auto strategyName = StrategyName(strategy);

    const auto basis = BuildBasis(cc, sk, strategyName, input, plan.plaintextInput,
                                  execPlan, plan, result);
    std::vector<EvaluatedBlock> blocks;
    blocks.reserve(execPlan.blocks.size());
    for (const auto& blockPlan : execPlan.blocks) {
        auto block = EvalBlockIfActive(cc, sk, strategy, blockPlan,
                                       basis, plan.plaintextInput, plan, result);
        if (block.has_value()) {
            blocks.push_back(*block);
        }
    }

    result.value = AssembleOuter(cc, sk, strategyName, blocks, basis,
                                 execPlan, plan.plaintextInput, plan, result);
    return result;
}

EvalResult RunExpandedEager(CC cc,
                            const PrivateKey<DCRTPoly>& sk,
                            const Ciphertext<DCRTPoly>& input,
                            const RestrictedDegree8Plan& plan) {
    return ExecuteDegree8Plan(cc, sk, input, plan, EvaluationStrategy::ExpandedEager);
}

EvalResult RunGroupedLazy(CC cc,
                          const PrivateKey<DCRTPoly>& sk,
                          const Ciphertext<DCRTPoly>& input,
                          const RestrictedDegree8Plan& plan) {
    return ExecuteDegree8Plan(cc, sk, input, plan, EvaluationStrategy::GroupedLazy);
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

Degree8PlanSummary SummarizeRestrictedDegree8Plan(const std::vector<double>& coeffs) {
    const auto execPlan = BuildDegree8ExecutionPlan(coeffs);
    Degree8PlanSummary summary;
    summary.layout = LayoutName(execPlan.layout);

    summary.blocks.reserve(execPlan.blocks.size());
    for (const auto& block : execPlan.blocks) {
        const size_t termCount = CountActiveTerms(block.terms);
        summary.blocks.push_back(
            Degree8BlockSummary{block.name, termCount, OuterMultiplierName(block.outerMultiplier)});
        if (block.name == "block0") {
            summary.block0Terms = termCount;
        }
        else if (block.name == "block1") {
            summary.block1Terms = termCount;
        }
    }

    summary.hasC0 = IsActiveCoeff(execPlan.tail.c0);
    summary.hasC1 = IsActiveCoeff(execPlan.tail.c1);
    summary.hasC5 = IsActiveCoeff(execPlan.tail.c5);
    summary.tailTerms = static_cast<size_t>(summary.hasC0) +
                        static_cast<size_t>(summary.hasC1) +
                        static_cast<size_t>(summary.hasC5);
    return summary;
}

std::string FormatDegree8PlanSummary(const Degree8PlanSummary& summary) {
    std::string out = summary.layout + " ";
    for (size_t i = 0; i < summary.blocks.size(); ++i) {
        const auto& block = summary.blocks[i];
        if (i > 0) {
            out += " ";
        }
        out += block.name + ":" + block.outerMultiplier + "=" + std::to_string(block.terms);
    }
    if (!summary.blocks.empty()) {
        out += " ";
    }
    out += "tail=" + std::to_string(summary.tailTerms) +
           " c0=" + std::string(summary.hasC0 ? "yes" : "no") +
           " c1=" + std::string(summary.hasC1 ? "yes" : "no") +
           " c5=" + std::string(summary.hasC5 ? "yes" : "no");
    return out;
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
