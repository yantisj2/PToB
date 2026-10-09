"""Directed V12 proposal checks; all clocks, radios and sources are synthetic."""
import dataclasses
import json
import unittest
from pathlib import Path

from tools.v12_model import (C1, C3, TC01, TC03, G4, G5, G6, RELAY, Clock, FORMAT, MAX64, Permit,
                             Message, Profile, Radio, Simulator, mac64, run)


class WireTests(unittest.TestCase):
    def test_siphash_reference_vectors(self):
        # SipHash authors' reference vectors, key 00..0f, inputs 00..n-1.
        expected = ["726fdb47dd0e0e31", "74f839c593dc67fd", "0d6c8009d9a94f5a",
                    "85676696d7fb7e2d", "cf2794e0277187b7", "18765564cd99a68d",
                    "cbc9466e58fee3ce", "ab0200f58b01d137", "93f5f5799a932462",
                    "9e0082df0ba9e4b0", "7a5dbbc594ddb9f3", "f4b32f46226bada7",
                    "751e8fbc860ee5fb", "14ea5627c0843d90", "f723ca908e7af2ee",
                    "a129ca6149be45e5"]
        for n, value in enumerate(expected):
            self.assertEqual(int.from_bytes(mac64(bytes(range(n)), bytes(range(16))), "little"), int(value, 16))

    def test_frozen_vectors(self):
        vectors = json.loads((Path(__file__).parents[1]/"protocol/v12_model_vectors.json").read_text())
        for vector in vectors["valid"]:
            m = Message(**vector["fields"])
            self.assertEqual(m.encode().hex(), vector["wire_hex"])
            self.assertEqual(Message.decode(m.encode()), m)
        self.assertEqual(FORMAT.size + 8, 110)

    def test_every_byte_mutation_and_length(self):
        m = Simulator(Profile(seconds=10)).message("REPORT", TC01, TC03)
        wire = m.encode()
        for i in range(len(wire)):
            changed = bytearray(wire); changed[i] ^= 1
            with self.assertRaises(ValueError):
                Message.decode(changed)
        for wire2 in (wire[:-1], wire+b"\0", b""):
            with self.assertRaises(ValueError):
                Message.decode(wire2)

    def test_field_ranges_and_exhaustion(self):
        s = Simulator(Profile(seconds=10)); m = s.message("REPORT", TC01, TC03)
        for fields in ({"boot":0}, {"sequence":MAX64+1}, {"flags":4},
                       {"target":C1}, {"evidence_us":1}, {"expires_us":0},
                       {"created_us":-1}, {"source_epoch":1<<32}):
            with self.assertRaises(ValueError):
                dataclasses.replace(m, **fields).encode()
        s.sequences[TC01] = MAX64
        with self.assertRaises(ValueError):
            s.message("REPORT", TC01, TC03)


class StateTests(unittest.TestCase):
    def sim(self, **kw):
        return Simulator(Profile(seconds=20, **kw))

    def test_duplicate_cannot_qualify_or_refresh(self):
        s = self.sim(); m = s.message("REPORT", TC01, TC03, flags=3, source_second=1)
        self.assertTrue(s.receive(m.encode())); self.assertFalse(s.receive(m.encode()))
        s.now = 100000
        m2 = s.message("REPORT", TC01, TC03, flags=3, source_second=1)
        self.assertTrue(s.receive(m2.encode()))
        self.assertEqual(s.peers[TC01].unique, 1)
        self.assertEqual(s.peers[TC01].evidence_us, 0)
        s.now = 6000000
        self.assertFalse(s.live(TC01))

    def test_three_fresh_qualified_seconds_required(self):
        s = self.sim()
        for t in range(3):
            s.now = t*1000000
            s.receive(s.message("REPORT", TC01, TC03, flags=3, source_second=t+1, evidence_us=s.now).encode())
            self.assertEqual(s.live(TC01), t == 2)
        s.now += 1000000
        s.receive(s.message("REPORT", TC01, TC03, flags=0, source_second=4, evidence_us=s.now).encode())
        self.assertFalse(s.live(TC01)); self.assertEqual(s.peers[TC01].unique, 0)

    def test_role_scope_replay_config_and_deadline(self):
        for changes in ({"coordinator_boot":2}, {"boot":2}, {"challenge":999}, {"config_epoch":2}):
            s = self.sim(); m = s.message("REPORT", TC01, TC03)
            self.assertFalse(s.receive(dataclasses.replace(m, **changes).encode()))
            self.assertFalse(s.accepted_sequences)
        s = self.sim(); m = s.message("REPORT", TC01, TC03)
        s.now = m.expires_us
        self.assertFalse(s.receive(m.encode()))
        s = self.sim()
        forged = s.message("COMMIT", C3, TC01, root=TC01, root_boot=1, source_epoch=1, decision=1, permit_until_us=3000000)
        self.assertFalse(s.receive(forged.encode())); self.assertFalse(s.permits)

    def test_queue_refusal_preserves_whole_records(self):
        s = self.sim(control_queue=1)
        first = s.message("REPORT", TC01, TC03); s.send(first)
        s.send(s.message("REPORT", TC01, TC03))
        self.assertEqual(len(s.queues[TC01, TC03]), 1)
        self.assertEqual(s.queues[TC01, TC03][0][0], first.encode())
        self.assertEqual(s.counters["queue_refused"], 1)

    def test_commit_requires_ready_and_unknown_bounds_refuse(self):
        for kw in ({}, {"map_known":False}, {"stop_known":False}):
            s = self.sim(**kw)
            m = s.message("COMMIT", TC03, TC01, root=TC01, root_boot=1, source_epoch=1, decision=1, permit_until_us=3000000)
            self.assertFalse(s.receive(m.encode())); self.assertFalse(s.permits)
        for kw in ({"map_known":False}, {"stop_known":False}):
            r = run(Profile(seconds=15, **kw))
            self.assertTrue(r["safety_pass"]); self.assertFalse(r["offers"])

    def test_stop_retires_later_delivered_old_commit(self):
        s = self.sim(); s.prepared[TC01] = 1
        initial = s.message("COMMIT", TC03, TC01, root=TC01, root_boot=1, source_epoch=1, decision=1, permit_until_us=3000000)
        self.assertTrue(s.receive(initial.encode()))
        stop = s.message("STOP", TC03, TC01, root=TC01, decision=2)
        self.assertTrue(s.receive(stop.encode())); s.stop(TC01, 2, reply=True)
        # Fresh transport sequence cannot revive a retired semantic decision.
        delayed = s.message("COMMIT", TC03, TC01, root=TC01, root_boot=1, source_epoch=1, decision=1, permit_until_us=3000000)
        self.assertFalse(s.receive(delayed.encode())); self.assertFalse(s.permits)

    def test_reboot_does_not_erase_outstanding_fence(self):
        s = self.sim(); s.issued_until = 2500000; s.now = 1000000
        old = s.message("REPORT", TC01, TC03)
        s.reboot(TC03)
        self.assertGreaterEqual(s.server_quiet_until, 4005000)
        self.assertFalse(s.receive(old.encode()))

    def test_source_epoch_resets_qualification(self):
        s = self.sim()
        for t in range(3):
            s.now=t*1000000
            s.receive(s.message("REPORT", TC01, TC03, flags=3, source_epoch=1, source_second=t+1, evidence_us=s.now).encode())
        self.assertTrue(s.live(TC01)); s.now += 1000000
        s.receive(s.message("REPORT", TC01, TC03, flags=3, source_epoch=2, source_second=4, evidence_us=s.now).encode())
        self.assertFalse(s.live(TC01))

    def test_cold_source_wait_is_not_transport_acquisition(self):
        r = run(Profile(seconds=70, warm_start=False))
        enabled = [e for e in r["transitions"] if e["kind"] == "PUBLISHER_ENABLED"]
        self.assertGreaterEqual(enabled[0]["at_us"], 62000000)

    def test_loss_and_stall_recover_without_renewing_uncertain_commit(self):
        r = run(Profile(seconds=40, logger_at_us=12000000, logger_stall_us=4000000))
        self.assertTrue(r["safety_pass"])
        self.assertTrue(any(e["kind"]=="PUBLISHER_ENABLED" and e["at_us"]>16000000 for e in r["transitions"]))
        self.assertGreater(r["listeners"]["2"]["corrected"], 100)

    def test_ack_loss_is_unavailability_not_unsafe_takeover(self):
        r = run(Profile(seconds=20, ack_loss=1))
        self.assertTrue(r["safety_pass"])
        self.assertLess(r["listeners"]["2"]["grid_available_fraction"], .9)

    def test_source_handoff_and_healthy_incumbent(self):
        r = run(Profile(seconds=100, source_loss_start_us=15000000, source_loss_us=10000000))
        self.assertTrue(r["safety_pass"])
        enabled = [e for e in r["transitions"] if e["kind"]=="PUBLISHER_ENABLED"]
        self.assertEqual([e["root"] for e in enabled], [TC01, TC03])

    def test_invalid_bound_counterexamples_are_visible(self):
        for kw in ({"map_error_us":-50000}, {"stop_delay_us":30000}, {"timer_ppm":-20000}):
            r=run(Profile(seconds=25, permit_pause_start_us=10000000, permit_pause_us=15000000, **kw))
            self.assertFalse(r["safety"]["publisher_deadline"])
            self.assertTrue(r["late_stops"])


class RadioAndPrecisionTests(unittest.TestCase):
    def test_half_duplex_and_transactional_caps(self):
        r=Radio(); self.assertTrue(r.reserve(0, 1000, True))
        before = r.metrics()
        self.assertFalse(r.reserve(500, 1200, False))
        self.assertEqual(r.metrics()["max_union_us_per_6s"], before["max_union_us_per_6s"])
        self.assertTrue(r.reserve(2000, 500000, True))
        self.assertFalse(r.reserve(500000, 502000, True))
        self.assertEqual(r.metrics()["max_rx_us_per_1s"], 499000)

    def test_cycle_split_and_union_overlap(self):
        r=Radio(); self.assertTrue(r.reserve(5999000, 6001000, True))
        self.assertEqual(r.metrics()["max_union_us_per_6s"], 1000)
        self.assertTrue(r.reserve(5999000, 6001000, True, background=True))
        self.assertEqual(r.metrics()["max_union_us_per_6s"], 1000)

    def test_missing_original_and_repeat_do_not_create_anchor(self):
        s=Simulator(Profile(seconds=10)); r=s.receivers[0]; scope=(TC01,1,1,1)
        s.now=10000; s.correct(r,scope,[(1,1000,1000000)])
        self.assertFalse(r.clock.rows)
        r.history[scope,1]=(1000,2000000); s.correct(r,scope,[(1,1000,1000000)])
        age=r.clock.last_reference_us
        s.now=20000; s.correct(r,scope,[(1,1000,1000000)])
        self.assertEqual(r.clock.last_reference_us,age)
        s.now=3001000; s.correct(r,scope,[(1,1000,1000000)])
        self.assertEqual(len(r.clock.rows),1)

    def test_causal_clock_scope_rate_freshness_and_monotonicity(self):
        c=Clock(); scope=(0,1,1,1)
        self.assertFalse(c.ingest(scope,1,200,100,200000,200000))
        self.assertTrue(c.ingest(scope,1,100,200,100000,100000))
        self.assertIsNone(c.predict(250,250000))
        self.assertTrue(c.ingest(scope,2,200,300,200000,200000))
        old=c.predict(400,400000)
        c.ingest(scope,3,300,500,300000,299950)
        self.assertGreaterEqual(c.predict(500,500000),old)
        self.assertIsNone(c.predict(5000300,5000300000))
        c.ingest((0,2,1,1),4,600,700,600000,600000)
        self.assertIsNone(c.predict(800,800000))

    def test_repeated_payload_ambiguity_is_not_precision(self):
        r=run(Profile(seconds=20, ambiguity_rate=1))
        self.assertEqual(r["listeners"]["2"]["corrected"],0)
        self.assertIsNone(r["independent_precision"])
        self.assertEqual(r["PTS"],"OFF")

    def test_phase_alias_and_three_independent_receivers(self):
        legacy=run(Profile(mode="A",seconds=20))
        self.assertEqual(legacy["listeners"]["2"]["corrected"],0)
        periodic=run(Profile(mode="D",seconds=20,clients=3))
        self.assertTrue(periodic["safety_pass"])
        self.assertTrue(all(v["corrected"]>100 for v in periodic["listeners"].values()))

    def test_discovery_collision_is_retained(self):
        r=run(Profile(mode="B",seconds=15,discovery_phase_us=0))
        self.assertEqual(r["listeners"]["2"]["corrected"],0)
        self.assertTrue(r["safety_pass"])

    def test_bias_remains_absolute_error_not_detrended_rms(self):
        r=run(Profile(seconds=20,timestamp_bias_ns=2000))
        self.assertFalse(r["listeners"]["2"]["numeric_screen"])
        self.assertGreater(r["listeners"]["2"]["synthetic_rms_ns"],1900)

    def test_declared_profiles_reject_invalid_inputs(self):
        for kw in ({"mode":"DD"},{"pa_interval_us":0},{"scan_us":500001},
                   {"history":5},{"loss":1.1},{"control_life_us":0},{"discovery_interval_us":0}):
            with self.assertRaises(ValueError):
                Profile(**kw).validate()


class MembershipAndOneHopTests(unittest.TestCase):
    def test_two_runs_and_400ms_window_deliver_at_least_1Hz_near_and_far(self):
        for choice in (0,1):
            r=run(Profile(seconds=30,quiet_listen=True,quiet_run_choice=choice,one_hop=True,
                          gm_count=6,clients=3,far_clients=2))
            self.assertTrue(r["safety_pass"])
            self.assertTrue(all(v["warmed_1Hz_update_fraction"]==1 for v in r["listeners"].values()))
            self.assertEqual(r["duty_profile"]["contiguous_quiet_us"],600000)
            self.assertFalse(r["duty_profile"]["PTS_qualified"])
            self.assertFalse(r["duty_profile"]["control_30ms_profile_provides_S3_blackout"])

    def test_quiet_hold_bias_cannot_enable_PTS(self):
        r=run(Profile(seconds=30,quiet_listen=True,timestamp_bias_ns=1500))
        self.assertGreater(r["listeners"]["2"]["synthetic_max_quiet_hold_abs_ns"],1000)
        self.assertEqual(r["PTS"],"OFF")

    def test_native_caps_do_not_remove_half_duplex_or_client_duty(self):
        radio=Radio(capped=False)
        self.assertTrue(radio.reserve(0,700000,True))
        self.assertFalse(radio.reserve(2000,3000,False))
        s=Simulator(Profile(seconds=10,native_w12=True,quiet_listen=True,one_hop=True))
        self.assertFalse(s.radios[C3].capped);self.assertFalse(s.radios[RELAY].capped)
        self.assertTrue(s.radios[C1].capped);self.assertTrue(s.radios[TC01].capped)

    def test_GMs_and_relay_online_during_client_quiet(self):
        s=Simulator(Profile(seconds=10,observe_GMs=True,quiet_listen=True,one_hop=True))
        client=s.receivers[0];client.phase=0
        self.assertFalse(s.awake(client,600000))
        self.assertTrue(all(s.awake(r,600000) for r in s.gm_receivers))
        self.assertTrue(s.awake(s.relay,600000))

    def test_updated_cycle_both_windows_and_half_clients_with_loss(self):
        r=run(Profile(seconds=60,pa_interval_us=62500,relay_phase_us=31250,two_windows=True,
            run_active_us=375000,sync_timeout_us=5000000,quiet_listen=True,full_clients=1,
            clients=3,one_hop=True,far_clients=2,gm_count=4,observe_GMs=True,native_w12=True,rf_loss=.1))
        self.assertTrue(r["safety_pass"])
        self.assertTrue(all(v["warmed_1Hz_update_fraction"]==1 for v in r["listeners"].values()))
        self.assertEqual(r["listeners"][str(C1)]["warmed_two_window_fraction"],1)
        self.assertEqual(r["GM_precision_observers"][str(TC03)]["warmed_two_window_fraction"],1)
        self.assertTrue(all(v["numeric_screen"] for v in r["listeners"].values()))

    def test_two_windows_do_not_require_client_quiet(self):
        r=run(Profile(seconds=20,pa_interval_us=62500,two_windows=True,run_active_us=375000))
        self.assertEqual(r["duty_profile"]["runs_per_second"],2)
        self.assertFalse(r["duty_profile"]["enabled"])
        self.assertEqual(r["listeners"][str(C1)]["warmed_two_window_fraction"],1)

    def test_one_hop_held_delivery_retains_freshness_and_finite_horizon(self):
        s=Simulator(Profile(seconds=10,one_hop=True));client=s.receivers[0]
        scope=(RELAY,1,1,TC01,1,1)
        client.clock.ingest(scope,1,100000,110000,s.local_ns(client,100000),100000000)
        client.clock.ingest(scope,2,200000,210000,s.local_ns(client,200000),200000000)
        client.original_source_us=180000;client.origin_deadline_us=500000
        s.now=800000;s.score()
        self.assertEqual(client.delivery_states["HELD"],1)
        self.assertEqual(client.original_source_us,180000)
        self.assertEqual(client.origin_deadline_us,500000)
        self.assertEqual(len(client.errors),1)
        s.now=1700000;s.score()
        self.assertEqual(client.delivery_states["UNAVAILABLE"],1)
        self.assertFalse(s.relay_valid())

    def test_coarse_observer_never_creates_precision_or_GM_authority(self):
        r=run(Profile(seconds=20,coarse_client=True))
        self.assertGreater(r["coarse_client"]["public_receipts"],0)
        self.assertEqual(r["coarse_client"]["precision_anchors"],0)
        self.assertFalse(r["coarse_client"]["GM_authority"])
        self.assertNotIn("12",r["listeners"])

    def test_qualification_suite_prevalidates_and_goal_denominators(self):
        from tools.v12_sim import qualification_cases,goal_screen
        cases=list(qualification_cases(1,60))
        self.assertEqual(len(cases),188)
        for _,cfg,_ in cases:cfg.validate()
        result=run(cases[0][1])
        self.assertTrue(all(goal_screen(result).values()))

    def test_short_initial_fit_refuses_output_until_span_is_ready(self):
        clock=Clock(4,250000)
        for i in range(4):
            clock.ingest((TC01,1,1,1),i+1,i*62500,i*62500+1000,i*62500000,i*62500000)
        self.assertFalse(clock.ready())
        self.assertIsNone(clock.predict(700000,700000000))
        clock.ingest((TC01,1,1,1),5,250000,251000,250000000,250000000)
        self.assertTrue(clock.ready());self.assertEqual(clock.predict(700000,700000000),700000000)

    def test_short_capture_startup_counterexample_has_conservative_readiness(self):
        from tools.v12_sim import qualification_cases,goal_screen
        cases=list(qualification_cases(1,60))
        # Quiet groupB,350ms window,4GMs,15ms ACL phase; old two-row
        # output was retained as a1.108us startup tail in qualification-01.
        result=run(cases[15][1])
        self.assertTrue(all(goal_screen(result).values()))
        self.assertGreater(result["listeners"]["7"]["first_usable_update_us"],0)

    def test_lost_direct_GM_becomes_an_actual_public_receiver(self):
        r=run(Profile(seconds=30,observe_GMs=True,quiet_listen=True,two_windows=True,
            pa_interval_us=62500,run_active_us=500000,precision_min_refs=4,precision_min_span_us=250000,
            disconnect_node=TC01,disconnect_at_us=12000000))
        self.assertNotIn(str(TC01),r["GM_precision_observers"])
        self.assertEqual(r["listeners"][str(TC01)]["clock_mode"],"HALF_DUTY_CLIENT")
        self.assertGreater(r["listeners"][str(TC01)]["corrected"],0)
        self.assertIn(TC01,r["membership"]["forced_client"])

    def test_four_unit_mesh_requires_no_extra_physical_receiver_or_GM(self):
        from tools.v12_sim import qualification_cases,goal_screen
        case=next(c for c in qualification_cases(1,60) if c[0]=="v12-four-unit-nominal")
        r=run(case[1])
        self.assertEqual(r["membership"]["participants"],[TC01,TC03])
        self.assertEqual(set(r["listeners"]),{str(C1)})
        self.assertEqual(r["listeners"][str(C1)]["timing_path"],"ONE_HOP_EXTENSION")
        self.assertEqual(r["four_unit_mesh_mapping"][str(RELAY)],"C-3 time-only relay")
        self.assertTrue(all(goal_screen(r).values()))

    def test_runner_enumerates_all_scenario_families_before_execution(self):
        from tools.v12_sim import stress_cases
        cases=list(stress_cases(1,10,2))
        for _,cfg,_ in cases:
            cfg.validate()
        families={name for name,_,_ in cases}
        self.assertIn("one-hop-direct-GM-disconnect",families)
        self.assertIn("one-hop-GM-only-via-relay",families)
        self.assertIn("one-hop-combined",families)

    def test_whole_relay_packet_must_fit_original_permission(self):
        s=Simulator(Profile(seconds=10,one_hop=True));r=s.relay
        scope=(TC01,1,1,1);s.train_epochs[TC01]=1
        r.clock.ingest(scope,1,750040,760000,s.local_ns(r,750040),750040000)
        r.clock.ingest(scope,2,875040,885000,s.local_ns(r,875040),875040000)
        s.now=1062500;r.original_source_us=875040;r.path=(TC01,1,1,0)
        r.extension_permit=Permit(TC01,1,1,1,1,1100000,s.now+1000)
        s.relay_publish()
        self.assertFalse(s.relay_offers)
        self.assertEqual(s.counters["relay_deadline_headroom_refused"],1)
    def test_max_four_counts_coordinator_and_overflow_receives_as_client(self):
        r=run(Profile(seconds=20,gm_count=6))
        self.assertEqual(r["membership"]["participants"],[TC01,TC03,C3,G4])
        self.assertEqual(r["membership"]["forced_client"],[G5,G6])
        self.assertTrue(all(r["listeners"][str(n)]["corrected"]>100 for n in (G5,G6)))
        self.assertTrue(r["safety_pass"])

    def test_overflow_authenticated_report_cannot_enter_control(self):
        s=Simulator(Profile(seconds=10,gm_count=6))
        m=s.message("REPORT",G5,TC03,flags=3,source_second=1)
        self.assertFalse(s.receive(m.encode()));s.send(m)
        self.assertFalse(s.queues[G5,TC03]);self.assertFalse(s.live(G5))
        self.assertFalse(s.accepted_sequences)

    def test_indirect_gm_is_client_and_never_reports_or_gets_permit(self):
        s=Simulator(Profile(seconds=20,gm_count=4,indirect_gm=G4,one_hop=True))
        self.assertFalse(s.receive(s.message("REPORT",G4,TC03,flags=3,source_second=1).encode()))
        r=s.run()
        self.assertIn(G4,r["membership"]["forced_client"])
        self.assertNotIn(G4,r["offers"])
        self.assertGreater(r["listeners"][str(G4)]["corrected"],100)
        self.assertEqual(r["listeners"][str(G4)]["timing_path"],"ONE_HOP_EXTENSION")

    def test_direct_link_loss_withdraws_gm_permission(self):
        r=run(Profile(seconds=25,disconnect_node=TC01,disconnect_at_us=12000000))
        self.assertTrue(r["safety_pass"])
        self.assertIn(TC01,r["membership"]["forced_client"])
        after=[e for e in r["transitions"] if e["kind"]=="PUBLISHER_ENABLED" and e["at_us"]>12000000]
        self.assertTrue(after);self.assertTrue(all(e["root"]!=TC01 for e in after))

    def test_off_phase_extension_retains_origin_and_costs_both_directions(self):
        r=run(Profile(seconds=20,one_hop=True,clients=3,far_clients=2))
        self.assertTrue(r["safety_pass"]);self.assertFalse(r["membership"]["relay_has_GM_authority"])
        for node in (7,8):
            listener=r["listeners"][str(node)]
            self.assertGreater(listener["corrected"],100)
            self.assertEqual(listener["last_origin_path"],(TC01,1,1,1))
            self.assertTrue(listener["numeric_screen"])
        self.assertTrue(all(e.get("relative_phase_us",62500)==62500 for e in r["one_hop"]["transitions"]))
        self.assertGreater(r["radios"][str(RELAY)]["ledger_total_union_us"],r["radios"][str(RELAY)]["ledger_total_rx_us"])

    def test_upstream_loss_cannot_manufacture_extension(self):
        r=run(Profile(seconds=20,one_hop=True,upstream_loss=1))
        self.assertEqual(r["one_hop"]["offers"],0)
        self.assertEqual(r["listeners"]["2"]["corrected"],0)
        self.assertTrue(r["safety_pass"])

    def test_expired_origin_and_depth_two_are_refused(self):
        s=Simulator(Profile(seconds=10,one_hop=True));client=s.receivers[0]
        scope=(RELAY,1,1,TC01,1,1);client.history[scope,1]=(1000,2000000)
        s.now=700000;s.relay_correct(client,scope,[(1,1000,1000000,1000,1000000)])
        self.assertFalse(client.clock.rows)
        scope2=(RELAY,1,1,RELAY,1,1);client.history[scope2,1]=(699000,700000000)
        s.relay_correct(client,scope2,[(1,699000,699000000,699000,1000000)])
        self.assertFalse(client.clock.rows)
        s.correct(s.relay,(RELAY,1,1,1),[(1,699000,699000000)])
        self.assertFalse(s.relay.clock.rows)

    def test_relay_bias_and_delayed_origin_fail_numeric_or_coverage_screen(self):
        r=run(Profile(seconds=20,one_hop=True,relay_map_bias_ns=1500))
        self.assertFalse(r["listeners"]["2"]["numeric_screen"])
        delayed=run(Profile(seconds=20,one_hop=True,relay_observer_stall_us=1000000))
        self.assertEqual(delayed["listeners"]["2"]["corrected"],0)
        self.assertTrue(delayed["safety_pass"])

    def test_one_hop_reboot_source_handoff_and_budget(self):
        r=run(Profile(seconds=40,one_hop=True,clients=3,far_clients=2,gm_count=6,
                      relay_reboot_us=10000000,source_loss_start_us=20000000,source_loss_us=5000000))
        self.assertTrue(r["safety_pass"])
        origins=[e["origin"][0] for e in r["one_hop"]["transitions"] if e["kind"]=="RELAY_ENABLED"]
        self.assertIn(TC01,origins);self.assertIn(TC03,origins)
        self.assertTrue(all(v["max_rx_us_per_1s"]<=500000 and v["max_union_us_per_6s"]<=3000000 for v in r["radios"].values()))


if __name__ == "__main__":
    unittest.main()
