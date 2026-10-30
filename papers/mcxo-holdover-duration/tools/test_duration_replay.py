#!/usr/bin/env python3
"""Estimand regressions, synthetic inputs only. MIT, Jonathan Yantis, 2026."""
import unittest
from duration_replay import analyze, stats, quantile
from fine_frontier import fine_frontier

def start(trial=1,probe=12000):
    return dict(kind='START',trial=trial,start20=20000000,probe_ms=probe,epoch_ns=1000000000,phase_ns=0,rate=0.)
def pair(seq,age,error):
    local=20000000+int(age*20000000)
    return dict(kind='PAIR',seq=seq,local20=local,available20=local+100,reference_ns=1000000000+int(age*1e9)-error,channel=0,generation=1)

class Estimand(unittest.TestCase):
    def test_prefix_retains_crossing_instead_of_endpoint(self):
        r=analyze([start(probe=48000),pair(1,1,1200),pair(2,12.125,50),pair(3,48,3000)])
        twelve=next(x for x in r['prefixes'] if x['age_ms']==12000)
        self.assertEqual((twelve['peak_ns'],twelve['outcome'],twelve['overshoot_ms']),(1200,2,125))
        self.assertEqual(next(x for x in r['prefixes'] if x['age_ms']==48000)['peak_ns'],3000)
    def test_overshoot_is_scored_and_gap_is_visible(self):
        r=analyze([start(),pair(1,11.9,70),pair(2,12.4,1400)])
        twelve=next(x for x in r['prefixes'] if x['age_ms']==12000)
        self.assertEqual((twelve['peak_ns'],twelve['outcome'],twelve['gaps']),(1400,2,2))
    def test_short_clocks_do_not_supply_twelve_second_denominator(self):
        r=analyze([start(probe=2000),pair(1,2,50)])
        self.assertEqual(next(x for x in r['all_age_results'] if x['age_ms']==12000)['n'],0)
    def test_expiry_is_unknown_without_later_witness(self):
        r=analyze([start(),pair(1,1,70),dict(kind='END',trial=1,start20=20000000,probe_ms=12000,last20=40000000,outcomes=sum((1 if a<=1000 else 3)<<(2*i) for i,a in enumerate(__import__('duration_replay').AGES) if a<=12000),peak_ns=70,gaps=1)])
        twelve=next(x for x in r['prefixes'] if x['age_ms']==12000)
        self.assertEqual((twelve['outcome'],twelve['witness']),(3,False))
    def test_nearest_rank_and_strict_threshold(self):
        self.assertEqual(quantile([100]*127+[1500],99),100)
        row=lambda o,p:dict(outcome=o,peak_ns=p,witness=True)
        self.assertTrue(stats([row(1,999)]*127+[row(2,1000)])['passes'])
        self.assertFalse(stats([row(1,999)]*126+[row(2,1000)]*2)['passes'])
        self.assertFalse(stats([row(1,999)]*99+[dict(outcome=3,peak_ns=0,witness=False)])['ready'])
    def test_fine_grid_never_uses_post_end_witness(self):
        s=start(probe=96000)
        records=[s,pair(1,96,70),dict(kind='END',trial=1,start20=s['start20'],probe_ms=96000,last20=1940000000,outcomes=0,peak_ns=70,gaps=1),pair(2,110,9000)]
        f=fine_frontier(records)
        self.assertEqual(f['mature_starts'],1)
        self.assertEqual(f['ages'][95]['p99_ns'],70)
        self.assertEqual(f['ages'][96]['unknown'],1)
        self.assertTrue(f['ages'][96]['numeric_censored'])

if __name__=='__main__':unittest.main()
