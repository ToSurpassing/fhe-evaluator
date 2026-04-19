#ifndef FHE_EVALUATOR_CKKS_LAZY_POLY_EVALUATOR_H
#define FHE_EVALUATOR_CKKS_LAZY_POLY_EVALUATOR_H

#include "smoke_common.h"

#include <string>
#include <vector>

namespace fhe_eval {

using fhe_smoke::CC;
using namespace lbcrypto;

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

struct EvalResult {
    Ciphertext<DCRTPoly> value;
    EvalStats stats;
    std::vector<TraceRow> trace;
};

struct PairedEvalResult {
    EvalResult expandedEager;
    EvalResult groupedLazy;
};

struct RestrictedDegree8Plan {
    std::vector<double> coeffs;
    std::vector<double> plaintextInput;
    size_t slots = fhe_smoke::kSlots;
};

struct Degree8BlockSummary {
    std::string name;
    size_t terms = 0;
    std::string outerMultiplier;
};

struct Degree8PlanSummary {
    size_t block0Terms = 0;
    size_t block1Terms = 0;
    size_t tailTerms = 0;
    bool hasC0 = false;
    bool hasC1 = false;
    bool hasC5 = false;
    std::vector<Degree8BlockSummary> blocks;
};

// First reusable prototype extracted from the validated 10_* experiment.
//
// Supported coefficients:
//   c0 + c1*x + c2*x^2 + ... + c8*x^8
//
// The implementation uses fixed block size 4 with z=x^4:
//   P(x) = b0(x) + b1(x)*z,
//   b0 uses c2,c3,c4 and b1 uses c6,c7,c8.
// Terms c0, c1*x, and c5*x^5 are handled as materialized tail terms in this
// restricted prototype.
PairedEvalResult EvalRestrictedDegree8(
    CC cc,
    const PrivateKey<DCRTPoly>& sk,
    const Ciphertext<DCRTPoly>& input,
    const RestrictedDegree8Plan& plan);

Degree8PlanSummary SummarizeRestrictedDegree8Plan(const std::vector<double>& coeffs);

std::string FormatDegree8PlanSummary(const Degree8PlanSummary& summary);

std::vector<double> EvalRestrictedDegree8Plain(
    const std::vector<double>& input,
    const std::vector<double>& coeffs);

}  // namespace fhe_eval

#endif  // FHE_EVALUATOR_CKKS_LAZY_POLY_EVALUATOR_H
