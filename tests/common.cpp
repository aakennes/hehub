#include "catch2/catch.hpp"
#include "common/mod_arith.h"
#include "common/ntt.h"
#include "common/permutation.h"
#include "common/rnspolynomial.h"
#include "primitives/ckks/ckks.h"

using namespace hehub;

TEST_CASE("RNS polynomial") {
    RnsPolynomial r1(4096, 3, std::vector<u64>{3, 5, 7});

    PolyDimensions poly_dim{4096, 3, std::vector<u64>{3, 5, 7}};
    RnsPolynomial r2(poly_dim);
    RnsPolynomial r3(poly_dim);

    RnsPolynomial r4(r2);
    RnsPolynomial r5(std::move(r1));
    r3 = r2;
    r4 = std::move(r2);

    r3.add_components(std::vector<u64>{11});
    r4.remove_components();

    REQUIRE(r3.component_count() == 4);
    REQUIRE(r4.component_count() == 2);
    REQUIRE(r5.component_count() == 3);

    REQUIRE_THROWS(RnsPolynomial(PolyDimensions{4096, 4, std::vector<u64>(3)}));
    REQUIRE_THROWS(RnsPolynomial(PolyDimensions{4095, 3, std::vector<u64>(3)}));
    REQUIRE_THROWS(RnsPolynomial(PolyDimensions{4097, 3, std::vector<u64>(3)}));
}

TEST_CASE("bit rev", "[.]") {
    REQUIRE(__bit_rev_naive_16(12345, 14) == __bit_rev_naive(12345, 14));
    REQUIRE(__bit_rev_naive_16(12345, 15) == __bit_rev_naive(12345, 15));
    REQUIRE(__bit_rev_naive_16(12345, 16) == __bit_rev_naive(12345, 16));

#ifdef HEHUB_DEBUG
    REQUIRE_NOTHROW(__bit_rev_naive(12345, 64));
    REQUIRE_THROWS(__bit_rev_naive(12345, -1));
    REQUIRE_THROWS(__bit_rev_naive(12345, 10000000));
    REQUIRE_THROWS(__bit_rev_naive(12345, 13));

    REQUIRE_NOTHROW(__bit_rev_naive_16(12345, 16));
    REQUIRE_THROWS(__bit_rev_naive_16(12345, -1));
    REQUIRE_THROWS(__bit_rev_naive_16(12345, 10000000));
    REQUIRE_THROWS(__bit_rev_naive_16(12345, 13));
#endif
}

/// Infinity norm on a simple (not RNS) polynomial
u64 simple_inf_norm(const RnsPolynomial &poly) {
    if (poly.component_count() != 1) {
        throw std::invalid_argument("poly");
    }
    if (poly.rep_form != PolyRepForm::coeff) {
        throw std::invalid_argument("poly");
    }

    u64 q = poly.modulus_at(0);
    u64 half_q = q / 2;
    u64 norm = 0;
    for (const auto &coeff : poly[0]) {
        if (coeff >= q) {
            throw std::logic_error("Not reduced strictly.");
        }
        if (coeff < half_q) {
            norm = std::max(coeff, norm);
        } else {
            norm = std::max(q - coeff, norm);
        }
    }

    return norm;
}

template <typename T>
bool all_close(const std::vector<T> &vec1, const std::vector<T> &vec2,
               double eps) {
    for (size_t i = 0; i < vec1.size(); i++) {
        if (i >= vec2.size() || std::abs(vec1[i] - vec2[i]) > eps) {
            return false;
        }
    }
    return true;
};

TEST_CASE("automorphism") {
    SECTION("involution") {
        u64 q = 65537;
        size_t poly_len = 8;
        RnsPolynomial poly(poly_len, 1, {q});

        // random polynomial with not too large norm
        u64 seed = 42;
        for (auto &v : poly[0]) {
            seed = seed * 4985348 + 93479384;
            v = seed % (q / 10);
        }
        ntt_negacyclic_inplace_lazy(poly);

        // check the involution
        auto involuted = involute(poly);
        REQUIRE(involute(involuted) == poly);

        // check the boundness property
        intt_negacyclic_inplace(poly);
        intt_negacyclic_inplace(involuted);
        REQUIRE(simple_inf_norm(poly) == simple_inf_norm(involuted));
    }
    SECTION("cycles") {
        u64 q = 65537;
        size_t poly_len = 8;
        RnsPolynomial poly(poly_len, 1, {q});

        // random polynomial with not too large norm
        u64 seed = 42;
        for (auto &v : poly[0]) {
            seed = seed * 4985348 + 93479384;
            v = seed % (q / 10);
        }
        ntt_negacyclic_inplace_lazy(poly);

        // check the cycles
        auto one_step = cycle(poly, 1);
        auto two_step = cycle(poly, 2);
        REQUIRE(cycle(one_step, poly_len / 2 - 1) == poly);
        REQUIRE(cycle(one_step, 1) == two_step);

        // check the boundness property
        intt_negacyclic_inplace(poly);
        intt_negacyclic_inplace(one_step);
        intt_negacyclic_inplace(two_step);
        REQUIRE(simple_inf_norm(poly) == simple_inf_norm(one_step));
        REQUIRE(simple_inf_norm(poly) == simple_inf_norm(two_step));
    }
    SECTION("involution on plain") {
        u64 q = 36028797017456641;
        size_t poly_len = 8;
        auto data_count = poly_len / 2;
        std::vector<cc_double> plain_data(data_count);
        std::vector<cc_double> data_conj;
        std::default_random_engine generator;
        std::normal_distribution<double> data_dist(0, 1);
        for (auto &d : plain_data) {
            d = {data_dist(generator), data_dist(generator)};
            data_conj.push_back(std::conj(d));
        }

        auto pt =
            ckks::simd_encode(plain_data, pow(2.0, 50), {poly_len, 1, {q}});
        ntt_negacyclic_inplace_lazy(pt);
        CkksPt involuted_pt = involute(pt);
        involuted_pt.scaling_factor = pt.scaling_factor;
        intt_negacyclic_inplace(involuted_pt);
        auto data_recovered = ckks::simd_decode<cc_double>(involuted_pt);

        REQUIRE(all_close(data_recovered, data_conj, pow(2.0, -45)));
    }
    SECTION("cycle on plain") {
        u64 q = 36028797017456641;
        size_t poly_len = 8;
        auto data_count = poly_len / 2;
        std::vector<cc_double> plain_data(data_count);
        std::vector<cc_double> data_rot(data_count);
        std::default_random_engine generator;
        std::normal_distribution<double> data_dist(0, 1);
        for (auto &d : plain_data) {
            d = {data_dist(generator), data_dist(generator)};
        }
        size_t step = GENERATE(1, 2, 3);
        for (size_t i = 0; i < data_count; i++) {
            data_rot[(i + step) % data_count] = plain_data[i];
        }

        auto pt =
            ckks::simd_encode(plain_data, pow(2.0, 50), {poly_len, 1, {q}});
        ntt_negacyclic_inplace_lazy(pt);
        CkksPt cycled_pt = cycle(pt, step);
        cycled_pt.scaling_factor = pt.scaling_factor;
        intt_negacyclic_inplace(cycled_pt);
        auto data_recovered = ckks::simd_decode<cc_double>(cycled_pt);

        REQUIRE(all_close(data_recovered, data_rot, pow(2.0, -45)));
    }
}
