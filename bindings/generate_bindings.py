"""Regenerates the SteamVR action manifest and the Steam Frame default binding. Run: python -I bindings/generate_bindings.py

Right hand = mouse: the laser is the cursor, trigger = left click, bumper = right click, stick up/down = mouse
wheel (zoom). Left hand = camera and modifiers: stick orbits the camera, D-pad pans (arrow keys).
Every other button is a "hotkey" action whose key is set in hw2vr.ini [buttons], and everything can be rebound in
SteamVR's binding UI.

Input paths come from SteamVR's drivers/frame_controller/resources/input/frame_controller_profile.json (right hand
has A/B/X/Y and Menu, left hand has the D-pad and View). Grips are deliberately unbound: SteamVR's compositor uses
grip for its own laser ("quickmouse"), which withholds controller input from the app.
"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent

# Hotkey buttons: hw2vr.ini [buttons] key -> label. Keep in sync with src/vr/vr_input.cpp kButtonNames.
BUTTONS = {
    "a": "A", "b": "B", "x": "X", "y": "Y", "menu": "Menu", "view": "View",
    "dpad_up": "D-pad up", "dpad_down": "D-pad down", "dpad_left": "D-pad left", "dpad_right": "D-pad right",
    "left_trigger": "Left trigger", "left_bumper": "Left bumper",
    "left_stick": "Left stick click", "right_stick": "Right stick click",
}

actions = [
    {"name": "/actions/main/in/pointer", "type": "pose"},
    {"name": "/actions/main/in/leftclick", "type": "boolean"},
    {"name": "/actions/main/in/rightclick", "type": "boolean"},
    {"name": "/actions/main/in/camera", "type": "vector2"},
    {"name": "/actions/main/in/zoom", "type": "vector2"},
] + [{"name": f"/actions/main/in/button_{b}", "type": "boolean"} for b in BUTTONS] + [
    {"name": "/actions/main/out/haptic", "type": "vibration"},
]

localization = {
    "language_tag": "en_US",
    "/actions/main": "Homeworld 2 VR",
    "/actions/main/in/pointer": "Laser pointer (mouse cursor)",
    "/actions/main/in/leftclick": "Left click (select, grab the screen bar)",
    "/actions/main/in/rightclick": "Right click (orders menu)",
    "/actions/main/in/camera": "Camera orbit (right-mouse drag)",
    "/actions/main/in/zoom": "Mouse wheel (zoom)",
    "/actions/main/out/haptic": "Click haptic",
}
for b, label in BUTTONS.items():
    localization[f"/actions/main/in/button_{b}"] = f"Hotkey: {label} (key set in hw2vr.ini [buttons])"

manifest = {
    "default_bindings": [{"controller_type": "frame_controller", "binding_url": "bindings_frame_controller.json"}],
    "actions": actions,
    "action_sets": [{"name": "/actions/main", "usage": "single"}],
    "localization": [localization],
}

R, L = "/user/hand/right/input", "/user/hand/left/input"


def button(path, action, mode="button"):
    return {"path": path, "mode": mode, "inputs": {"click": {"output": f"/actions/main/in/{action}"}}}


def stick(path, position, click):
    return {"path": path, "mode": "joystick", "inputs": {
        "position": {"output": f"/actions/main/in/{position}"},
        "click": {"output": f"/actions/main/in/{click}"}}}


frame_sources = [
    button(f"{R}/trigger", "leftclick", "trigger"),
    button(f"{R}/bumper", "rightclick"),
    stick(f"{R}/thumbstick", "zoom", "button_right_stick"),
    stick(f"{L}/thumbstick", "camera", "button_left_stick"),
    button(f"{R}/a", "button_a"), button(f"{R}/b", "button_b"),
    button(f"{R}/x", "button_x"), button(f"{R}/y", "button_y"),
    button(f"{R}/menu", "button_menu"), button(f"{L}/view", "button_view"),
    button(f"{L}/dpad_up", "button_dpad_up"), button(f"{L}/dpad_down", "button_dpad_down"),
    button(f"{L}/dpad_left", "button_dpad_left"), button(f"{L}/dpad_right", "button_dpad_right"),
    button(f"{L}/trigger", "button_left_trigger", "trigger"), button(f"{L}/bumper", "button_left_bumper"),
]
frame = {
    "action_manifest_version": 0,
    "controller_type": "frame_controller",
    "name": "HW2 Frame VR default (Steam Frame)",
    "description": "Right hand = mouse: laser points, trigger = left click, bumper = right click, stick = zoom, "
                   "A/B/X/Y/Menu = hotkeys. Left hand = camera: stick orbits, D-pad pans, trigger = Shift, "
                   "bumper = Ctrl, View and stick click = hotkeys.",
    "bindings": {"/actions/main": {
        "poses": [{"output": "/actions/main/in/pointer", "path": "/user/hand/right/pose/tip"}],
        "haptics": [{"output": "/actions/main/out/haptic", "path": "/user/hand/right/output/haptic"}],
        "sources": frame_sources,
    }},
}


def write(name, data):
    (HERE / name).write_text(json.dumps(data, indent=2) + "\n", encoding="ascii", newline="\n")


write("action_manifest.json", manifest)
write("bindings_frame_controller.json", frame)
print("Wrote action_manifest.json and bindings_frame_controller.json")
