/*
 Copyright (C) 2019 Fredrik Öhrström

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

#include "prune.h"

#include "log.h"
#include "util.h"

#include <set>

static ComponentId PRUNE = registerLogComponent("prune");

using namespace std;

struct PruneImplementation : public Prune
{
public:
    void addPointInTime(uint64_t p);
    void prune(std::map<uint64_t,bool> *result);

    PruneImplementation(uint64_t now, const Keep &keep) { now_ = now; keep_ = keep; }
private:

    uint64_t now_ {};
    Keep keep_;
    uint64_t prev_ {};
    map<uint64_t,bool> points_;
    uint64_t latest_;
    set<uint64_t> all_;
    map<uint64_t,uint64_t> daily_max_;
    map<uint64_t,uint64_t> weekly_max_;
    map<uint64_t,uint64_t> monthly_max_;

    bool isAll(uint64_t p);
    bool isDailyMax(uint64_t p);
    bool isWeeklyMax(uint64_t p);
    bool isMonthlyMax(uint64_t p);
};

unique_ptr<Prune> newPrune(uint64_t now, const Keep &keep)
{
    return unique_ptr<Prune>(new PruneImplementation(now, keep));
}

#define NANOS 1000*1000*1000ull

uint64_t to_days_since_epoch(uint64_t p)
{
    return p/(3600ull*24*NANOS);
}

uint64_t to_weeks_since_epoch(uint64_t p)
{
    // Unix time 0 is 1970-01-01 which was a thursday.
    // Lets add mon,tues and wednesday to p, to make
    // week number changes align with sun -> mon.
    // What is important is that you usually start new work
    // on monday, thus the previous weeks last commit should be saved.
    return (p+3*3600ull*24*NANOS)/(3600ull*24*7*NANOS);
}

uint64_t to_month_identifier_since_epoch(uint64_t p)
{
    // This is not the exact nr of months since the epoch.
    // Instead we take the year*100 + month to calculate
    // a unique increasing identifier.

    // Time date calculation below included from:
    // howardhinnant.github.io/date_algorithms.html

    uint64_t z = to_days_since_epoch(p);
    z += 719468;
    const uint64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);          // [0, 146096]
    const unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;  // [0, 399]
    const uint64_t y = static_cast<uint64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);                // [0, 365]
    const unsigned mp = (5*doy + 2)/153;                                   // [0, 11]
    //const unsigned d = doy - (153*mp+2)/5 + 1;                             // [1, 31]
    const unsigned m = mp + (mp < 10 ? 3 : -9);                            // [1, 12]
    // Now multiply year with 100 and add month.
    const uint64_t mid = (y + (m <= 2))*100 + m;

    return mid;
}

bool PruneImplementation::isAll(uint64_t p)
{
    return all_.count(p) != 0;
}

bool PruneImplementation::isDailyMax(uint64_t p)
{
    for (auto& e : daily_max_) {
        if (e.second == p) return true;
    }
    return false;
}

bool PruneImplementation::isWeeklyMax(uint64_t p)
{
    for (auto& e : weekly_max_) {
        if (e.second == p) return true;
    }
    return false;
}

bool PruneImplementation::isMonthlyMax(uint64_t p)
{
    for (auto& e : monthly_max_) {
        if (e.second == p) return true;
    }
    return false;
}

void PruneImplementation::addPointInTime(uint64_t p)
{
    assert(p <= now_);
    assert(p >= prev_);
    prev_ = p;
    points_[p] = false;
    uint64_t diff = (now_ - p)/(1000ull*1000*1000);
    uint64_t d = to_days_since_epoch(p);
    uint64_t w = to_weeks_since_epoch(p);
    uint64_t m = to_month_identifier_since_epoch(p);

    // Always keep the latest
    latest_ = p;

    // Keep all
    if (diff < keep_.all) {
        all_.insert(p);
    }

    // Keep dailys
    if (diff < keep_.daily) {
        daily_max_[d] = p;
    }

    // Keep weeklys
    if (diff < keep_.weekly) {
        weekly_max_[w] = p;
    }

    // Keep monthlys
    if (diff < keep_.monthly) {
        monthly_max_[m] = p;
    }
}

const char* weekday_names[] = { "mon", "tue", "wed", "thu", "fri", "sat", "sun" };
const char* month_names[] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };

void PruneImplementation::prune(std::map<uint64_t,bool> *result)
{
    points_[latest_] = true;
    for (auto& p : all_) { points_[p] = true; }
    for (auto& e : daily_max_) { points_[e.second] = true; }
    for (auto& e : weekly_max_) { points_[e.second] = true; }
    for (auto& e : monthly_max_) { points_[e.second] = true; }

    // Print the pruning descisions...
    verbose(PRUNE, "Action     Date       Time      Daynr  Weeknr     Monthnr\n");
    for (auto& e : points_)
    {
        uint64_t p = e.first;
        string s = timeToString(p);
        bool keep = e.second;

        uint64_t days = to_days_since_epoch(p);
        uint64_t weeknr = to_weeks_since_epoch(p);
        int weekday = (days+3) % 7; // [0 == mon, 6 == sun]

        uint64_t monthid = to_month_identifier_since_epoch(p);
        if (keep) {
            verbose(PRUNE, "keeping    %s ", s.c_str());
        } else {
            verbose(PRUNE, "discarding %s ", s.c_str());
        }
        verbose(PRUNE, " %5zu  %4zu(%s)  %6zu(%s)", days, weeknr, weekday_names[weekday], monthid,
            month_names[monthid%100-1]);
        if (latest_ == p) verbose(PRUNE, " LATEST");
        if (isAll(p)) verbose(PRUNE, " ALL");
        if (isDailyMax(p)) verbose(PRUNE, " DAY");
        if (isWeeklyMax(p)) verbose(PRUNE, " WEEK");
        if (isMonthlyMax(p)) verbose(PRUNE, " MONTH");
        verbose(PRUNE, " \n");
    }

    *result = points_;
}
