# Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
"""The benchmark driver, run inside the editor by bench_launch.ps1 (-ExecCmds "py bench_run.py").

Two walls of buttons (README.md): DREAMBENCH_MODE=screen puts a screen widget class on the screen and plays its animations
by its click handler, round after round; DREAMBENCH_MODE=world loads a level of world widget actors that animate on their
own and points a camera at them. It measures an idle window and the animated windows, writes "[DreamPerf]" lines to the
log, and with DREAMBENCH_TRACE / DREAMBENCH_CSV a trace and a CSV profile of the animated windows.

In the editor it plays the level in PIE (DREAMBENCH_GAME=0); with DREAMBENCH_GAME=1 it runs in a -game process on the
editor binary, which has no editor UI to pay for, and finds its world on its own.

Everything is configured through the environment bench_launch.ps1 sets; see that script for the meaning of each variable.
"""
import math
import os
import time
import unreal

OUT = os.environ.get("DREAMBENCH_OUT", "")
MODE = os.environ.get("DREAMBENCH_MODE", "screen")
TAG = os.environ.get("DREAMBENCH_TAG", "bench")
GAME = os.environ.get("DREAMBENCH_GAME", "0") != "0"
WARMUP_S = float(os.environ.get("DREAMBENCH_WARMUP", "20"))
WINDOW_S = float(os.environ.get("DREAMBENCH_WINDOW", "8"))
ANIM_LENGTH_S = float(os.environ.get("DREAMBENCH_ANIM_S", "1.9"))
ANIM_PERIOD_S = float(os.environ.get("DREAMBENCH_ANIM_PERIOD", "2.3"))
ANIM_ROUNDS = int(os.environ.get("DREAMBENCH_ROUNDS", "4"))
SCREEN_WIDGET = os.environ.get("DREAMBENCH_SCREEN_WIDGET", "/Game/UI/DW_Benchmark_Button_SW.DW_Benchmark_Button_SW_C")
AB = [c.strip() for c in os.environ.get("DREAMBENCH_AB_CMDS", "").split(";") if c.strip()]
STATS = [c.strip() for c in os.environ.get("DREAMBENCH_STATS_CMDS", "").split(";") if c.strip()]
TRACE = os.environ.get("DREAMBENCH_TRACE", "0") != "0"
TRACE_CHANNELS = os.environ.get("DREAMBENCH_TRACE_CHANNELS", "") or "default,counters,region"
CSV = os.environ.get("DREAMBENCH_CSV", "0") != "0"
# An external sampler (StackSampler.cs) samples the game thread while this file exists; see bench_launch.ps1 -Sample.
SAMPLE_FLAG = os.environ.get("DREAMBENCH_SAMPLE_FLAG", "")
# "window": side A's windows from 0.3 s in; "all": side A whole; "edges": around the plays' starts; "ends": their ends.
SAMPLE_PHASE = os.environ.get("DREAMBENCH_SAMPLE_PHASE", "window")

# The suite checks DreamGUI's shortcuts against the long way round (the test host turns these on in its config); what
# is measured here is the shortcuts.
VERIFY_OFF = ["r.DreamUI.VerifyPartialPrepare 0", "r.DreamUI.VerifyKeptPointers 0"]

if OUT:
    os.makedirs(OUT, exist_ok=True)

editor_subsystem = None if GAME else unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level_subsystem = None if GAME else unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
state = {"phase": "wait_level", "t0": time.time(), "deltas": [], "handle": None, "widget": None, "world": None}
sample_flag_on = [False]


def log(msg):
    unreal.log_warning("[DreamPerf] " + msg)


def sample_flag(on):
    # Once per change: opening a file every frame was a visible share of the frames being measured.
    if not SAMPLE_FLAG or sample_flag_on[0] == on:
        return
    sample_flag_on[0] = on
    try:
        if on:
            open(SAMPLE_FLAG, "w").close()
        elif os.path.exists(SAMPLE_FLAG):
            os.remove(SAMPLE_FLAG)
    except OSError:
        pass


def find_game_world():
    """In -game, the world the map loaded into: the first that is not an untitled editor scratch world."""
    for candidate in unreal.ObjectIterator(unreal.World):
        try:
            name = candidate.get_name()
            if name.startswith("Untitled") or name.startswith("Transient"):
                continue
            if unreal.GameplayStatics.get_player_controller(candidate, 0) is None:
                continue
            return candidate
        except Exception:
            continue
    return None


def game_world():
    if GAME:
        return state["world"]
    return editor_subsystem.get_game_world()


def world():
    if GAME:
        return state["world"]
    return editor_subsystem.get_game_world() or editor_subsystem.get_editor_world()


def console(cmd):
    unreal.SystemLibrary.execute_console_command(world(), cmd)


def summarize(name, deltas):
    if not deltas:
        log("%s: no frames" % name)
        return
    ms = sorted(d * 1000.0 for d in deltas)
    avg = sum(ms) / len(ms)
    log("%s: %d frames, avg %.2f ms (%.1f FPS), median %.2f ms, p95 %.2f ms, max %.2f ms"
        % (name, len(ms), avg, 1000.0 / avg, ms[len(ms) // 2], ms[min(len(ms) - 1, int(len(ms) * 0.95))], ms[-1]))


def enter(phase):
    state["phase"] = phase
    state["t0"] = time.time()
    log("phase " + phase)


def play_all():
    """What the wall's Play button does: its click handler, or failing that each row's CallAnimation."""
    widget = state.get("widget")
    if widget is None:
        return
    way = state.get("play_way")
    if way in (None, "click"):
        try:
            widget.call_method("Button_On_Clicked")
            if way is None:
                state["play_way"] = "click"
                log("playing through Button_On_Clicked")
            return
        except Exception as error:
            if way == "click":
                raise
            log("no Button_On_Clicked (%s); calling each row's CallAnimation" % error)
    rows = widget.get_editor_property("Buttons")
    for row in rows:
        row.call_method("CallAnimation")
    if way is None:
        state["play_way"] = "rows"
        log("playing through %d rows' CallAnimation" % len(rows))


def aim_at_world_wall(at_world):
    actors = unreal.GameplayStatics.get_all_actors_of_class(at_world, unreal.DreamWorldWidgetActor)
    if not actors:
        log("no world widget actors")
        return
    lo = [1e18, 1e18, 1e18]
    hi = [-1e18, -1e18, -1e18]
    for actor in actors:
        p = actor.get_actor_location()
        for i, v in enumerate((p.x, p.y, p.z)):
            lo[i] = min(lo[i], v)
            hi[i] = max(hi[i], v)
    center = unreal.Vector((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2)
    forward = actors[0].get_actor_forward_vector()
    span = max(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2])
    distance = max(300.0, span * 0.6 / math.tan(math.radians(45.0)))
    state["camera"] = (center - forward * distance, forward.rotator())
    log("%d world widget actors, span %.0f, camera %.0f in front of %s" % (len(actors), span, distance, center))


def quit_now():
    if state["handle"] is not None:
        unreal.unregister_slate_post_tick_callback(state["handle"])
        state["handle"] = None
    log("done")
    if GAME:
        unreal.SystemLibrary.quit_game(state["world"], None, unreal.QuitPreference.QUIT, False)
    else:
        unreal.SystemLibrary.quit_editor()


def tick(delta):
    phase = state["phase"]
    elapsed = time.time() - state["t0"]
    if phase == "wait_level":
        if GAME:
            if state["world"] is None:
                state["world"] = find_game_world()
            if state["world"] is not None and elapsed > 3:
                enter("start")
            elif elapsed > 120:
                log("no game world")
                enter("quit")
            return
        if editor_subsystem.get_editor_world() is not None and elapsed > 5:
            for command in VERIFY_OFF:
                console(command)
            level_subsystem.editor_request_begin_play()
            enter("start")
        return
    at_world = game_world()
    if phase == "start":
        if at_world is None:
            if elapsed > 120:
                log("the game did not start")
                enter("quit")
            return
        for command in VERIFY_OFF:
            unreal.SystemLibrary.execute_console_command(at_world, command)
        if MODE == "screen":
            subsystem = unreal.DreamScreenUISubsystem.get_dream_screen_ui_subsystem(at_world)
            widget_class = unreal.load_class(None, SCREEN_WIDGET)
            state["widget"] = subsystem.create_widget_on_screen(widget_class, 0)
            log("screen widget %s" % (state["widget"].get_name() if state["widget"] else None))
        else:
            aim_at_world_wall(at_world)
            unreal.SystemLibrary.execute_console_command(at_world, "EnableCheats")
            unreal.SystemLibrary.execute_console_command(at_world, "summon CameraActor")
        enter("warmup")
    elif phase == "warmup":
        if MODE == "world" and not state.get("placed") and elapsed > 2 and state.get("camera"):
            state["placed"] = True
            cameras = unreal.GameplayStatics.get_all_actors_of_class(at_world, unreal.CameraActor)
            controller = unreal.GameplayStatics.get_player_controller(at_world, 0)
            if cameras and controller is not None:
                location, rotation = state["camera"]
                cameras[0].set_actor_location_and_rotation(location, rotation, False, True)
                cameras[0].camera_component.set_editor_property("constrain_aspect_ratio", False)
                controller.set_view_target_with_blend(cameras[0], 0.0)
                log("view target set")
        if elapsed > WARMUP_S:
            if TRACE and OUT:
                console('Trace.File "%s" %s' % (os.path.join(OUT, "%s_%s.utrace" % (TAG, MODE)), TRACE_CHANNELS))
            console("Trace.RegionBegin DreamPerf_Idle")
            state["deltas"] = []
            enter("measure_idle")
    elif phase == "measure_idle":
        state["deltas"].append(delta)
        if elapsed > WINDOW_S:
            console("Trace.RegionEnd DreamPerf_Idle")
            summarize("%s idle" % MODE, state["deltas"])
            state["deltas"] = []
            state["round"] = 0
            state["side"] = "A"
            enter("anim_gap")
    elif phase == "anim_gap":
        # World mode: the wall animates on its own; the windows are consecutive stretches of it.
        if SAMPLE_PHASE == "edges" and state["side"] == "A":
            sample_flag(True)
        if elapsed > (0.3 if MODE == "screen" else 0.0):
            if MODE == "screen":
                if SAMPLE_PHASE == "all" and state["side"] == "A":
                    sample_flag(True)
                play_all()
            if state["round"] == 0:
                for command in STATS:
                    console(command)
                if CSV and state["side"] == "A":
                    console("r.GPUCsvStatsEnabled 1")
                    console("CsvProfile Start")
            console("Trace.RegionBegin DreamPerf_Anim_%s" % state["side"])
            enter("anim_window")
    elif phase == "anim_window":
        state["deltas"].append(delta)
        if SAMPLE_PHASE == "edges":
            if state["side"] == "A" and elapsed > 0.25:
                sample_flag(False)
        elif SAMPLE_PHASE == "ends":
            if state["side"] == "A" and elapsed > ANIM_LENGTH_S - 0.4:
                sample_flag(True)
        elif state["side"] == "A" and (elapsed > 0.3 or SAMPLE_PHASE == "all"):
            sample_flag(True)
        if elapsed > (ANIM_LENGTH_S if MODE == "screen" else WINDOW_S / ANIM_ROUNDS):
            console("Trace.RegionEnd DreamPerf_Anim_%s" % state["side"])
            if SAMPLE_PHASE != "all":
                sample_flag(False)
            state["round"] += 1
            if state["round"] < ANIM_ROUNDS:
                enter("anim_gap")
            else:
                summarize("%s anim %s" % (MODE, state["side"]), state["deltas"])
                if CSV and state["side"] == "A":
                    console("CsvProfile Stop")
                if SAMPLE_FLAG and state["side"] == "A":
                    sample_flag(False)
                    try:
                        open(SAMPLE_FLAG + ".done", "w").close()
                    except OSError:
                        pass
                for command in STATS:
                    console(command)
                state["deltas"] = []
                if state["side"] == "A" and AB:
                    for command in AB:
                        console(command)
                    log("B side: %s" % "; ".join(AB))
                    state["side"] = "B"
                    state["round"] = 0
                    enter("settle_b")
                else:
                    if TRACE:
                        console("Trace.Stop")
                    if not GAME:
                        level_subsystem.editor_request_end_play()
                    enter("end")
    elif phase == "settle_b" and elapsed > (ANIM_PERIOD_S if MODE == "screen" else 3.0):
        enter("anim_gap")
    elif phase == "end" and elapsed > (1 if GAME else 5):
        # A sampler resolves its program counters against this process: it says when it is done.
        if SAMPLE_FLAG and not os.path.exists(SAMPLE_FLAG + ".resolved") and elapsed < 120:
            return
        enter("quit")
    elif phase == "quit":
        quit_now()


def guarded_tick(delta):
    try:
        tick(delta)
    except Exception as error:
        import traceback
        log("tick failed in phase %s: %s\n%s" % (state["phase"], error, traceback.format_exc()))
        if state["phase"] != "quit":
            try:
                console("Trace.Stop")
            except Exception:
                pass
            state["phase"] = "quit"
        else:
            quit_now()


state["handle"] = unreal.register_slate_post_tick_callback(guarded_tick)
log("bench armed: mode=%s game=%s out=%s warmup=%ss window=%ss rounds=%d" % (MODE, GAME, OUT, WARMUP_S, WINDOW_S, ANIM_ROUNDS))
