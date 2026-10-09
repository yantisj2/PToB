#!/usr/bin/env python3
"""V12 extension to the existing offline lab. Never opens serial or builds firmware."""
from __future__ import annotations
import argparse
import dataclasses
import hashlib
import json
import random
import shutil
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))
from tools.v12_model import Profile, TC01, run

LAB = REPO/"reviews/v10-model-20261005-01"
INPUTS = ["tools/v12_model.py", "tools/v12_sim.py", "tests/test_v12_model.py",
          "protocol/v12_model_vectors.json", "protocol/v12_model_contract.json",
          "config/v12_offline_profile.json", "config/v12_one_hop_profile.json",
          "config/v12_quiet_client_profile.json",
          "config/v12_native_lab_profile.json",
          "config/v12_four_unit_mesh_profile.json",
          "docs/V12_MODEL_RULES.md"]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True)+"\n")


def receipt(out, argv):
    files = {name: digest(REPO/name) for name in INPUTS if (REPO/name).exists()}
    for name in files:
        target=out/"sources"/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(REPO/name,target)
    registry = LAB/"model-registry.json"
    write(out/"receipt.json", {"schema":1, "command":argv, "files":files,
          "baseline_registry_sha256":digest(registry) if registry.exists() else None,
          "baseline_identity":json.loads(registry.read_text())["latest"] if registry.exists() else None,
          "execution":"SYNTHETIC_OFFLINE_ONLY", "physical_actions":False,
          "independent_precision":None, "quality":"UNKNOWN", "PTS":"OFF",
          "clock_source":"integer virtual scheduler with declared clock/map/capture inputs",
          "wire_status":"proposal fixture; no production firmware codec assertion"})


def install(lab):
    # Add one dispatch point to this lab, preserving frozen snapshots/registry.
    p=lab/"harness.py"
    source=p.read_text()
    marker="    # V12 tracked proposal extension; frozen registry unchanged.\n"
    injection=marker+"    if argv is None and len(sys.argv)>1 and sys.argv[1]=='v12':\n        from v12_entry import main as v12_main\n        return v12_main(sys.argv[2:])\n"
    if marker not in source:
        needle="def main(argv=None):\n"
        if source.count(needle)!=1:
            raise ValueError("unknown harness entry point")
        p.write_text(source.replace(needle, needle+injection))
    entry="from pathlib import Path\nimport sys\nsys.path.insert(0, str(Path(__file__).resolve().parents[2]))\nfrom tools.v12_sim import main\n"
    (lab/"v12_entry.py").write_text(entry)
    annex="\n\n## October 6 V12 proposal extension\n\nThe executable V12 proposal is `harness.py v12`; it imports tracked\n`tools/v12_model.py` and `tools/v12_sim.py` from this workspace. Read\n[the V12 rules](../../docs/V12_MODEL_RULES.md) and\n[the plan](../../docs/V12_PERIODIC_COORDINATION_PLAN.md).\nExisting frozen snapshots and the `latest` source registry remain the V11\nregression baseline. V12 results are synthetic hypotheses, never installed\nfirmware or physical RF/output qualification. Use fresh output directories.\n"
    rules=lab/"MODEL_RULES.md"
    if "## October 6 V12 proposal extension" not in rules.read_text():
        rules.write_text(rules.read_text()+annex)
    print(json.dumps({"installed":str(lab), "entry":"harness.py v12", "registry_unchanged":True}))


def stress_cases(seeds, seconds, random_cases):
    # Matched seeds/phase/cadence permit fair within-fixture comparisons.
    for seed in range(1, seeds+1):
        for phase in range(0,1000000,62500):
            for cadence in (125000,1000000):
                for mode in "ABCD":
                    yield "phase", Profile(mode=mode, seconds=seconds, seed=seed,
                          scan_phase_us=phase, pa_interval_us=cadence,
                          legacy_interval_us=cadence, clients=3), False
    directed = {
        "cold":dict(warm_start=False,seconds=max(100,seconds)),
        "source-loss":dict(source_loss_start_us=15000000,source_loss_us=10000000,seconds=max(100,seconds)),
        "root-reboot":dict(root_reboot_us=15000000,seconds=max(100,seconds)),
        "coordinator-reboot":dict(coordinator_reboot_us=15000000),
        "loss-10pct":dict(loss=.1,ack_loss=.1,rf_loss=.1),
        "loss-50pct":dict(loss=.5,ack_loss=.5,rf_loss=.5),
        "all-ACK-loss":dict(ack_loss=1), "all-control-loss":dict(loss=1),
        "all-RF-loss":dict(rf_loss=1),
        "burst":dict(burst_start_us=12000000,burst_us=10000000),
        "asymmetric-partition":dict(burst_start_us=12000000,burst_us=10000000,asymmetric=True),
        "logger-stall":dict(logger_at_us=12000000,logger_stall_us=4000000),
        "logger-20s":dict(logger_at_us=12000000,logger_stall_us=20000000),
        "unknown-map":dict(map_known=False), "unknown-stop":dict(stop_known=False),
        "lease-withdrawal":dict(permit_pause_start_us=12000000,permit_pause_us=20000000),
        "data-update-miss":dict(data_update_misses=True),
        "ambiguous-all":dict(ambiguity_rate=1),
        "biased-capture":dict(timestamp_bias_ns=2000),
        "clock-drift":dict(drift_ppb_s=100),
        "discovery-collision":dict(mode="B",discovery_phase_us=0),
        "late-discovery":dict(discovery_blackout_us=12000000),
        "queue-one":dict(control_queue=1,loss=.5,ack_loss=.5),
        "max-listen":dict(scan_us=500000),
        "map-edge":dict(map_error_us=-100,timer_ppm=-100),
        "false-map-bound":dict(map_error_us=-50000,permit_pause_start_us=12000000,permit_pause_us=20000000),
        "false-stop-bound":dict(stop_delay_us=30000,permit_pause_start_us=12000000,permit_pause_us=20000000),
        "false-timer-bound":dict(timer_ppm=-20000,permit_pause_start_us=12000000,permit_pause_us=20000000),
    }
    for seed in range(1,seeds+1):
        for name, overrides in directed.items():
            values=dict(seconds=seconds,seed=seed,clients=3); values.update(overrides)
            yield name, Profile(**values), name.startswith("false-")
    rng=random.Random(20261006)
    for index in range(random_cases):
        values=dict(seconds=seconds,seed=rng.randrange(1,1<<31),clients=rng.randint(1,3),
              mode=rng.choice("CD"),scan_phase_us=rng.randrange(1000000),
              acl_phase_us=rng.randrange(30000),discovery_phase_us=rng.randrange(10000),
              scan_us=rng.choice([0,200000,400000,500000]),
              loss=rng.choice([0,.01,.1,.3,.7]),ack_loss=rng.choice([0,.1,.5,1]),
              rf_loss=rng.choice([0,.01,.1,.5]),history=rng.randint(1,4),
              work_us=rng.choice([1000,20000,200000]),
              burst_start_us=15000000,burst_us=rng.choice([0,500000,3000000,12000000]),
              asymmetric=bool(rng.getrandbits(1)),
              coordinator_reboot_us=rng.choice([0,0,20000000]),
              root_reboot_us=rng.choice([0,0,25000000]),
              source_loss_start_us=18000000,source_loss_us=rng.choice([0,0,4000000]),
              logger_at_us=30000000,logger_stall_us=rng.choice([0,0,2000000,8000000]),
              map_error_us=rng.randint(-100,100),timer_ppm=rng.uniform(-100,100),
              timestamp_noise_ns=rng.choice([20,100,300]),
              ambiguity_rate=rng.choice([0,0,.1]),data_update_misses=bool(rng.getrandbits(1)))
        yield "combined-random",Profile(**values),False
    # One-hop is timing fanout, never a forwarding path for GM participation.
    for seed in range(1,seeds+1):
        for phase in (0,500,2000,5000,15000,30000,62500,100000,124000):
            for client_phase in (0,250000,500000,750000):
                yield "one-hop-phase",Profile(seconds=seconds,seed=seed,one_hop=True,
                    clients=3,far_clients=2,gm_count=6,relay_phase_us=phase,scan_phase_us=client_phase),False
        mesh_faults={
            "nominal":{},"upstream-total-loss":dict(upstream_loss=1),
            "downstream-total-loss":dict(downstream_loss=1),
            "hop-loss":dict(upstream_loss=.3,downstream_loss=.3),
            "root-loss":dict(source_loss_start_us=15000000,source_loss_us=10000000),
            "coordinator-reboot":dict(coordinator_reboot_us=15000000),
            "relay-reboot":dict(relay_reboot_us=15000000),
            "relay-bias":dict(relay_map_bias_ns=1500),
            "delayed-origin":dict(relay_observer_stall_us=1000000),
            "direct-GM-disconnect":dict(disconnect_node=TC01,disconnect_at_us=15000000),
            "GM-only-via-relay":dict(indirect_gm=6),
            "lease-withdrawal":dict(permit_pause_start_us=12000000,permit_pause_us=20000000),
            "ambiguous-payload":dict(data_update_misses=True,ambiguity_rate=.1),
            "logging-stall":dict(logger_at_us=15000000,logger_stall_us=4000000),
            "tight-budget":dict(scan_us=500000,relay_guard_us=5000),
        }
        for name,kw in mesh_faults.items():
            yield "one-hop-"+name,Profile(seconds=seconds,seed=seed,one_hop=True,clients=3,far_clients=2,gm_count=6,**kw),False
    rng=random.Random(12120261006)
    for index in range(random_cases//2):
        yield "one-hop-combined",Profile(seconds=seconds,seed=rng.randrange(1,1<<31),one_hop=True,
                gm_count=rng.choice([3,4,5,6]),clients=3,far_clients=rng.choice([1,2,3]),
                indirect_gm=rng.choice([255,255,6]),relay_phase_us=rng.randrange(125000),
                scan_phase_us=rng.randrange(1000000),acl_phase_us=rng.randrange(30000),
                upstream_loss=rng.choice([0,.1,.3,.7]),downstream_loss=rng.choice([0,.1,.3,.7]),
                loss=rng.choice([0,.1,.5]),ack_loss=rng.choice([0,.1,.5]),
                relay_reboot_us=rng.choice([0,0,20000000]),
                source_loss_start_us=30000000,source_loss_us=rng.choice([0,0,10000000]),
                coordinator_reboot_us=rng.choice([0,0,15000000]),
                logger_at_us=35000000,logger_stall_us=rng.choice([0,0,4000000]),
                relay_observer_stall_us=rng.choice([0,0,300000]),
                map_error_us=rng.randint(-100,100),timer_ppm=rng.uniform(-100,100)),False


def quiet_cases(seeds,seconds):
    for seed in range(1,seeds+1):
        for window in (350000,400000,450000):
            for choice in (0,1):
                for mesh in (False,True):
                    yield "quiet-nominal",Profile(seconds=seconds,seed=seed,quiet_listen=True,
                        ble_window_us=window,quiet_run_choice=choice,one_hop=mesh,
                        clients=3,far_clients=2,gm_count=6),False
        faults={"RF-loss":dict(rf_loss=.1),"hop-loss":dict(upstream_loss=.1,downstream_loss=.1),
                "burst":dict(burst_start_us=15000000,burst_us=3000000),
                "coordinator-reboot":dict(coordinator_reboot_us=15000000),
                "root-loss":dict(source_loss_start_us=20000000,source_loss_us=10000000),
                "relay-reboot":dict(relay_reboot_us=15000000),
                "biased-capture":dict(timestamp_bias_ns=1500),
                "clock-drift":dict(drift_ppb_s=1000),
                "logging-stall":dict(logger_at_us=20000000,logger_stall_us=3000000),
                "missed-payload":dict(data_update_misses=True),
                "all-upstream-loss":dict(upstream_loss=1),
                "all-downstream-loss":dict(downstream_loss=1)}
        for name,kw in faults.items():
            yield "quiet-"+name,Profile(seconds=seconds,seed=seed,quiet_listen=True,one_hop=True,
                clients=3,far_clients=2,gm_count=6,**kw),False


def qualification_cases(seeds,seconds):
    """Updated cycle goals, separated from fault/unavailability screens."""
    base=dict(clients=3,far_clients=2,pa_interval_us=62500,relay_phase_us=31250,
              two_windows=True,quiet_listen=True,run_active_us=500000,
              sync_timeout_us=5000000,full_clients=1,observe_GMs=True,
              native_w12=True,coarse_client=True,precision_min_refs=4,precision_min_span_us=250000)
    for seed in range(1,seeds+1):
        for window in (350000,400000,450000):
            for choice in (0,1):
                for mesh in (False,True):
                    for population in (3,4,6):
                        for phase in (0,15000):
                            yield "v12-nominal",Profile(**base,seconds=seconds,seed=seed,
                                ble_window_us=window,quiet_run_choice=choice,one_hop=mesh,
                                gm_count=population,acl_phase_us=phase),False
        for window in (350000,400000,450000):
            for choice in (0,1):
                for phase in (0,15000):
                    yield "v12-four-unit-nominal",Profile(**(base|dict(seconds=seconds,seed=seed,
                        clients=1,far_clients=1,full_clients=0,gm_count=2,one_hop=True,
                        ble_window_us=window,quiet_run_choice=choice,acl_phase_us=phase))),False
    for seed in range(1,21):
        for window in (350000,400000):
            for native in (False,True):
                values=base|dict(seconds=max(120,seconds),seed=seed,one_hop=True,
                                gm_count=4,ble_window_us=window,native_w12=native,rf_loss=.1)
                yield "v12-loss10-"+("native" if native else "capped")+f"-{window}",Profile(**values),False
    faults={
        "control10":dict(loss=.1,ack_loss=.1),"control50":dict(loss=.5,ack_loss=.5),
        "total-RF":dict(rf_loss=1),"total-upstream":dict(upstream_loss=1),
        "total-downstream":dict(downstream_loss=1),"all-ACK-loss":dict(ack_loss=1),
        "outage":dict(burst_start_us=15000000,burst_us=10000000),
        "partition":dict(burst_start_us=15000000,burst_us=10000000,asymmetric=True),
        "coordinator-reboot":dict(coordinator_reboot_us=15000000),
        "root-reboot":dict(root_reboot_us=15000000),
        "relay-reboot":dict(relay_reboot_us=15000000),
        "source-handoff":dict(source_loss_start_us=15000000,source_loss_us=10000000),
        "direct-GM-loss":dict(disconnect_node=TC01,disconnect_at_us=15000000),
        "indirect-GM":dict(indirect_gm=6),"unknown-map":dict(map_known=False),
        "unknown-stop":dict(stop_known=False),"capture-bias":dict(timestamp_bias_ns=1500),
        "relay-bias":dict(relay_map_bias_ns=1500),"high-drift":dict(drift_ppb_s=1000),
        "observer-stall":dict(logger_at_us=15000000,logger_stall_us=4000000),
        "relay-work-stall":dict(relay_observer_stall_us=1000000),
        "payload-miss":dict(data_update_misses=True),"ambiguity":dict(ambiguity_rate=1),
        "queue-pressure":dict(control_queue=1,loss=.5,ack_loss=.5),
    }
    for seed in range(1,seeds+1):
        for name,kw in faults.items():
            yield "v12-fault-"+name,Profile(**(base|dict(seconds=seconds,seed=seed,gm_count=6,one_hop=True)|kw)),False


def goal_screen(result):
    public=result["listeners"]
    full=list(v for v in public.values() if v["clock_mode"]=="FULL_PRECISION")
    gms=list(v for v in result["GM_precision_observers"].values() if v["eligible_source_windows"])
    return {"all_clients_1Hz":all(v["warmed_1Hz_update_fraction"]==1 for v in public.values()),
            "full_clocks_both_windows":all(v["warmed_two_window_fraction"]==1 for v in full+gms),
            "all_delivered_numeric_screen":all(v["numeric_screen"] for v in list(public.values())+gms),
            "client_duty":result["safety"]["client_quiet_schedule"],
            "coarse_no_precision_or_authority":result["coarse_client"]["precision_anchors"]==0 and not result["coarse_client"]["GM_authority"]}


def stress(out, seeds, seconds, random_cases, mesh_only=False, quiet_only=False, qualification=False):
    start=time.perf_counter(); groups={}; failures=[]; assumption_failures=[]; counter=0; virtual=0
    cases=list(qualification_cases(seeds,seconds) if qualification else
               quiet_cases(seeds,seconds) if quiet_only else stress_cases(seeds,seconds,random_cases))
    goal_failures=[]; loss_groups={}
    if mesh_only:
        cases=[case for case in cases if case[0].startswith("one-hop")]
    for _,cfg,_ in cases:
        cfg.validate()
    with (out/"cases.jsonl").open("w") as index:
        for family,cfg,invalid_bound in cases:
            case=out/f"case-{counter:04d}";case.mkdir()
            result=run(cfg);write(case/"config.json",dataclasses.asdict(cfg));write(case/"metrics.json",result)
            row={"case":case.name,"family":family,"mode":cfg.mode,"seconds":cfg.seconds,
                 "assumptions_intentionally_falsified":invalid_bound,"safety":result["safety"],
                 "listeners":result["listeners"],"counters":result["counters"]}
            row["membership"]=result["membership"];row["one_hop"]=result["one_hop"]
            if qualification:
                row["goals"]=goal_screen(result)
                if family in ("v12-nominal","v12-four-unit-nominal") and not all(row["goals"].values()):
                    goal_failures.append(row)
                if family.startswith("v12-loss10"):
                    for n,v in result["listeners"].items():
                        key=family+":"+v["timing_path"]+":"+v["clock_mode"]
                        g=loss_groups.setdefault(key,Counter())
                        g["eligible_seconds"]+=cfg.seconds-11
                        g["missed_seconds"]+=v["missed_update_seconds_after_10s"]
                    for n,v in result["GM_precision_observers"].items():
                        if v["eligible_source_windows"]:
                            key=family+":GM:"+n;g=loss_groups.setdefault(key,Counter())
                            g["eligible_windows"]+=v["eligible_source_windows"]
                            g["missed_windows"]+=v["eligible_source_windows"]-round(v["warmed_two_window_fraction"]*v["eligible_source_windows"])
            index.write(json.dumps(row)+"\n");index.flush()
            group=groups.setdefault(family,Counter());group["cases"]+=1
            group["safety_pass"]+=int(result["safety_pass"])
            group["listener_numeric_screen"]+=sum(v["numeric_screen"] for v in result["listeners"].values())
            group["listeners"]+=len(result["listeners"])
            group["listeners_zero_corrected"]+=sum(v["corrected"]==0 for v in result["listeners"].values())
            if not result["safety_pass"]:
                (assumption_failures if invalid_bound else failures).append(row)
            counter+=1;virtual+=cfg.seconds
            if counter%50==0:
                print(json.dumps({"cases":counter,"unexpected_safety_failures":len(failures),"wall_s":round(time.perf_counter()-start,1)}),flush=True)
    loss_failures={}
    for key,g in loss_groups.items():
        count=g.get("eligible_seconds",g.get("eligible_windows",0));miss=g.get("missed_seconds",g.get("missed_windows",0))
        g["reception_fraction"]=1-miss/max(1,count)
        if g["reception_fraction"]<.999:loss_failures[key]=g
    summary={"schema":1,"cases":counter,"virtual_seconds":virtual,"wall_s":time.perf_counter()-start,
             "unexpected_safety_failures":failures,"intentional_bound_counterexamples":assumption_failures,
             "nominal_goal_failures":goal_failures,"loss10_groups":loss_groups,"loss10_target_failures":loss_failures,
             "groups":groups,"pass":not failures and not goal_failures and not loss_failures,"independent_precision":None,"PTS":"OFF",
             "pass_scope":"finite declared-bound safety and explicit nominal/loss10 goals" if qualification else
                          "declared-bound safety only; no reliability or precision blanket pass"}
    write(out/"summary.json",summary)
    return {k:summary[k] for k in ("cases","virtual_seconds","wall_s","pass")}|{
           "unexpected_safety_failures":len(failures),"intentional_bound_counterexamples":len(assumption_failures),
           "nominal_goal_failures":len(goal_failures),"loss10_target_failures":len(loss_failures)}


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest="command",required=True)
    q=sub.add_parser("install");q.add_argument("--lab",type=Path,default=LAB)
    sub.add_parser("variables")
    for name in ("run","stress","proof","quiet-stress","qualification"):
        q=sub.add_parser(name);q.add_argument("--out",type=Path,required=True)
        if name=="run":
            q.add_argument("--config",type=Path);q.add_argument("--set",action="append",default=[])
        if name in ("stress","quiet-stress","qualification"):
            q.add_argument("--seeds",type=int,default=3);q.add_argument("--seconds",type=int,default=60)
            q.add_argument("--random-cases",type=int,default=300)
            q.add_argument("--mesh-only",action="store_true")
    args=p.parse_args(argv)
    if args.command=="install":return install(args.lab.resolve())
    if args.command=="variables":
        print(json.dumps(dataclasses.asdict(Profile()),indent=2));return
    if args.command in ("stress","quiet-stress","qualification") and (args.seeds<1 or args.random_cases<0 or not 10<=args.seconds<=86400):
        raise ValueError("invalid sweep dimensions")
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    receipt(out,argv or sys.argv[1:])
    if args.command=="run":
        values=json.loads(args.config.read_text()) if args.config else {}
        for setting in args.set:
            key,value=setting.split("=",1);values[key]=json.loads(value)
        cfg=Profile(**values);write(out/"config.json",dataclasses.asdict(cfg))
        result=run(cfg);write(out/"metrics.json",result)
        print(json.dumps({"safety":result["safety"],"listeners":result["listeners"],"out":str(out)}))
    elif args.command=="proof":
        r=subprocess.run([sys.executable,"-m","unittest","discover","-s","tests","-p","test_v12_model.py","-v"],cwd=REPO,capture_output=True,text=True)
        (out/"tests.log").write_text(r.stdout+r.stderr);write(out/"summary.json",{"pass":r.returncode==0,"exit_code":r.returncode})
        print(json.dumps({"pass":r.returncode==0,"out":str(out)}))
        if r.returncode:raise SystemExit(r.returncode)
    else:
        try:
            summary=stress(out,args.seeds,args.seconds,args.random_cases,args.mesh_only,args.command=="quiet-stress",args.command=="qualification")
        except Exception as e:
            write(out/"incomplete.json",{"status":"INCOMPLETE","error":str(e),"type":type(e).__name__})
            raise
        print(json.dumps(summary))
        if not summary["pass"]:raise SystemExit(1)


if __name__=="__main__":
    try:main()
    except (ValueError,FileExistsError,TypeError) as e:raise SystemExit(str(e))
