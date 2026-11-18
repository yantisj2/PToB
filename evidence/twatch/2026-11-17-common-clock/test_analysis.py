#!/usr/bin/env python3
"""Independent small-number oracles for full-UTC and missing denominators.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import copy
import unittest
import analyze


class AnalysisTest(unittest.TestCase):
    def test_integer_utc_subtraction_below_float_resolution(self):
        utc=1790000000000000000
        x=dict(ble=dict(eligible=True,utc_ns=utc+7),lora=dict(eligible=True,utc_ns=utc+19),clock=dict(valid=True,utc_ns=utc+3))
        self.assertEqual(analyze.differences(x),dict(lora_minus_ble=12,clock_minus_ble=-4,clock_minus_lora=-16))
        x['ble']['eligible']=False
        self.assertEqual(analyze.differences(x),dict(clock_minus_lora=-16))

    def test_all_tails_and_nearest_rank_p99(self):
        values=[0]*99+[1000000]
        d=analyze.distribution(values)
        self.assertEqual(d['n'],100)
        self.assertEqual(d['p99_abs_ns'],0)
        self.assertEqual(d['max_abs_ns'],1000000)
        self.assertEqual(d['rms_ns'],100000)

    def test_absence_is_not_a_zero_measurement(self):
        self.assertIsNone(analyze.distribution([])['rms_ns'])

    def test_projection_count_does_not_create_physical_packets(self):
        # Ten repeated projections in a five-second bin remain ten projections.
        # The analysis separately counts unique (scope, domain, observation ID).
        values=[dict(elapsed_s=.1*i,stage='synthetic',mode='LORA_COARSE',clock_valid=1,authoritative=1,ble_ready=0,lora_ready=1,
                     lora_minus_ble_ns=None,clock_minus_ble_ns=None,clock_minus_lora_ns=4,rate_ppb=3,holdover_age_s=0) for i in range(10)]
        row=analyze.aggregate_timeline(values)[0]
        self.assertEqual(row['snapshots'],10)
        self.assertEqual(row['clock_minus_lora_ns_min'],4)
        self.assertIsNone(row['lora_minus_ble_ns_median'])

    def test_missing_opportunities_and_old_innovation_remain_distinct(self):
        rows=[];base=1790000000000000000
        for sequence,at in enumerate([.5,1.5,4.5],1):
            tick=int((10+at)*20000000)
            radio=dict(eligible=True,map_valid=True,available_tick=tick-1,until_tick=tick+1,calibration_match=False,current_correction_ns=2900,utc_ns=base+tick*50,authenticated=True,scope='synthetic-scope',counter_domain='synthetic-domain',observation_id=99,reason=0)
            empty=dict(radio,eligible=False,utc_ns=0,scope='0000000000000000')
            event=dict(tick=1,scope='synthetic-scope',observation_id=1,innovation_ns=1000000)
            x=dict(sequence=sequence,tick=tick,ble=radio,lora=empty,lora_physical_pairs=1,
                   ble_lineage=dict(root='synthetic-root',parent='synthetic-root',depth_known=True,depth=0),
                   lora_lineage={},ble_causal_event=event,lora_causal_event=dict(event,tick=0),
                   clock=dict(valid=True,utc_ns=base+tick*50,mode='BLE_FINE',authoritative=True,fine=True,
                              calibrated=False,instance='synthetic-instance',counter_domain='synthetic-domain',holdover_age_us=0,rate_ppb=0))
            rows.append(dict(stage='warmup',stage_elapsed_s=at,host_elapsed_s=at,boot='synthetic-boot',firmware='synthetic-build',sample=x))
        events=[dict(kind='STAGE_START',stage='warmup'),dict(kind='STAGE_END',stage='warmup',actual_elapsed_s=5)]
        result,_=analyze.analyze(rows,dict(warmup_seconds=5,stages=[],fixed_prior={}),events)
        stage=result['stages'][0]
        self.assertEqual(stage['missing_bins'],2)
        self.assertEqual(stage['grid']['authoritative']['nominal_opportunities'],5)
        self.assertEqual(stage['grid']['authoritative']['observed_available_fraction'],.6)
        self.assertEqual(stage['distinct_physical_observations']['ble'],1)
        self.assertEqual(stage['causal_innovations']['ble']['n'],0)
        self.assertNotIn('lora_minus_ble',stage['model_disagreement'])
        held=copy.deepcopy(rows)
        held[0]['sample']['clock'].update(mode='HOLD',authoritative=False,fine=False,holdover_age_us=500000)
        held[1].update(stage='return',stage_elapsed_s=.5,host_elapsed_s=5.5)
        held[1]['sample']['clock'].update(mode='HOLD',authoritative=False,fine=False,holdover_age_us=5500000)
        held[2].update(stage='return',stage_elapsed_s=1.5,host_elapsed_s=6.5)
        recovered,_=analyze.analyze(held,dict(warmup_seconds=5,stages=[dict(name='return',seconds=5)],fixed_prior={}),events)
        self.assertEqual(recovered['hold_recoveries'][0]['loss_stage'],'warmup')
        self.assertEqual(recovered['hold_recoveries'][0]['return_stage'],'return')
        self.assertEqual(recovered['hold_recoveries'][0]['last_sampled_hold_age_us'],5500000)



if __name__=='__main__':unittest.main()
