#include <gtest/gtest.h>
#include "../src/Integrator.hpp"
#include <cmath>

// ─── Вспомогательная константа точности ────────────────────────────────────
static constexpr double H      = 1e-4; ///< Шаг интегрирования для большинства тестов
static constexpr double EPS    = 1e-3; ///< Допустимая погрешность (метод трапеций — O(h²))

// ─── Базовые математические функции ────────────────────────────────────────

/// x² на [0, 1] → 1/3
TEST(IntegratorTest, Polynomial_x2) {
    Integrator integrator;
    double result = integrator.integrate([](double x){ return x * x; }, 0.0, 1.0, H);
    EXPECT_NEAR(result, 1.0 / 3.0, EPS);
}

/// x³ на [0, 1] → 1/4
TEST(IntegratorTest, Polynomial_x3) {
    Integrator integrator;
    double result = integrator.integrate([](double x){ return x * x * x; }, 0.0, 1.0, H);
    EXPECT_NEAR(result, 0.25, EPS);
}

/// Константа 5 на [0, 3] → 15
TEST(IntegratorTest, Constant) {
    Integrator integrator;
    double result = integrator.integrate([](double x){ return 5.0; }, 0.0, 3.0, H);
    EXPECT_NEAR(result, 15.0, EPS);
}

/// Линейная функция x на [0, 4] → 8
TEST(IntegratorTest, Linear) {
    Integrator integrator;
    double result = integrator.integrate([](double x){ return x; }, 0.0, 4.0, H);
    EXPECT_NEAR(result, 8.0, EPS);
}

/// sin(x) на [0, π] → 2
TEST(IntegratorTest, Sine) {
    Integrator integrator;
    double result = integrator.integrate([](double x){ return std::sin(x); }, 0.0, M_PI, H);
    EXPECT_NEAR(result, 2.0, EPS);
}

/// cos(x) на [0, π/2] → 1
TEST(IntegratorTest, Cosine) {
    Integrator integrator;
    double result = integrator.integrate([](double x){ return std::cos(x); }, 0.0, M_PI / 2.0, H);
    EXPECT_NEAR(result, 1.0, EPS);
}

/// e^x на [0, 1] → e - 1
TEST(IntegratorTest, Exponential) {
    Integrator integrator;
    double result = integrator.integrate([](double x){ return std::exp(x); }, 0.0, 1.0, H);
    EXPECT_NEAR(result, std::exp(1.0) - 1.0, EPS);
}

/// 1/x на [1, e] → 1
TEST(IntegratorTest, OneOverX) {
    Integrator integrator;
    double result = integrator.integrate([](double x){ return 1.0 / x; }, 1.0, M_E, H);
    EXPECT_NEAR(result, 1.0, EPS);
}

// ─── Целевая функция задания ────────────────────────────────────────────────

/// 1/ln(x) на [2, 3] — нет аналитического результата, проверяем стабильность
/// при разных шагах: уменьшение шага вдвое должно уточнять результат
TEST(IntegratorTest, LogarithmicIntegral_Convergence) {
    Integrator integrator;
    auto f = [](double x){ return 1.0 / std::log(x); };
    double r1 = integrator.integrate(f, 2.0, 3.0, 1e-3);
    double r2 = integrator.integrate(f, 2.0, 3.0, 5e-4);
    double r3 = integrator.integrate(f, 2.0, 3.0, 1e-4);
    // Каждый следующий результат должен быть ближе к предыдущему (сходимость)
    EXPECT_LT(std::abs(r3 - r2), std::abs(r2 - r1));
}

/// 1/ln(x) на [2, 4] должна давать значение в разумном диапазоне [1.5, 3.0]
TEST(IntegratorTest, LogarithmicIntegral_Range) {
    Integrator integrator;
    double result = integrator.integrate([](double x){ return 1.0 / std::log(x); }, 2.0, 4.0, H);
    EXPECT_GT(result, 1.5);
    EXPECT_LT(result, 3.0);
}

// ─── Свойства интеграла ─────────────────────────────────────────────────────

/// Аддитивность: ∫[a,c] = ∫[a,b] + ∫[b,c]
TEST(IntegratorTest, Additivity) {
    Integrator integrator;
    auto f = [](double x){ return x * x; };
    double full  = integrator.integrate(f, 1.0, 3.0, H);
    double left  = integrator.integrate(f, 1.0, 2.0, H);
    double right = integrator.integrate(f, 2.0, 3.0, H);
    EXPECT_NEAR(full, left + right, EPS);
}

/// Линейность: ∫(a·f + b·g) = a·∫f + b·∫g
TEST(IntegratorTest, Linearity) {
    Integrator integrator;
    auto f = [](double x){ return std::sin(x); };
    auto g = [](double x){ return std::cos(x); };
    auto fg = [](double x){ return 2.0 * std::sin(x) + 3.0 * std::cos(x); };
    double combined  = integrator.integrate(fg, 0.0, 1.0, H);
    double separate  = 2.0 * integrator.integrate(f, 0.0, 1.0, H)
                      + 3.0 * integrator.integrate(g, 0.0, 1.0, H);
    EXPECT_NEAR(combined, separate, EPS);
}

/// Нечётная функция на симметричном отрезке → 0
TEST(IntegratorTest, OddFunction_SymmetricInterval) {
    Integrator integrator;
    double result = integrator.integrate([](double x){ return x * x * x; }, -1.0, 1.0, H);
    EXPECT_NEAR(result, 0.0, EPS);
}

// ─── Граничные случаи ───────────────────────────────────────────────────────

/// Очень маленький отрезок → результат стремится к нулю
TEST(IntegratorTest, TinyInterval) {
    Integrator integrator;
    double result = integrator.integrate([](double x){ return x * x; }, 1.0, 1.0 + 1e-9, H);
    EXPECT_NEAR(result, 0.0, 1e-6);
}

/// Крупный шаг (один-два прямоугольника) — метод работает, не падает
TEST(IntegratorTest, LargeStep_NoCrash) {
    Integrator integrator;
    EXPECT_NO_THROW(
        integrator.integrate([](double x){ return x; }, 0.0, 1.0, 0.9)
        );
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
