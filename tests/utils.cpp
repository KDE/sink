#include "utils.h"

#include <QList>
#include <math.h>

double variance(const QList<double> &values)
{
    //The variance of a single value is 0
    if (values.size() == 1) {
        return 0;
    }
    double mean = 0;
    for (auto value : values) {
        mean += value;
    }
    mean = mean / static_cast<double>(values.size());
    double variance = 0;
    for (auto value : values) {
        variance += pow(static_cast<double>(value) - mean, 2);
    }
    variance = variance / static_cast<double>(values.size() - 1);
    return variance;
}

double maxDifference(const QList<double> &values)
{
    auto max = values.first();
    auto min = values.first();
    for (auto value : values) {
        if (value > max) {
            max = value;
        }
        if (value < min) {
            min = value;
        }
    }
    return max - min;
}
