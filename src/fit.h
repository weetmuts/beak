/*
 Copyright (C) 2018 Fredrik Öhrström

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FIT_H
#define FIT_H

#include "always.h"

#include <vector>


struct SecsBytes
{
    double secs;
    double bytes;
};

struct Matrix2x2
{
    double a{},b{},
           c{},d{};

    double det() {
        return a*d-b*c;
    }

    double deti(int col, double x, double y) {
        if (col == 1) {
            return x*d-b*y;
        }
        if (col == 2) {
            return a*y-x*c;
        }
        assert(0);
        return 0;
    }
};


struct Matrix3x3
{
    double a{},b{},c{},
           d{},e{},f{},
           g{},h{},i{};

    double det() {
        return a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    }

    double deti(int col, double x, double y, double z) {
        if (col == 1) {
            return x*(e*i-f*h)-b*(y*i-f*z)+c*(y*h-e*z);
        }
        if (col == 2) {
            return a*(y*i-f*z)-x*(d*i-f*g)+c*(d*z-y*g);
        }
        if (col == 3) {
            return a*(e*z-y*h)-b*(d*z-y*g)+x*(d*h-e*g);
        }
        assert(0);
        return 0;
    }
};

void fitFirstOrderCurve(std::vector<std::pair<double,double>> &xy, double *a, double *b);
double calculateFirstOrderCurve(double a, double b, double x);

void fitSecondOrderCurve(std::vector<std::pair<double,double>> &xy, double *a, double *b, double *c);
double calculateSecondOrderCurve(double a, double b, double c, double x);

void predict_all(std::vector<SecsBytes> &sb, size_t n, size_t max_bytes,
                 double *eta_1s_speed,
                 double *eta_immediate,
                 double *eta_average);

double predict_1s_speed(std::vector<SecsBytes> &sb, size_t n, size_t max_bytes);
double predict_immediate(std::vector<SecsBytes> &sb, size_t n, size_t max_bytes);
double predict_average(std::vector<SecsBytes> &sb, size_t n, size_t max_bytes);

#endif
