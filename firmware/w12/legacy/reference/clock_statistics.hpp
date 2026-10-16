#pragma once
namespace clock_stats {
constexpr unsigned Percentile=95;
constexpr unsigned percentile_rank(unsigned count,unsigned percentile=Percentile){return (percentile*count+99)/100;}
}
