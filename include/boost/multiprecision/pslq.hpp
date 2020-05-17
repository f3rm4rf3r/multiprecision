/*
 * Copyright Nick Thompson 2020
 * Use, modification and distribution are subject to the
 * Boost Software License, Version 1.0. (See accompanying file
 * LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */

// Mathematica has an implementation of PSLQ which has the following interface:
// FindIntegerNullVector[{E, Pi}, 100000]
// FindIntegerNullVector::norel: There is no integer null vector for {E,\[Pi]} with norm less than or equal to 100000.
// Or:
// FindIntegerNullVector[{E, \[Pi]}]
// FindIntegerNullVector::rnfu: FindIntegerNullVector has not found an integer null vector for {E,\[Pi]}.
// I don't like this, because it should default to telling us the norm, as it's coproduced by the computation.

// Maple's Interface:
// with(IntegerRelations)
// v := [1.57079..., 1.4142135]
// u := PSLQ(v)
// u:= [-25474, 56589]
// Maple's interface is in fact worse, because it gives the wrong answer, instead of recognizing the precision provided.

// David Bailey's interface in tpslqm2.f90 in https://www.davidhbailey.com/dhbsoftware/  in  mpfun-fort-v19.tar.gz
// subroutine pslqm2(idb, n nwds, rb, eps, x, iq, r)
// idb is debug level
// n is the length of input vector and output relation r.
// nwds if the full precision level in words.
// rb is the log10 os max size Euclidean norm of relation
// eps tolerance for full precision relation detection.
// x input vector
// iq output flag: 0 (unsuccessful), 1 successful.
// r output integer relation vector, if successful.

#ifndef BOOST_MULTIPRECISION_PSLQ_HPP
#define BOOST_MULTIPRECISION_PSLQ_HPP
#include <iostream>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <map>
#include <cmath>
#include <boost/math/constants/constants.hpp>
#if defined __has_include
#  if __has_include (<Eigen/Dense>)
#    include <Eigen/Dense>
#  else
   #error This file has a dependency on Eigen; you must have Eigen (http://eigen.tuxfamily.org/index.php?title=Main_Page) in your include paths.
#  endif
#endif

namespace boost::multiprecision {

// For debugging:
template<typename Real>
auto tiny_pslq_dictionary() {
    using std::sqrt;
    using namespace boost::math::constants;
    std::map<Real, std::string> m;
    m.emplace(pi<Real>(), "π");
    m.emplace(e<Real>(), "e");
    m.emplace(root_two<Real>(), "√2");
    m.emplace(ln_two<Real>(), "ln(2)");
    return m;
}
template<typename Real>
auto small_pslq_dictionary() {
    using std::sqrt;
    using std::log;
    using namespace boost::math::constants;
    std::map<Real, std::string> m;
    m.emplace(one_div_euler<Real>(), "1/γ");
    m.emplace(root_pi<Real>(), "√π");
    m.emplace(pi<Real>(), "π");
    m.emplace(log(pi<Real>()), "ln(π)");
    m.emplace(pi_sqr<Real>(), "π²");
    m.emplace(pi_cubed<Real>(), "π³");
    m.emplace(e<Real>(), "e");
    m.emplace(root_two<Real>(), "√2");
    m.emplace(root_three<Real>(), "√3");
    m.emplace(sqrt(static_cast<Real>(5)), "√5");
    m.emplace(sqrt(static_cast<Real>(7)), "√7");
    m.emplace(sqrt(static_cast<Real>(11)), "√11");
    m.emplace(euler<Real>(), "γ");
    // φ is linearly dependent on √5; its logarithm is not.
    m.emplace(log(phi<Real>()), "ln(φ)");
    m.emplace(catalan<Real>(), "G");
    m.emplace(glaisher<Real>(), "A");
    m.emplace(khinchin<Real>(), "K₀");
    m.emplace(zeta_three<Real>(), "ζ(3)");
    // To recover multiplicative relations we need the logarithms of small primes.
    m.emplace(log(static_cast<Real>(2)), "ln(2)");
    m.emplace(log(static_cast<Real>(3)), "ln(3)");
    m.emplace(log(static_cast<Real>(5)), "ln(5)");
    m.emplace(log(static_cast<Real>(7)), "ln(7)");
    m.emplace(log(static_cast<Real>(11)), "ln(11)");
    m.emplace(log(static_cast<Real>(13)), "ln(13)");
    m.emplace(log(static_cast<Real>(17)), "ln(17)");
    m.emplace(log(static_cast<Real>(19)), "ln(19)");
    return m;
}

// The PSLQ algorithm; partial sum of squares, lower trapezoidal decomposition.
// See: https://www.davidhbailey.com/dhbpapers/cpslq.pdf, section 3.
template<typename Real>
std::vector<std::pair<int64_t, Real>> pslq(std::vector<Real> & x, Real gamma) {
    std::vector<std::pair<int64_t, Real>> relation;
    if (!std::is_sorted(x.begin(), x.end())) {
        std::cerr << "Elements must be sorted in increasing order.\n";
        return relation;
    }
    using std::sqrt;
    if (gamma <= 2/sqrt(3)) {
        std::cerr << "γ > 2/√3 is required\n";
        return relation;
    }
    Real tmp = 1/Real(4) + 1/(gamma*gamma);
    Real tau = 1/sqrt(tmp);
    if (tau <= 1 || tau >= 2) {
        std::cerr << "τ ∈ (1, 2) is required.\n";
        return relation;
    }

    if (x.size() < 2) {
        std::cerr << "At least two values are required to find an integer relation.\n";
        return relation;
    }

    for (auto & t : x) {
        if (t == 0) {
            std::cerr << "Zero in the dictionary gives trivial relations.\n";
            return relation;
        }
        if (t < sqrt(std::numeric_limits<Real>::epsilon())) {
            std::cerr << "Super small elements in the dictionary gives spurious relations; more precision is required.\n";
            return relation;
        }

        if (t < 0) {
            std::cerr << "The algorithm is reflection invariant, so negative values in the dictionary should be removed.\n";
            return relation;
        }
    }

    // Are we computing too many square roots??? Should we use s instead?
    std::vector<Real> s_sq(x.size());
    s_sq.back() = x.back()*x.back();
    int64_t n = x.size();
    for (int64_t i = n - 2; i >= 0; --i) {
        s_sq[i] = s_sq[i+1] + x[i]*x[i];
    }
    
    Eigen::Matrix<Real, Eigen::Dynamic, Eigen::Dynamic> Hx(n, n-1);
    for (int64_t i = 0; i < n - 1; ++i) {
        for (int64_t j = 0; j < n - 1; ++j) {
            if (i < j) {
                Hx(i,j) = 0;
            }
            else if (i == j) {
                Hx(i,i) = sqrt(s_sq[i+1]/s_sq[i]);
            }
            else {
                // i > j:
                Hx(i,j) = -x[i]*x[j]/sqrt(s_sq[j]*s_sq[j+1]);
            }
        }
    }
    for (int64_t j = 0; j < n - 1; ++j) {
        Hx(n-1, j) = -x[n-1]*x[j]/sqrt(s_sq[j]*s_sq[j+1]);
    }

    // This validates that Hx is indeed lower trapezoidal,
    // but that's trival and verbose:
    //std::cout << "Hx = \n";
    //std::cout << Hx << "\n";

    // Validate the conditions of Lemma 1 in the referenced paper:
    // These tests should eventually be removed once we're confident that the code is correct.
    auto Hxnorm_sq = Hx.squaredNorm();
    if (abs(Hxnorm_sq/(n-1) - 1) > sqrt(std::numeric_limits<Real>::epsilon())) {
        std::cerr << "‖Hₓ‖² ≠ n - 1. Hence Lemma 1.ii of the reference has numerically failed; this is a bug.\n";
        return relation;
    }

    Eigen::Matrix<Real, Eigen::Dynamic, 1> y(x.size());
    for (int64_t i = 0; i < n; ++i) {
        y[i] = x[i]/sqrt(s_sq[0]);
    }
    auto v = y.transpose()*Hx;
    for (int64_t i = 0; i < n - 1; ++i) {
        if (abs(v[i])/(n-1) > sqrt(std::numeric_limits<Real>::epsilon())) {
            std::cerr << "xᵀHₓ ≠ 0; Lemma 1.iii of the reference cpslq has numerically failed; this is a bug.\n";
            return relation;
        }
    }
    //std::cout << "H, pre-reduction:\n" << Hx << "\n";
    
    using std::round;
    // Matrix D of Definition 4:
    Eigen::Matrix<Real, Eigen::Dynamic, Eigen::Dynamic> D = Eigen::Matrix<Real, Eigen::Dynamic, Eigen::Dynamic>::Identity(n, n);
    Eigen::Matrix<Real, Eigen::Dynamic, Eigen::Dynamic> A = Eigen::Matrix<Real, Eigen::Dynamic, Eigen::Dynamic>::Identity(n, n);
    Eigen::Matrix<Real, Eigen::Dynamic, Eigen::Dynamic> B = Eigen::Matrix<Real, Eigen::Dynamic, Eigen::Dynamic>::Identity(n, n);
    for (int64_t i = 1; i < n; ++i) {
        for (int64_t j = i - 1; j >= 0; --j) {
            Real q = round(Hx(i,j)/Hx(j,j));
            // This happens a lot because x_0 < x_1 < ...!
            // Sort them in decreasing order and it almost never happens.
            if (q == 0) {
                continue;
            }
            for (int64_t k = 0; k <= j; ++k)
            {
                Hx(i,k) = Hx(i,k) - q*Hx(j,k);
            }
            for (int64_t k = 0; k < n; ++k) {
                D(i,k) = D(i,k) - q*D(j,k);
                A(i,k) = A(i,k) - q*A(j,k);
                B(k,j) = B(k,j) + q*B(k,i);
            }
            y[j] += q*y[i];
        }
    }
    //std::cout << "D = \n" << D << "\n";
    //std::cout << "H, post-reduction:\n" << Hx << "\n";

    // It looks like this: https://www.davidhbailey.com/dhbpapers/pslq-cse.pdf
    // gives a more explicit description of the algorithm.

    std::cout << __LINE__ <<  ": Looking for max coeff:\n";
    Real max_coeff = 0;
    for (int64_t i = 0; i < n - 1; ++i) {
        if (abs(Hx(i,i)) > max_coeff) {
            max_coeff = abs(Hx(i,i));
        }
    }
    Real norm_bound = 1/max_coeff;
    std::cout << "Norm bound: " << norm_bound << "\n";
    Real max_acceptable_norm_bound = 10e10;
    int64_t iteration = 0;
    while (norm_bound < max_acceptable_norm_bound)
    {
        std::cout << "Beginning iteration " << iteration++ << "\n";
        std::cout << "Hx = \n" << Hx << "\n";
        std::cout << "A = \n" << A << "\n";
        std::cout << "B = \n" << B << "\n";
        std::cout << "y = \n" << y << "\n";
        // "1. Select m such that y^{i+1}|H_ii| is maximal when i = m":
        // (note my C indexing translated from DHB's Fortran indexing)
        Real gammai = gamma;
        Real max_term = 0;
        int64_t m = -1;
        for (int i = 0; i < n - 1; ++i) {
            Real term = gammai*abs(Hx(i,i));
            if (term > max_term) {
                max_term = term;
                m = i;
            }
            gammai *= gamma;
        }
        // "2. Exchange the entries of y indexed m and m + 1"
        if (m == n - 1) {
            std::cerr << "OMG: m = n- 1, swap gonna segfault.\n";
            return relation;
        }
        if (m < 0) {
            std::cerr << "OMG: m = - 1, swap gonna segfault.\n";
            return relation;
        }
        std::cout << "Swapping\n";
        std::swap(y[m], y[m+1]);
        // Swap the corresponding rows of A and H:
        A.row(m).swap(A.row(m+1));
        Hx.row(m).swap(Hx.row(m+1));
        // Swap the corresponding columns of B:
        B.col(m).swap(B.col(m+1));

        // "3. Remove the corner on H diagonal:"
        std::cout << "Removing corner:\n";
        if (m < n - 2) {
            std::cout << "Not yet in loop, m = " << m << ", n = " << n << "\n";
            Real t0 = Hx(m,m)*Hx(m,m) + Hx(m, m+1)*Hx(m, m+1);
            t0 = sqrt(t0);
            Real t1 = Hx(m,m)/t0;
            Real t2 = Hx(m,m+1)/t0;
            for (int64_t i = m; i < n - 1; ++i) {
                std::cout << "i = " << i << "/ " << n << "\n";
                Real t3 = Hx(i,m);
                Real t4 = Hx(i, m+1);
                Hx(i,m) = t1*t3 + t2*t4;
                Hx(i,m+1) = -t2*t3 + t1*t4;
            }
        }

        // "4. Reduce H:"
        std::cout << "Reducing H:\n";
        for (int64_t i = m+1; i < n - 1; ++i) {
            std::cout << "i = " << i << ", n = " << n << "\n";
            for (int64_t j = std::min(i-1, m+1); j >= 0; --j) {
                std::cout << "j = " << j << ", n = " << n << "\n";
                Real t = round(Hx(i,j)/Hx(j,j));
                if (t == 0) {
                    continue;
                }
                std::cout << "Update y\n";
                y[j] += t*y[i];
                std::cout << "Updating H\n";
                for (int64_t k = 0; k < j; ++k) {
                    Hx(i,k) = Hx(i,k) - t*Hx(j,k);
                }
                std::cout << "Updating A,B:\n";
                for (int64_t k = 0; k < n; ++k) {
                    A(i,k) = A(i,k) - t*A(j,k);
                    B(k,j) = B(k,j) + t*B(k,i);
                }
            }
        }

        std::cout << "Looking for a solution\n";
        // Look for a solution:
        for (int64_t i = 0; i < n; ++i) {
            if (abs(y[i]) < sqrt(std::numeric_limits<Real>::epsilon())) {
                std::cout << "We've found a solution!\n";
                return relation;
            }
        }

        std::cout << "Computing the norm bound:\n";
        max_coeff = 0;
        for (int64_t i = 0; i < n - 1; ++i) {
            if (abs(Hx(i,i)) > max_coeff) {
                max_coeff = abs(Hx(i,i));
            }
        }
        norm_bound = 1/max_coeff;
        std::cout << "Norm bound = " << norm_bound << "\n";
        std::cout << "Hit enter to continue\n";
        std::cin.get();
    }
    // stubbing it out . . .
    for (auto t : x) {
        relation.push_back({-8, t});
    }
    return relation;
}

template<typename Real>
std::vector<std::pair<int64_t, Real>> pslq(std::vector<Real> const & x) {
    Real gamma = 2/sqrt(3) + 0.01;
    return pslq(x, gamma);
}

template<typename Real>
std::string pslq(std::map<Real, std::string> const & dictionary, Real gamma) {    
    std::vector<Real> values(dictionary.size());
    size_t i = 0;
    for (auto it = dictionary.begin(); it != dictionary.end(); ++it) {
        values[i++] = it->first;
    }

    auto m = pslq(values, gamma);
    if (m.size() > 0) {
        std::ostringstream oss;
        auto const & symbol = dictionary.find(m[0].second)->second;
        oss << "As\n\t";
        Real sum = m[0].first*m[0].second;
        oss << m[0].first << "⋅" << m[0].second;
        for (size_t i = 1; i < m.size(); ++i)
        {
            if (m[i].first < 0) {
                oss << " - ";
            } else {
                oss << " + ";
            }
            oss << abs(m[i].first) << "⋅" << m[i].second;
            sum += m[i].first*m[i].second;
        }
        oss << " = " << sum << ",\nit is likely that\n\t";

        oss << m[0].first << "⋅" << symbol;
        for (size_t i = 1; i < m.size(); ++i)
        {
            if (m[i].first < 0) {
                oss << " - ";
            } else {
                oss << " + ";
            }
            auto const & symbol = dictionary.find(m[i].second)->second;
            oss << abs(m[i].first) << "⋅" << symbol;
        }
        oss << " = 0.";

        return oss.str();
    }
    return "";
}

template<typename Real>
std::string pslq(std::map<Real, std::string> const & dictionary) {
    Real gamma = 2/sqrt(3) + 0.01;
    return pslq(dictionary, gamma);
}

template<typename Real>
bool is_algebraic(Real x, std::vector<int64_t>& m) {
    // TODO: Figure out this interface.
    return false;
}

}
#endif