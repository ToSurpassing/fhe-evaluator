#include "internal_bsgs_common.h"
#include "smoke_common.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fhe_smoke;
using namespace fhe_eval::internal_bsgs;
using Clock = std::chrono::steady_clock;

namespace {

constexpr double kErrThreshold = 1e-8;

struct BenchmarkConfig {
    size_t warmup = 1;
    size_t repeat = 3;
};

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

struct TimedOutput {
    double seconds = 0.0;
    EvalOutput output;
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

CC BuildBenchmarkContext(ScalingTechnique scalTech) {
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

TimedOutput TimeRunInternalEager(CC cc,
                                 const InternalBsgsPlan& plan,
                                 const Ciphertext<DCRTPoly>& input) {
    const auto t0 = Clock::now();
    auto output = RunInternalEager(cc, plan, input);
    const auto t1 = Clock::now();
    return TimedOutput{std::chrono::duration<double>(t1 - t0).count(), output};
}

TimedOutput TimeRunInternalLazy(CC cc,
                                const InternalBsgsPlan& plan,
                                const Ciphertext<DCRTPoly>& input) {
    const auto t0 = Clock::now();
    auto output = RunInternalLazy(cc, plan, input);
    const auto t1 = Clock::now();
    return TimedOutput{std::chrono::duration<double>(t1 - t0).count(), output};
}

double Mean(const std::vector<double>& values) {
    if (values.empty()) {
        throw std::runtime_error("Mean: empty values");
    }
    return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
}

double Median(std::vector<double> values) {
    if (values.empty()) {
        throw std::runtime_error("Median: empty values");
    }
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[mid];
    }
    return 0.5 * (values[mid - 1] + values[mid]);
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

void PrintMarkdownHeader() {
    std::cout << "| pattern | mode | path | warmup | repeat | tensor | relin | rescale | "
              << "median_sec | mean_sec | final_err |\n";
    std::cout << "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n";
}

size_t ParsePositiveSize(const std::string& name, const std::string& value) {
    size_t consumed = 0;
    size_t parsed = 0;
    try {
        parsed = std::stoull(value, &consumed);
    }
    catch (const std::exception&) {
        throw std::runtime_error(name + " must be a positive integer");
    }
    if (consumed != value.size() || parsed == 0) {
        throw std::runtime_error(name + " must be a positive integer");
    }
    return parsed;
}

void PrintUsage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [--warmup N] [--repeat N]\n\n";
    std::cout << "Defaults: --warmup 1 --repeat 3\n";
}

BenchmarkConfig ParseArgs(int argc, char** argv) {
    BenchmarkConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            std::exit(0);
        }
        if (arg == "--warmup" || arg == "--repeat") {
            if (i + 1 >= argc) {
                throw std::runtime_error(arg + " requires a value");
            }
            const auto value = ParsePositiveSize(arg, argv[++i]);
            if (arg == "--warmup") {
                config.warmup = value;
            }
            else {
                config.repeat = value;
            }
            continue;
        }
        throw std::runtime_error("unknown argument: " + arg);
    }
    return config;
}

void BenchmarkOne(const CoeffPattern& pattern, ScalingTechnique scalTech, const BenchmarkConfig& config) {
    const auto referencePlan = ReferencePlanForPattern(pattern);
    const auto plan = GenerateInternalPlanFromCoeffs(pattern);
    ValidateInternalPlan(referencePlan);
    ValidateInternalPlan(plan);
    ValidateGeneratedPlanMatchesCoeffs(referencePlan, pattern);
    ValidateGeneratedPlanMatchesCoeffs(plan, pattern);

    auto cc = BuildBenchmarkContext(scalTech);
    auto kp = cc->KeyGen();
    cc->EvalMultKeysGen(kp.secretKey);

    Plaintext pt = cc->MakeCKKSPackedPlaintext(kInput);
    pt->SetLength(kSlots);
    auto ct = cc->Encrypt(kp.publicKey, pt);

    for (size_t i = 0; i < config.warmup; ++i) {
        (void)RunInternalEager(cc, plan, ct);
        (void)RunInternalLazy(cc, plan, ct);
    }

    std::vector<double> eagerTimes;
    std::vector<double> lazyTimes;
    EvalOutput lastEager;
    EvalOutput lastLazy;
    for (size_t i = 0; i < config.repeat; ++i) {
        auto eager = TimeRunInternalEager(cc, plan, ct);
        auto lazy = TimeRunInternalLazy(cc, plan, ct);
        eagerTimes.push_back(eager.seconds);
        lazyTimes.push_back(lazy.seconds);
        lastEager = eager.output;
        lastLazy = lazy.output;
    }

    const double eagerErr = FinalErr(cc, kp.secretKey, lastEager.value, plan);
    const double lazyErr = FinalErr(cc, kp.secretKey, lastLazy.value, plan);
    if (eagerErr >= kErrThreshold || lazyErr >= kErrThreshold) {
        throw std::runtime_error("BenchmarkOne: final error threshold failed");
    }

    std::cout << "| " << pattern.name
              << " | " << ModeName(scalTech)
              << " | eager"
              << " | " << config.warmup
              << " | " << config.repeat
              << " | " << lastEager.stats.tensorProducts
              << " | " << lastEager.stats.relinCount
              << " | " << lastEager.stats.rescaleCount
              << " | " << std::scientific << Median(eagerTimes)
              << " | " << std::scientific << Mean(eagerTimes)
              << " | " << std::scientific << eagerErr
              << " |\n";

    std::cout << "| " << pattern.name
              << " | " << ModeName(scalTech)
              << " | inner-lazy"
              << " | " << config.warmup
              << " | " << config.repeat
              << " | " << lastLazy.stats.tensorProducts
              << " | " << lastLazy.stats.relinCount
              << " | " << lastLazy.stats.rescaleCount
              << " | " << std::scientific << Median(lazyTimes)
              << " | " << std::scientific << Mean(lazyTimes)
              << " | " << std::scientific << lazyErr
              << " |\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto config = ParseArgs(argc, argv);
        std::cout << "# Internal BSGS Runtime Benchmark\n\n";
        std::cout << "This benchmark measures core evaluator runtime only. "
                  << "Key generation and encryption are outside the timed region.\n\n";
        std::cout << "Configuration: warmup=" << config.warmup
                  << ", repeat=" << config.repeat << "\n\n";
        PrintMarkdownHeader();
        for (const auto& pattern : KnownCoeffPatterns()) {
            BenchmarkOne(pattern, FIXEDMANUAL, config);
            BenchmarkOne(pattern, COMPOSITESCALINGMANUAL, config);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "\n[EXCEPTION] " << e.what() << '\n';
        return 1;
    }
    return 0;
}
