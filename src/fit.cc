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

#include "always.h"

#include "fit.h"

using namespace std;

void fitFirstOrderCurve(std::vector<std::pair<double,double>> &xy, double *a, double *b)
{
    Matrix2x2 M;
    double ka=0,kb=0;

    M.a = xy.size();
    for (auto &p : xy) {
        double x = p.first;
        double y = p.second;
        M.b += x;
        M.c += x;
        M.d += x*x;
        ka += y;
        kb += x*y;
    }

    /*
    printf("|%4.4f %4.4f| |a0|   |%4.4f|\n", M.a, M.b, ka);
    printf("|%4.4f %4.4f| |b0| = |%4.4f|\n", M.c, M.d, kb);
    */
    double det = M.det();
    double det0 = M.deti(1, ka, kb);
    double det1 = M.deti(2, ka, kb);

    /*printf("det =%4.4f\n", det);
    printf("det0=%4.4f\n", det0);
    printf("det1=%4.4f\n", det1);
    printf("det2=%4.4f\n", det2);
*/
    double a0 = det0/det;
    double a1 = det1/det;

    *a = a1;
    *b = a0;
}

double calculateFirstOrderCurve(double a, double b, double x)
{
    return a*x+b;
}

void fitSecondOrderCurve(std::vector<std::pair<double,double>> &xy, double *a, double *b, double *c)
{
    Matrix3x3 M;
    double ka=0,kb=0,kc=0;

    M.a = xy.size();
    for (auto &p : xy) {
        double x = p.first;
        double y = p.second;
        M.b += x;
        M.c += x*x;
        M.d += x;
        M.e += x*x;
        M.f += x*x*x;
        M.g += x*x;
        M.h += x*x*x;
        M.i += x*x*x*x;
        ka += y;
        kb += x*y;
        kc += x*x*y;
    }

    /*
    printf("|%4.4f %4.4f %4.4f| |a0|   |%4.4f|\n", M.a, M.d, M.g, ka);
    printf("|%4.4f %4.4f %4.4f| |b0| = |%4.4f|\n", M.b, M.e, M.h, kb);
    printf("|%4.4f %4.4f %4.4f| |c0|   |%4.4f|\n", M.c, M.f, M.i, kc);
    */
    double det = M.det();
    double det0 = M.deti(1, ka, kb, kc);
    double det1 = M.deti(2, ka, kb, kc);
    double det2 = M.deti(3, ka, kb, kc);
/*
    printf("det =%4.4f\n", det);
    printf("det0=%4.4f\n", det0);
    printf("det1=%4.4f\n", det1);
    printf("det2=%4.4f\n", det2);
*/
    double a0 = det0/det;
    double a1 = det1/det;
    double a2 = det2/det;

    *a = a2;
    *b = a1;
    *c = a0;
//    printf("a=%f b=%f c=%f\n", *a, *b, *c);
}

double calculateSecondOrderCurve(double a, double b, double c, double x)
{
    return a*x*x+b*x+c;
}

void predict_all(std::vector<SecsBytes> &sb, size_t n, size_t max_bytes,
                 double *eta_1s_speed,
                 double *eta_immediate,
                 double *eta_average)
{
    *eta_1s_speed = predict_1s_speed(sb, n, max_bytes);
    *eta_immediate = predict_immediate(sb, n, max_bytes);
    *eta_average = predict_average(sb, n, max_bytes);
}

double predict_1s_speed(std::vector<SecsBytes> &sb, size_t n, size_t max_b)
{
    double max_bytes = max_b;
    double count = 0.0;
    double eta = 0.0;

    for (size_t i = (n-9)<0?0:n-9; i<=n; ++i) {
        double bytes = sb[i].bytes;
        double secs = sb[i].secs;
        double mul = i-(n-10);
        eta += secs*(1+(max_bytes-bytes)/bytes)*mul;
        count+=mul;
    }

    return eta/count;
}

double predict_immediate(vector<SecsBytes> &sb, size_t n, size_t max_b)
{
    double max_bytes = max_b;
    if (sb[n].bytes == 0) return 0.0;
    assert(n < sb.size());
    double bytes = sb[n].bytes;
    double secs = sb[n].secs;
    return secs*(1+(max_bytes-bytes)/bytes);
}

double predict_average(vector<SecsBytes> &sb, size_t n, size_t max_b)
{
    double max_bytes = max_b;
    if (sb[n].bytes == 0) return 0.0;
    assert(n < sb.size());
    double eta = 0.0;
    double count = 0.0;
    double bytes = sb[n].bytes;
    double secs = sb[n].secs;

    double start_from = 0.0;
    double half_bytes = max_bytes/2;
    if (bytes > half_bytes) {
        start_from = (bytes-half_bytes)*2;
    }
    for (size_t i = 0; i<=n; ++i) {
        bytes = sb[i].bytes;
        secs = sb[i].secs;
        if (bytes < start_from) continue;
        if (bytes == 0) continue;
        eta += secs*(1+(max_bytes-bytes)/bytes);
        count++;
    }
    if (count > 0) {
        return eta/count;
    }
    return secs;
}
