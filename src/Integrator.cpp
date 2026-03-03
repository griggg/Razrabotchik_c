#include "Integrator.hpp"
#include <cmath>
#include <iostream>

double Integrator::integrate(const std::function<double(double)>& f, double a, double b, double h)
{
    double sum = 0.5 * (f(a) + f(b));

    long long steps = static_cast<long long>((b - a) / h);
    for (long long i = 1; i < steps; i++) {
        double x = a + i * h;
        sum += f(x);
    }

    return sum * h;
}
