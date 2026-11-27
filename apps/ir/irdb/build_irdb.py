#!/usr/bin/env python3
"""Build the IR Remote app's code database from Flipper-IRDB.

Flipper-IRDB (https://github.com/Lucaslhm/Flipper-IRDB, CC0-1.0) is a large
community collection of remote controls in the Flipper Zero ".ir" format,
filed as <Category>/<Brand>/<Model>.ir. The app reads those files as they
are; this script picks the ones it can send, and writes beside them:

  index.tsv              category, brand, model, path, buttons: one line per
                         remote, so the app can list 4,500 remotes without
                         opening them
  brands/<Kind>/<Brand>.ir
                         a brand's remote for a kind of device: every
                         distinct code its remotes use for each of 55 common
                         buttons, the most common first, each with "# src:"
                         naming a remote it came from. _all.ir is the
                         universal one: every brand's codes
  brands/index.tsv       kind, brand ("" for _all), remotes, path, buttons
  SOURCE.txt, LICENSE    where the data came from (commit) and its licence

Usage:
  build_irdb.py --out DIR [--src FLIPPER_IRDB_CHECKOUT]
  build_irdb.py --out DIR --fetch [--cache DIR]    clone the pinned commit

Its _Converted_ folder holds bulk conversions from other databases, filed
by brand only. The Pronto set (2,500 remotes converted from Philips Pronto
files, much of it AV gear: projectors, receivers, screens, video
conferencing, switchers) comes in: classify() works out the kind of each
from its brand, model and buttons. The CSV set (converted from
probonopd/irdb, whose licence asks every product that ships it to register)
and IR Plus are left out.

Every remote is written to <Kind>/<Brand>/<Model>.ir under the kind it is
listed as, so a code's "# src:" names the kind and brand the app opens.
"""
import argparse
import collections
import os
import re
import shutil
import subprocess
import sys

REPO = "https://github.com/Lucaslhm/Flipper-IRDB.git"
COMMIT = "d126fb1b6f1e114c52b4a8c19839ea65e3a9c24d"   # main, 2026-04-11

PROTOCOLS = {p.lower(): p for p in (
    "NEC", "NECext", "NEC42", "NEC42ext", "Samsung32", "RC5", "RC5X", "RC6",
    "SIRC", "SIRC15", "SIRC20", "Kaseikyo", "RCA", "Pioneer")}

SKIP_DIRS = {"_Converted_"}
CONVERTED = ["_Converted_/Pronto"]   # <Letter>/<Brand>/<Model>.ir, no kind

# Universal lists: (file name, source categories, [(button, [name aliases])]).
# Aliases are compared after norm() below.
POWER = ["power", "power_toggle", "pwr", "on_off", "onoff", "standby", "power_on_off", "power_onoff"]
VOL_UP = ["vol_up", "volume_up", "volup", "vol_plus", "volume_plus", "v_up"]
VOL_DN = ["vol_dn", "vol_down", "volume_down", "volume_dn", "voldown", "vol_minus", "volume_minus", "v_dn"]
MUTE = ["mute", "mute_toggle", "mute_unmute"]
CH_UP = ["ch_next", "ch_up", "channel_up", "ch_plus", "prog_up", "p_up", "program_up", "channel_next"]
CH_DN = ["ch_prev", "ch_dn", "ch_down", "channel_down", "ch_minus", "prog_dn", "prog_down", "p_dn",
         "program_down", "channel_prev"]
POWER_ON = ["power_on", "pwr_on", "poweron", "on", "discrete_on", "power_on_discrete"]
POWER_OFF = ["power_off", "pwr_off", "poweroff", "off", "discrete_off", "power_off_discrete"]
INPUT = ["input", "source", "input_select", "input_sel", "inputs", "src", "select_input", "source_select",
         "input_source", "input_cycle", "tv_av", "av", "tv_video", "tv_input", "input_toggle"]
HDMI_1 = ["hdmi_1", "hdmi1", "hdmi"]
HDMI_2 = ["hdmi_2", "hdmi2"]
BLANK = ["blank", "av_mute", "a_v_mute", "avmute", "pic_mute", "picture_mute", "video_mute", "shutter", "hide",
         "blank_screen", "no_show", "black_screen", "av_mute_toggle"]
FREEZE = ["freeze", "still", "freeze_frame"]
PLAY = ["play", "play_pause", "playpause", "play_and_pause"]
# Brand remotes: brands/<Kind>/<Brand>.ir, every code a brand uses for each of
# these buttons in that kind of device, most common first; _all.ir has every
# brand's. The app sweeps a button's codes once, then locks to the protocol
# and address of the one that worked, so every button after that sends one
# code. Aliases are compared after norm() below.
MAX_PER_BUTTON = 400        # universal (_all.ir)
MAX_PER_BRAND_BUTTON = 60
BRAND_BUTTONS = [
    ("Power", POWER), ("Power_on", POWER_ON), ("Power_off", POWER_OFF), ("Input", INPUT),
    ("HDMI_1", HDMI_1), ("HDMI_2", HDMI_2), ("HDMI_3", ["hdmi_3", "hdmi3"]), ("HDMI_4", ["hdmi_4", "hdmi4"]),
    ("Vol_up", VOL_UP), ("Vol_dn", VOL_DN), ("Mute", MUTE), ("Ch_next", CH_UP), ("Ch_prev", CH_DN),
    ("Up", ["up", "up_arrow", "cursor_up", "arrow_up", "nav_up"]),
    ("Down", ["down", "dn", "dn_arrow", "down_arrow", "cursor_down", "cursor_dn", "arrow_down", "nav_down"]),
    ("Left", ["left", "left_arrow", "cursor_left", "arrow_left", "nav_left"]),
    ("Right", ["right", "right_arrow", "cursor_right", "arrow_right", "nav_right"]),
    ("Ok", ["ok", "select", "enter_ok", "ok_enter", "center"]),
    ("Menu", ["menu", "setup", "settings"]), ("Back", ["back", "return", "exit", "back_exit"]),
    ("Home", ["home"]), ("Info", ["info", "display", "disp", "status"]), ("Guide", ["guide", "epg", "tv_guide"]),
    ("Blank", BLANK), ("Freeze", FREEZE), ("Aspect", ["aspect", "wide", "zoom_mode", "picture_size", "p_size"]),
    ("Picture", ["picture", "pic_mode", "picture_mode", "image", "color_mode", "colour_mode"]),
    ("Screen_up", ["screen_up", "up_screen"]), ("Screen_down", ["screen_down", "screen_dn", "down_screen"]),
    ("Play", PLAY), ("Pause", ["pause"]), ("Stop", ["stop"]), ("Rew", ["rew", "rewind", "r_srch", "rev"]),
    ("Ffwd", ["ffwd", "ff", "fast_forward", "f_srch", "fwd", "forward"]),
    ("Prev", ["prev", "previous", "skip_dn", "track_dn", "r_trk", "skip_back", "chapter_dn"]),
    ("Next", ["next", "skip_up", "track_up", "f_trk", "skip_fwd", "chapter_up"]),
    ("Eject", ["eject", "open_close", "openclose", "open_eject"]),
    ("Oscillate", ["osc", "oscillate", "oscillation", "rotate", "rotation", "swing", "swing_toggle"]),
    ("Speed", ["speed", "fan_speed", "speed_up", "fan_up", "wind_speed"]), ("Mode", ["mode", "ac_mode", "fan_mode"]),
    ("Temp_up", ["temp_up", "temperature_up", "temp_plus", "heat_up"]),
    ("Temp_dn", ["temp_dn", "temp_down", "temperature_down", "temp_minus", "heat_dn"]), ("Timer", ["timer"]),
    ("Bright_up", ["bright_up", "brightness_up", "brighter", "dim_up", "light_up"]),
    ("Bright_dn", ["bright_dn", "brightness_down", "brightness_dn", "dimmer", "dim_dn", "light_dn", "darker"]),
] + [(str(d), [str(d), "num_%d" % d, "digit_%d" % d, "key_%d" % d, "btn_%d" % d]) for d in range(1, 10)] + [
    ("0", ["0", "num_0", "digit_0", "key_0", "btn_0"]),
]


def norm(name):
    """Button name -> a key: "Vol+" -> vol_up, "VOL -" -> vol_dn, "Ch Up" -> ch_up."""
    s = name.strip().lower()
    s = re.sub(r"\s*\+$", "_up", s)
    s = re.sub(r"(?<=[a-z])\s*-$", "_dn", s)
    s = re.sub(r"[^a-z0-9]+", "_", s).strip("_")
    return s


# Brands that make one kind of thing.
BRAND_KIND = {
    "dalite": "Projector_Screens", "da_lite": "Projector_Screens", "draper": "Projector_Screens",
    "vutec": "Projector_Screens", "lift_tech": "Projector_Screens", "stewart": "Projector_Screens",
    "somfy": "Shades", "silent_gliss": "Shades", "hunter_douglas": "Shades", "elero": "Shades",
    "lutron": "Lighting_Control", "litetouch": "Lighting_Control", "vantage": "Lighting_Control",
    "x_10": "Lighting_Control",
    "crestron": "Control_Systems", "xantech": "Control_Systems", "niles": "Control_Systems",
    "elan": "Control_Systems", "mindpath": "Control_Systems", "universal_electronics": "Control_Systems",
    "polycom": "Videoconferencing", "tandberg": "Videoconferencing", "lifesize": "Videoconferencing",
    "picturetel": "Videoconferencing", "aethra": "Videoconferencing",
    "extron": "Switchers_and_Scalers", "tvone": "Switchers_and_Scalers", "zeevee": "Switchers_and_Scalers",
    "algolith": "Switchers_and_Scalers", "nview": "Switchers_and_Scalers", "multi_video_labs": "Switchers_and_Scalers",
    "real_fyre": "Fireplaces", "classic_flame": "Fireplaces",
    "dish": "Cable_Boxes", "directv": "Cable_Boxes", "echostar": "Cable_Boxes", "tivo": "Cable_Boxes",
    "scientific_atlanta": "Cable_Boxes", "general_instruments": "Cable_Boxes", "jerrold": "Cable_Boxes",
    "motorola": "Cable_Boxes", "comcast": "Cable_Boxes", "primestar": "Cable_Boxes", "replay_tv": "Cable_Boxes",
    "foxtel": "Cable_Boxes", "austar": "Cable_Boxes", "sky": "Cable_Boxes", "pace": "Cable_Boxes",
    "pace_sky_manual": "Cable_Boxes", "telus": "Cable_Boxes", "moxi": "Cable_Boxes", "hughes": "Cable_Boxes",
    "entone_cable_box": "Cable_Boxes", "on_command": "Cable_Boxes", "chaparral": "Cable_Boxes",
    "houston_tracking_systems": "Cable_Boxes", "globecast": "Cable_Boxes", "panasat": "Cable_Boxes",
    "roku": "Streaming_Devices", "apple": "Streaming_Devices",
    "christie_digital_systems": "Projectors", "digital_projection": "Projectors", "proxima": "Projectors",
    "infocus_systems": "Projectors", "boxlight": "Projectors", "dukane": "Projectors", "eiki": "Projectors",
    "eike": "Projectors", "vidikron": "Projectors", "runco": "Projectors", "ampro": "Projectors",
    "electrohome": "Projectors", "projectavision": "Projectors", "barco": "Projectors", "ask": "Projectors",
    "elmo": "Cameras", "lumens": "Cameras",
    "velodyne": "Speakers", "sunfire": "Speakers", "definitive_technology": "Speakers", "jbl": "Speakers",
    "polk_audio": "Speakers",
    "epson": "Projectors", "optoma": "Projectors", "faroudja": "Switchers_and_Scalers",
    "biamp": "Pro_Audio", "shure": "Pro_Audio", "tascam": "Pro_Audio", "fostex": "Pro_Audio",
    "bogen": "Pro_Audio", "telex": "Pro_Audio", "lencore_acoustics": "Pro_Audio",
}

# Model numbers that name the kind of device, by brand: (brand, start of the model, kind).
MODEL_PREFIX = [
    ("sony", "kv|kdl|xbr|kd|fwd", "TVs"), ("sony", "vpl", "Projectors"), ("sony", "bdp", "Blu-Ray"),
    ("sony", "dvp|rdr", "DVD_Players"), ("sony", "cdp|scd", "CD_Players"), ("sony", "str|ta|tc", "Audio_and_Video_Receivers"),
    ("sony", "slv|uvw|svo|evo|dsr", "VCR"), ("sony", "evi|brc", "Cameras"), ("sony", "pvm|bvm|lmd|pfm", "Monitors"),
    ("sony", "pcs", "Videoconferencing"), ("sony", "dss|sat", "Cable_Boxes"),
    ("panasonic", "tc|ct", "TVs"), ("panasonic", "th|tx", "Monitors"), ("panasonic", "pt", "Projectors"),
    ("panasonic", "dmp", "Blu-Ray"), ("panasonic", "dmr|dvd", "DVD_Players"), ("panasonic", "pv|ag|nv", "VCR"),
    ("panasonic", "sa|sh|su", "Audio_and_Video_Receivers"), ("panasonic", "aw", "Cameras"),
    ("jvc", "dla|lx", "Projectors"), ("jvc", "hr|sr", "VCR"), ("jvc", "av|lt", "TVs"), ("jvc", "xv", "DVD_Players"),
    ("jvc", "rx", "Audio_and_Video_Receivers"),
    ("sharp", "xg|xv|pg|xr", "Projectors"), ("sharp", "lc", "TVs"), ("sharp", "vc", "VCR"),
    ("nec", "mt|vt|lt|gt|np|px", "Projectors"), ("nec", "pd|plasma|multisync", "Monitors"),
    ("hitachi", "cp|pj|ed|tx", "Projectors"), ("sanyo", "plc|plv|pdg|lp", "Projectors"), ("toshiba", "tlp", "Projectors"),
    ("marantz", "vp", "Projectors"), ("marantz", "sr|av|pm|recv|na|nr", "Audio_and_Video_Receivers"),
    ("marantz", "cc|cd|sa", "CD_Players"), ("marantz", "dv", "DVD_Players"), ("marantz", "pmd", "Pro_Audio"),
    ("denon", "avr|avc|avp|dra|pma|tu", "Audio_and_Video_Receivers"), ("denon", "dvd|dvm", "DVD_Players"),
    ("denon", "dcm|dcd|dn", "CD_Players"), ("dbd", "dbd", "Blu-Ray"),
    ("pioneer", "cld|ld", "Laserdisc"), ("pioneer", "pdp|pro", "Monitors"), ("pioneer", "dv", "DVD_Players"),
    ("pioneer", "bdp", "Blu-Ray"), ("pioneer", "vsx|vsa|sc|sx", "Audio_and_Video_Receivers"), ("pioneer", "kuro", "Monitors"),
    ("yamaha", "rx|dsp|htr|cx|mx|ax", "Audio_and_Video_Receivers"), ("yamaha", "dvd|dv", "DVD_Players"),
    ("onkyo", "tx|dtr|pr|integra", "Audio_and_Video_Receivers"), ("onkyo", "dv", "DVD_Players"),
    ("integra", "dtr|dtc|dhc", "Audio_and_Video_Receivers"), ("samsung", "bd", "Blu-Ray"),
    ("samsung", "ln|un|pn|hl|hp|tx|cl", "TVs"), ("lg", "bd|bp|bh", "Blu-Ray"), ("oppo", "bdp|udp", "Blu-Ray"),
    ("mitsubishi", "hc|xd|lvp|xl", "Projectors"), ("mitsubishi", "hs", "VCR"), ("mitsubishi", "vs|ws|cs|wd|lt", "TVs"),
    ("3m", "mp|x|s", "Projectors"), ("philips", "pd", "Monitors"),
]

# Words in the model name (after splitting on non-letters).
MODEL_WORDS = [
    (r"screen|lift", "Projector_Screens"),
    (r"proj|projector|powerlite|lcd_?proj|dlp|vpl|lamp", "Projectors"),
    (r"blu_?ray|bdp|bd|bluray", "Blu-Ray"),
    (r"dvd|dvdp", "DVD_Players"),
    (r"vcr|vhs|svhs|betamax|video_?cassette", "VCR"),
    (r"laser_?disc|ld_?player|cld", "Laserdisc"),
    (r"cd_?player|cdp|cd_?changer|^cd$", "CD_Players"),
    (r"rec(eiver|v)?|avr|amp|amplifier|preamp|pre_amp|processor|tuner|surround|stereo|tape|cassette|deck",
     "Audio_and_Video_Receivers"),
    (r"sub(woofer)?|speaker", "Speakers"),
    (r"sat(ellite)?|cable|dss|dvr|pvr|stb|set_?top|tivo|dish|converter", "Cable_Boxes"),
    (r"camera|cam|visualizer|document", "Cameras"),
    (r"switch(er)?|matrix|scaler|router|selector|splitter|extender|line_?doubler|doubler|processor", "Switchers_and_Scalers"),
    (r"codec|videoconf.*|conference", "Videoconferencing"),
    (r"monitor|plasma|pdp|display|lcd|pvm|bvm", "Monitors"),
    (r"[a-z]*tv|television|crt|kdl|kd|kv", "TVs"),
]

# Buttons that point at a kind of device: (pattern on norm(name), kind, weight).
BUTTON_HINTS = [
    (r"keystone|key_?stone|lamp|lens|av_?mute|a_v_mute|blank|shutter|focus|foc_(up|dn)|zm_(up|dn)|"
     r"lens_shift|rgb_?\d?|computer_?\d?|auto_(adj|sync|pc)|pc", "Projectors", 2),
    (r"screen_(up|down|dn|stop)|up_screen|down_screen", "Projector_Screens", 5),
    (r"eject|open_close|subtitle|angle|top_menu|popup_menu|chapter.*|title|disc_?\d?|a_b|rpt_a_b|"
     r"f_step|r_step|slow.*", "DVD_Players", 1),
    (r"tv_vcr|sp_ep|tracking.*|counter.*|vcr_?\d?|rec|record|tape", "VCR", 1),
    (r"tuner|phono|fm|am|am_fm|band|preset.*|surround|pro_logic|dsp|test_tone|thx|bass.*|treble.*|"
     r"center.*|rear.*|sub.*|cd|aux\d?|tape_?\d?|bypass|dolby|dts|stereo|mono|loudness", "Audio_and_Video_Receivers", 1),
    (r"guide|dvr|list|fav(orite)?|sat|live_?tv|ppv|day_(up|dn)|page_(up|dn|down)", "Cable_Boxes", 1),
    (r"ch_(up|dn)|channel.*|pip.*|mts|cc|sleep|tv_video|aspect|antenna|ant|tv", "TVs", 1),
    (r"hang_?up|far_?end|near_?end|far_?cam.*|near_?cam.*|end_call", "Videoconferencing", 3),
]


def classify(brand, model, names):
    """Kind of device for a Pronto remote: its brand, model and button names."""
    b = norm(brand)
    if b in BRAND_KIND:
        return BRAND_KIND[b]
    w = norm(model)
    for pb, pre, kind in MODEL_PREFIX:
        if pb == b and re.match(r"(%s)(?![a-z])" % pre if len(pre) <= 2 else r"(%s)" % pre, w):
            return kind
    toks = set()
    for t in re.split(r"[^A-Za-z0-9]+|(?<=[a-z])(?=[A-Z])", model):
        t = t.lower()
        if t:
            toks.add(t)
            toks.add(re.match(r"[a-z]*", t).group(0))
    toks.discard("")
    for pat, kind in MODEL_WORDS:
        if any(re.fullmatch(pat, t) for t in toks):
            return kind
    score = {}
    for n in names:
        for pat, kind, w in BUTTON_HINTS:
            if re.fullmatch(pat, n):
                score[kind] = score.get(kind, 0) + w
    if not score:
        return "Miscellaneous"
    best = max(score, key=score.get)
    if best == "DVD_Players" and score.get("Audio_and_Video_Receivers", 0) > score[best]:
        best = "Audio_and_Video_Receivers"
    return best


def refile(cat, brand, model):
    """Kind for a remote of the main set: screens and HDMI switches filed with
    projectors or under Miscellaneous get kinds of their own."""
    w = norm(brand + " " + model)
    if cat in ("Projectors", "Miscellaneous") and re.search(r"(^|_)(screen|screens|lift)(_|$)|screen", w) \
            and not re.search(r"touch", w):
        return "Projector_Screens"
    if cat in ("Miscellaneous", "Multimedia") and (norm(brand) in BRAND_KIND and
                                                     BRAND_KIND[norm(brand)] == "Switchers_and_Scalers"
                                                     or re.search(r"hdmi|switch|matrix|splitter|scaler", w)):
        return "Switchers_and_Scalers"
    if cat == "Miscellaneous" and norm(brand) in BRAND_KIND:
        return BRAND_KIND[norm(brand)]
    return cat


def decode(s):
    """A raw capture as the protocol it is, when it is a clean NEC, Samsung32 or
    SIRC frame (as ZeroShell's from_capture() reads learned codes), else None.
    Pronto files hold every code raw: this lets them merge with the same codes
    elsewhere in the universal lists, and sends them as clean frames."""
    if s["type"] != "raw":
        return None
    t = []
    for i, v in enumerate(s["data"]):
        if i % 2 == 1 and v > 20000:
            break
        t.append(v)
    if len(t) % 2 == 0 and t:
        t.pop()

    def near(v, ref):
        return ref * 0.7 <= v <= ref * 1.3

    def parsed(protocol, address, command):
        return {"name": s["name"], "type": "parsed", "protocol": protocol, "address": address, "command": command}

    if len(t) == 67 and (near(t[0], 9000) or near(t[0], 4500)) and near(t[1], 4500):
        v = 0
        for i in range(32):
            m, sp = t[2 + 2 * i], t[3 + 2 * i]
            if not near(m, 560):
                return None
            if near(sp, 1690):
                v |= 1 << i
            elif not near(sp, 560):
                return None
        b = [(v >> (8 * i)) & 0xFF for i in range(4)]
        if near(t[0], 4500):
            return parsed("Samsung32", b[0], b[2]) if b[0] == b[1] and b[2] ^ 0xFF == b[3] else None
        if b[0] ^ 0xFF == b[1] and b[2] ^ 0xFF == b[3]:
            return parsed("NEC", b[0], b[2])
        return parsed("NECext", v & 0xFFFF, v >> 16)
    n = (len(t) - 1) // 2
    if len(t) % 2 == 1 and n in (12, 15, 20) and near(t[0], 2400) and near(t[1], 600):
        v = 0
        for i in range(n):
            if near(t[2 + 2 * i], 1200):
                v |= 1 << i
            elif not near(t[2 + 2 * i], 600):
                return None
            if i + 1 < n and not near(t[3 + 2 * i], 600):
                return None
        proto, amask = {12: ("SIRC", 0x1F), 15: ("SIRC15", 0xFF), 20: ("SIRC20", 0x1FFF)}[n]
        return parsed(proto, (v >> 7) & amask, v & 0x7F)
    return None


def parse(text):
    """Signals from a .ir file's text, as dicts; None if it isn't one."""
    sigs, cur, ok = [], None, True

    def flush():
        if cur is None or not ok or not cur.get("name"):
            return
        if cur.get("type") == "raw":
            data = cur.get("data", [])
            if len(data) % 2 == 0 and data:
                data = data[:-1]
            if not data or not 10000 <= cur.get("frequency", 38000) <= 100000:
                return
            cur["data"] = data
        elif cur.get("type") == "parsed":
            p = PROTOCOLS.get(cur.get("protocol", "").lower())
            if not p or "address" not in cur or "command" not in cur:
                return
            cur["protocol"] = p
        else:
            return
        sigs.append(cur)

    is_ir = False
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        key, val = line.split(":", 1)
        key, val = key.strip().lower(), val.strip()
        if key == "filetype":
            is_ir = val.lower().startswith("ir ")
        elif key == "name":
            flush()
            cur, ok = {"name": val}, True
        elif cur is None:
            continue
        elif key == "type":
            cur["type"] = val.lower()
        elif key == "protocol":
            cur["protocol"] = val
        elif key in ("address", "command"):
            try:
                b = [int(x, 16) for x in val.split()]
                if not 1 <= len(b) <= 4 or any(x > 255 for x in b):
                    raise ValueError
                cur[key] = sum(x << (8 * i) for i, x in enumerate(b))
            except ValueError:
                ok = False
        elif key == "frequency":
            try:
                cur["frequency"] = int(float(val))
            except ValueError:
                ok = False
        elif key == "duty_cycle":
            cur["duty_cycle"] = val
        elif key == "data":
            try:
                vals = [int(x) for x in val.split()]
                if any(v <= 0 or v > 1000000 for v in vals):
                    raise ValueError
                cur.setdefault("data", []).extend(vals)
            except ValueError:
                ok = False
    flush()
    return sigs if (is_ir or sigs) else None


def code_key(s):
    """Identity of a code, for removing duplicates across remotes."""
    if s["type"] == "parsed":
        return ("p", s["protocol"], s["address"], s["command"])
    # Raw captures of the same button differ slightly: round to 10%.
    return ("r", round(s.get("frequency", 38000), -3),
            tuple(int(round(v / max(50, v // 10))) for v in s["data"]))


def signal_text(s, name, src):
    out = ["#", "# src: " + src, "name: " + name]
    if s["type"] == "parsed":
        def b(v):
            return " ".join("%02X" % ((v >> (8 * i)) & 0xFF) for i in range(4))
        out += ["type: parsed", "protocol: " + s["protocol"], "address: " + b(s["address"]),
                "command: " + b(s["command"])]
    else:
        out += ["type: raw", "frequency: %d" % s.get("frequency", 38000),
                "duty_cycle: " + s.get("duty_cycle", "0.330000"), "data: " + " ".join(map(str, s["data"]))]
    return "\n".join(out)


def pretty_model(brand, stem, series):
    m = stem
    if m.lower().startswith(brand.lower()):
        m = m[len(brand):].lstrip("_- ") or stem
    m = m.replace("_", " ").strip()
    return " / ".join(series + [m]) if series else m


def fetch(cache):
    if not os.path.isdir(os.path.join(cache, ".git")):
        os.makedirs(cache, exist_ok=True)
        subprocess.run(["git", "init", "-q", cache], check=True)
        subprocess.run(["git", "-C", cache, "remote", "add", "origin", REPO], check=True)
    head = subprocess.run(["git", "-C", cache, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if head != COMMIT:
        subprocess.run(["git", "-C", cache, "fetch", "-q", "--depth", "1", "origin", COMMIT], check=True)
        subprocess.run(["git", "-C", cache, "checkout", "-q", "--force", COMMIT], check=True)
    return cache


def brand_key(brand):
    return re.sub(r"[^a-z0-9]", "", brand.lower())


# Pronto brand folders spelled another way than the main set's.
BRAND_ALIASES = {"infocussystems": "InFocus", "christiedigitalsystems": "Christie", "generic": "Generic",
                 "nomanufacturer": "Generic", "dalite": "Da-Lite"}


def read_ir(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        return parse(fh.read())


def build(src, out, commit=None):
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(out)
    # (kind, brand folder, series, file name, source path, signals)
    remotes = []
    skipped = 0
    for cat in sorted(os.listdir(src)):
        cdir = os.path.join(src, cat)
        if cat.startswith(".") or cat in SKIP_DIRS or not os.path.isdir(cdir):
            continue
        for root, dirs, files in os.walk(cdir):
            dirs.sort()
            for f in sorted(files):
                if not f.lower().endswith(".ir"):
                    continue
                path = os.path.join(root, f)
                rel = os.path.relpath(path, src)
                if "\t" in rel or "\n" in rel:
                    continue
                sigs = read_ir(path)
                if not sigs:
                    skipped += 1
                    continue
                parts = rel.split(os.sep)
                stem = os.path.splitext(parts[-1])[0]
                if len(parts) >= 3:
                    bdir, series = parts[1], parts[2:-1]
                else:
                    bdir, series = stem.split("_")[0], []
                kind = refile(cat, bdir, stem)
                remotes.append((kind, bdir, series, f, path, sigs))

    # The converted sets: spell brands as the main set does, and work out the kind.
    spelling = {}
    for r in remotes:
        spelling.setdefault(brand_key(r[1]), r[1])
    from_converted = 0
    for conv in CONVERTED:
        cdir = os.path.join(src, *conv.split("/"))
        if not os.path.isdir(cdir):
            continue
        for letter in sorted(os.listdir(cdir)):
            ldir = os.path.join(cdir, letter)
            if not os.path.isdir(ldir):
                continue
            for bdir in sorted(os.listdir(ldir)):
                bpath = os.path.join(ldir, bdir)
                if not os.path.isdir(bpath) or "\t" in bdir or "\n" in bdir:
                    continue
                k = brand_key(bdir)
                brand = BRAND_ALIASES.get(k) or spelling.get(k) or bdir.strip("()")
                for f in sorted(os.listdir(bpath)):
                    path = os.path.join(bpath, f)
                    if not f.lower().endswith(".ir") or "\t" in f or "\n" in f or not os.path.isfile(path):
                        continue
                    sigs = read_ir(path)
                    if not sigs:
                        skipped += 1
                        continue
                    stem = os.path.splitext(f)[0]
                    kind = classify(brand, stem, {norm(s["name"]) for s in sigs})
                    remotes.append((kind, brand, [], f, path, sigs))
                    from_converted += 1

    index = []
    by_cat = collections.defaultdict(list)   # category -> [(relpath, signals)]
    brand_of = {}
    taken = set()
    for kind, bdir, series, f, path, sigs in remotes:
        rel = "/".join([kind, bdir] + series + [f])
        stem, ext = os.path.splitext(f)
        n = 2
        while rel.lower() in taken:   # a converted remote of the same name as one already there
            rel = "/".join([kind, bdir] + series + ["%s_%d%s" % (stem, n, ext)])
            n += 1
        taken.add(rel.lower())
        dest = os.path.join(out, *rel.split("/"))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copyfile(path, dest)
        brand = bdir.replace("_", " ").strip()
        index.append((kind, brand, pretty_model(brand, stem, series), rel, len(sigs)))
        brand_of[rel] = brand
        by_cat[kind].append((rel, sigs))

    index.sort(key=lambda r: (r[0].lower(), r[1].lower(), r[2].lower()))
    with open(os.path.join(out, "index.tsv"), "w", encoding="utf-8") as fh:
        fh.write("# category\tbrand\tmodel\tpath\tbuttons\n")
        for row in index:
            fh.write("\t".join(map(str, row)) + "\n")

    brand_remotes = write_brand_remotes(out, by_cat, brand_of)

    lic = os.path.join(src, "LICENSE")
    if os.path.exists(lic):
        shutil.copyfile(lic, os.path.join(out, "LICENSE"))
    with open(os.path.join(out, "SOURCE.txt"), "w", encoding="utf-8") as fh:
        fh.write("IR codes from Flipper-IRDB, %s\ncommit: %s\n" % (REPO, commit or "local copy"))
        fh.write("Licence: CC0-1.0 (see LICENSE and the project's README for its scope).\n")
        fh.write("Built by apps/ir/irdb/build_irdb.py: %d remotes (%d from %s), %d brand remotes, "
                 "%d universal\n" % (len(index), from_converted, ", ".join(CONVERTED), *brand_remotes))
    print("irdb: %d remotes (%d converted, %d skipped), %d brand remotes, %d universal -> %s"
          % (len(index), from_converted, skipped, *brand_remotes, out))


def merged_remote(remotes, brand_of, alias, limit):
    """[(button, [(signal, credited remote)])] for a set of remotes: each
    distinct code once, the most used first, credited to a remote of the
    brand that uses it most (so the app can name the likely maker)."""
    # button -> code key -> [signal, remotes using it, {brand: [remotes, first remote]}]
    seen = collections.defaultdict(collections.OrderedDict)
    for rel, sigs in remotes:
        in_file = set()
        for s in sigs:
            button = alias.get(norm(s["name"]))
            if not button:
                continue
            s = decode(s) or s
            k = code_key(s)
            if (button, k) in in_file:
                continue
            in_file.add((button, k))
            e = seen[button].setdefault(k, [s, 0, {}])
            e[1] += 1
            e[2].setdefault(brand_of[rel].lower(), [0, rel])[0] += 1
    out = []
    for button, _ in BRAND_BUTTONS:
        if button in seen:
            ranked = sorted(seen[button].values(), key=lambda e: -e[1])[:limit]
            out.append((button, [(s, max(b.values(), key=lambda v: v[0])[1]) for s, _, b in ranked]))
    return out


def write_brand_remotes(out, by_cat, brand_of):
    """brands/<Kind>/<Brand>.ir for each brand with two or more remotes of a
    kind, brands/<Kind>/_all.ir with every brand's codes (the universal
    remote), and brands/index.tsv (kind, brand or "", remotes, path,
    buttons). A brand with one remote has none: the app opens that remote."""
    alias = {}
    for button, names in BRAND_BUTTONS:
        for n in names:
            alias.setdefault(n, button)
    rows = []
    universal = 0
    for cat in sorted(by_cat):
        brands = collections.defaultdict(list)
        for rel, sigs in by_cat[cat]:
            brands[brand_of[rel]].append((rel, sigs))
        sets = [("", by_cat[cat], MAX_PER_BUTTON)] + [
            (brand, remotes, MAX_PER_BRAND_BUTTON)
            for brand, remotes in sorted(brands.items(), key=lambda b: b[0].lower())]
        for brand, remotes, limit in sets:
            if len(remotes) < 2:
                continue
            buttons = merged_remote(remotes, brand_of, alias, limit)
            if len(buttons) < 2:
                continue
            fname = "_all" if not brand else re.sub(r"[^A-Za-z0-9._ -]+", "_", brand).strip(" ._") or "Brand"
            rel = "%s/%s.ir" % (cat, fname)
            dest = os.path.join(out, "brands", cat, fname + ".ir")
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "w", encoding="utf-8") as fh:
                fh.write("Filetype: IR library file\nVersion: 1\n# ZeroOS %s remote: %s, from %d remotes\n"
                         % ("brand" if brand else "universal", (brand + " " if brand else "") + cat, len(remotes)))
                fh.write("\n".join(signal_text(sig, button, src) for button, codes in buttons
                                    for sig, src in codes) + "\n")
            rows.append((cat, brand, len(remotes), rel, len(buttons)))
            universal += not brand
    os.makedirs(os.path.join(out, "brands"), exist_ok=True)
    with open(os.path.join(out, "brands", "index.tsv"), "w", encoding="utf-8") as fh:
        fh.write("# kind\tbrand\tremotes\tpath\tbuttons\n")
        for row in rows:
            fh.write("\t".join(map(str, row)) + "\n")
    return len(rows) - universal, universal


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--src", help="a Flipper-IRDB checkout (or any folder laid out the same way)")
    ap.add_argument("--fetch", action="store_true", help="clone the pinned Flipper-IRDB commit first")
    ap.add_argument("--cache", default=os.path.join("build", "irdb-src"), help="where --fetch clones to")
    a = ap.parse_args()
    if a.fetch:
        src, commit = fetch(a.cache), COMMIT
    elif a.src:
        src, commit = a.src, None
    else:
        ap.error("give --src or --fetch")
    if not os.path.isdir(src):
        sys.exit("error: %s is not a folder" % src)
    build(src, a.out, commit)


if __name__ == "__main__":
    main()
