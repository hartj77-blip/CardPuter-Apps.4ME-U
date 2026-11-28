#!/usr/bin/env python3
"""A stand-in Jellyfin server for the Jellyfin app's tests, and for trying the
app on a PC without a server.

It answers the requests the app makes, in the shapes of Jellyfin's API
description (api.jellyfin.org, 12.4): signing in, the libraries, lists of
every kind, search, one item, pictures (drawn here, sent as JPEG), favourites
and watched marks. It refuses anything without the sign-in token in the
Authorization header, and writes every request to a log the test reads.

  fake_jellyfin.py [--port N] [--port-file F] [--log F] [--old] [--slow MS]

  --port N       listen on 127.0.0.1:N (default: any free port)
  --port-file F  write the port there once listening
  --log F        one JSON line per request
  --old          behave like a server before 10.9: the lists under /UserViews,
                 /UserItems and /Items/Latest are only under /Users/<id>/...
  --slow MS      wait that long before every answer

Sign in as any user with the password "secret".
Needs Pillow (python3-pil) for the pictures.
"""
import argparse
import hashlib
import io
import json
import re
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from PIL import Image, ImageDraw

TOKEN = "0123456789abcdef0123456789abcdef"
USER_ID = "aaaaaaaabbbbccccddddeeeeeeeeeeee"
SERVER_ID = "ffffffffffffffffffffffffffffffff"


def uid(*parts):
    return hashlib.md5("/".join(parts).encode()).hexdigest()


def ticks(seconds):
    return int(seconds * 10_000_000)


class Library:
    """Every item the server has, and what the user did with each."""

    def __init__(self):
        self.items = {}      # id -> dict (a BaseItemDto, plus _private keys)
        self.order = []      # ids, oldest first
        self.lock = threading.Lock()
        self.build()

    def add(self, type_, name, parent=None, **more):
        item = {"Id": uid(type_, name, parent or ""), "Name": name, "Type": type_, "ServerId": SERVER_ID,
                "ImageTags": {}, "BackdropImageTags": [],
                "UserData": {"PlaybackPositionTicks": 0, "PlayCount": 0, "IsFavorite": False, "Played": False},
                "_parent": parent}
        images = more.pop("images", ("Primary",))
        for kind in images:
            if kind == "Backdrop":
                item["BackdropImageTags"] = [uid("tag", item["Id"], kind)]
            else:
                item["ImageTags"][kind] = uid("tag", item["Id"], kind)
        item.update(more)
        self.items[item["Id"]] = item
        self.order.append(item["Id"])
        return item

    def build(self):
        music = self.add("CollectionFolder", "Music", CollectionType="music", IsFolder=True)
        movies = self.add("CollectionFolder", "Movies", CollectionType="movies", IsFolder=True)
        shows = self.add("CollectionFolder", "Shows", CollectionType="tvshows", IsFolder=True)
        home = self.add("CollectionFolder", "Home videos", CollectionType="homevideos", IsFolder=True)
        archive = self.add("CollectionFolder", "Archive", CollectionType="movies", IsFolder=True)
        self.add("CollectionFolder", "Live TV", CollectionType="livetv", IsFolder=True)

        albums = [
            ("Bench Tapes", "Bench Tapes Vol. 2", 2023,
             ["Low Battery Blues", "Pocket Computer", "Forty-Six Keys", "Interrupt Request", "Brownout"]),
            ("Bench Tapes", "Solder & Flux", 2021, ["Cold Joint", "Rosin Core", "Helping Hands"]),
            ("Synthwave Demos", "Night Drive", 2019,
             ["Neon Exit", "Tunnel Lights", "Last Petrol Station", "Dawn on the M1"]),
            ("Les Étoiles", "Café Électrique", 2020,
             ["Déjà Vu", "Crème de la Crème", "Über den Wolken"]),
            ("The Love Letters", "Love Songs for Robots", 2018,
             ["Love Me Tinder", "Lovely Day", "Robot Love", "Loveless", "Love Is a Stranger",
              "Glove Compartment"] + [f"Love Song No. {n}" for n in range(1, 31)]),
        ]
        artists = {}
        for artist, album, year, tracks in albums:
            if artist not in artists:
                # One artist has no picture: the app draws a card for it.
                artists[artist] = self.add("MusicArtist", artist, music["Id"], IsFolder=True,
                                           images=() if artist == "Synthwave Demos" else ("Primary",))
            a = self.add("MusicAlbum", album, music["Id"], AlbumArtist=artist, Artists=[artist],
                         AlbumArtists=[{"Name": artist, "Id": artists[artist]["Id"]}],
                         ProductionYear=year, IsFolder=True, ChildCount=len(tracks), _artist=artists[artist]["Id"])
            for n, t in enumerate(tracks, 1):
                # Tracks have no picture of their own: they show their album's.
                self.add("Audio", t, a["Id"], Album=album, AlbumId=a["Id"], AlbumArtist=artist, Artists=[artist],
                         AlbumPrimaryImageTag=a["ImageTags"]["Primary"], IndexNumber=n, ParentIndexNumber=1,
                         RunTimeTicks=ticks(150 + 11 * n), MediaType="Audio", images=())
        mix = self.add("Playlist", "Soldering", None, IsFolder=True, MediaType="Audio")
        mix["_children"] = [i for i in self.order if self.items[i]["Type"] == "Audio"][:6]

        films = [("Big Buck Bunny", 2008, 596, "G", 6.4), ("Sintel", 2010, 888, "PG", 7.4),
                 ("Tears of Steel", 2012, 734, "PG-13", 5.5), ("Elephants Dream", 2006, 654, None, 5.6),
                 ("Love Actually Not", 2015, 5400, "R", 4.2), ("Cosmos Laundromat", 2015, 730, "PG", 7.0)]
        for name, year, seconds, rated, stars in films:
            m = self.add("Movie", name, movies["Id"], ProductionYear=year, RunTimeTicks=ticks(seconds),
                         CommunityRating=stars, MediaType="Video",
                         Overview=f"{name} ({year}). An open film, here to stand in for yours. "
                                  "A longer description follows so the page has something to wrap and cut.",
                         # One film has no poster, only a backdrop.
                         images=("Backdrop",) if name == "Elephants Dream" else ("Primary", "Backdrop"))
            if rated:
                m["OfficialRating"] = rated
        self.find("Sintel")["UserData"]["PlaybackPositionTicks"] = ticks(300)
        # A film with a second language and subtitles, as /Items/<id> lists them.
        sintel = self.find("Sintel")
        sintel["MediaSources"] = [{"Id": sintel["Id"], "DefaultAudioStreamIndex": 1, "MediaStreams": [
            {"Type": "Video", "Index": 0, "Codec": "h264"},
            {"Type": "Audio", "Index": 1, "Language": "eng", "DisplayTitle": "English - AAC - Stereo - Default",
             "IsDefault": True},
            {"Type": "Audio", "Index": 2, "Language": "jpn", "DisplayTitle": "Japanese - AAC - Stereo"},
            {"Type": "Subtitle", "Index": 3, "Language": "eng", "DisplayTitle": "English - SUBRIP"},
            {"Type": "Subtitle", "Index": 4, "Language": "spa", "DisplayTitle": "Spanish - SUBRIP"}]}]
        self.find("Big Buck Bunny")["UserData"]["Played"] = True
        self.find("Tears of Steel")["UserData"]["IsFavorite"] = True

        show = self.add("Series", "Bench Talk", shows["Id"], ProductionYear=2022, IsFolder=True,
                        images=("Primary", "Backdrop"))
        show["UserData"]["UnplayedItemCount"] = 5
        names = [["The Soldering Iron", "Reading Schematics", "Batteries", "Logic Analysers"],
                 ["Oscilloscopes", "Reflow at Home", "Love Your Multimeter"]]
        for s, eps in enumerate(names, 1):
            season = self.add("Season", f"Season {s}", show["Id"], SeriesName="Bench Talk", SeriesId=show["Id"],
                              IndexNumber=s, IsFolder=True, SeriesPrimaryImageTag=show["ImageTags"]["Primary"])
            season["UserData"]["UnplayedItemCount"] = 2 if s == 1 else 3
            for e, title in enumerate(eps, 1):
                ep = self.add("Episode", title, season["Id"], SeriesName="Bench Talk", SeriesId=show["Id"],
                              SeasonId=season["Id"], SeasonName=season["Name"], IndexNumber=e, ParentIndexNumber=s,
                              RunTimeTicks=ticks(1320 + 60 * e), MediaType="Video",
                              SeriesPrimaryImageTag=show["ImageTags"]["Primary"],
                              ParentBackdropItemId=show["Id"], ParentBackdropImageTags=show["BackdropImageTags"],
                              Overview=f"In which {title.lower()} get their due.")
                if s == 1 and e <= 2:
                    ep["UserData"]["Played"] = True
        self.find("Batteries")["UserData"]["PlaybackPositionTicks"] = ticks(754)
        # A show with one season: the app goes straight to its episodes.
        solo = self.add("Series", "One Season Wonder", shows["Id"], ProductionYear=2020, IsFolder=True)
        only = self.add("Season", "Season 1", solo["Id"], SeriesName=solo["Name"], SeriesId=solo["Id"],
                        IndexNumber=1, IsFolder=True)
        for e in (1, 2):
            self.add("Episode", f"Part {e}", only["Id"], SeriesName=solo["Name"], SeriesId=solo["Id"],
                     SeasonId=only["Id"], IndexNumber=e, ParentIndexNumber=1, RunTimeTicks=ticks(600),
                     MediaType="Video")

        for year in ("2024", "2025"):
            folder = self.add("Folder", year, home["Id"], IsFolder=True, images=())
            for title in (("Beach day", "First steps") if year == "2024" else ("Garden",)):
                self.add("Video", title, folder["Id"], RunTimeTicks=ticks(95), MediaType="Video")
        # A library longer than one page of a list (100 rows).
        for n in range(1, 231):
            self.add("Movie", f"Reel {n:03d}", archive["Id"], ProductionYear=1950 + n % 60,
                     RunTimeTicks=ticks(3000 + n), MediaType="Video", images=())
        # Kinds the app has no screen for: it must leave them out.
        self.add("Photo", "Holiday snap", home["Id"])
        self.add("Book", "A manual", home["Id"])

    def find(self, name):
        return next(i for i in self.items.values() if i["Name"] == name)

    def public(self, item, full=False):
        out = {k: v for k, v in item.items() if not k.startswith("_")}
        if not full:
            out.pop("Overview", None)
            out.pop("MediaSources", None)
        return out

    def descendants(self, parent_id, recursive):
        out = []
        parent = self.items.get(parent_id)
        if parent is not None and "_children" in parent:
            return [self.items[i] for i in parent["_children"]]
        for i in self.order:
            item = self.items[i]
            p = item["_parent"]
            if p == parent_id:
                out.append(item)
            elif recursive:
                while p is not None and p != parent_id:
                    p = self.items[p]["_parent"]
                if p == parent_id and p is not None:
                    out.append(item)
        return out


def clean(text):
    """Jellyfin matches a search against names with accents and punctuation taken off."""
    import unicodedata
    text = unicodedata.normalize("NFKD", text)
    return re.sub(r"[^a-z0-9 ]", "", "".join(c for c in text if not unicodedata.combining(c)).lower())


def picture(item, kind, w, h):
    """A JPEG for an item: its own two colours, a shape for its type, and a
    corner the test looks for."""
    seed = hashlib.md5((item["Id"] + kind).encode()).digest()
    top = (60 + seed[0] % 180, 40 + seed[1] % 160, 60 + seed[2] % 180)
    bottom = (seed[3] % 70, seed[4] % 70, 20 + seed[5] % 80)
    sw, sh = (640, 360) if kind in ("Backdrop", "Thumb") or item["Type"] in ("Episode", "Video", "CollectionFolder") \
        else (400, 400) if item["Type"] in ("MusicAlbum", "MusicArtist", "Audio", "Playlist") else (400, 600)
    img = Image.new("RGB", (sw, sh))
    d = ImageDraw.Draw(img)
    for y in range(sh):
        t = y / (sh - 1)
        d.line([(0, y), (sw, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    r = min(sw, sh) // 4
    if item["Type"] in ("MusicAlbum", "MusicArtist", "Playlist"):
        d.ellipse([sw // 2 - r, sh // 2 - r, sw // 2 + r, sh // 2 + r], fill=(255, 236, 170))
        d.ellipse([sw // 2 - r // 4, sh // 2 - r // 4, sw // 2 + r // 4, sh // 2 + r // 4], fill=bottom)
    elif item["Type"] in ("Movie", "Video"):
        d.polygon([(0, sh), (sw // 3, sh // 2), (sw * 2 // 3, sh * 3 // 4), (sw, sh // 3), (sw, sh)], fill=(16, 20, 30))
    else:
        for i in range(5):
            x = sw * (i + 1) // 7
            d.rectangle([x, sh // 4 + i * sh // 12, x + sw // 12, sh * 3 // 4], fill=(240, 240, 240))
    # Pure magenta in the middle of the top edge: nothing else on the screen is that colour.
    d.rectangle([sw // 2 - sw // 10, 0, sw // 2 + sw // 10, sh // 8], fill=(255, 0, 255))
    # The server scales to cover the box asked for, keeping the shape (never larger).
    if w and h:
        scale = max(w / sw, h / sh)
        if scale < 1:
            img = img.resize((max(1, round(sw * scale)), max(1, round(sh * scale))), Image.LANCZOS)
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=90)
    return buf.getvalue()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    lib = None
    opts = None
    log_lock = threading.Lock()

    def log_message(self, *args):
        pass

    def record(self, status):
        if not self.opts.log:
            return
        u = urlparse(self.path)
        auth = self.headers.get("Authorization", "")
        line = json.dumps({"method": self.command, "path": u.path, "query": u.query, "status": status,
                           "auth": bool(auth), "token": f'Token="{TOKEN}"' in auth,
                           "token_in_url": TOKEN in self.path or "api_key" in self.path.lower()})
        with self.log_lock, open(self.opts.log, "a") as f:
            f.write(line + "\n")

    def send(self, status, body=b"", ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        self.record(status)
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def signed_in(self):
        return f'Token="{TOKEN}"' in self.headers.get("Authorization", "")

    def page(self, items, q, counted=True):
        total = len(items)
        start = int(q.get("startIndex", ["0"])[0])
        limit = int(q.get("limit", [str(total or 1)])[0])
        rows = [self.lib.public(i) for i in items[start:start + limit]]
        out = {"Items": rows, "StartIndex": start,
               "TotalRecordCount": total if counted and q.get("enableTotalRecordCount", ["true"])[0] != "false" else 0}
        self.send(200, out)

    def do_POST(self):
        if self.opts.slow:
            time.sleep(self.opts.slow / 1000)
        u = urlparse(self.path)
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else b""
        if u.path == "/Users/AuthenticateByName":
            try:
                who = json.loads(body or b"{}")
            except ValueError:
                who = {}
            if who.get("Pw") != "secret" or not who.get("Username"):
                return self.send(401, {})
            return self.send(200, {"AccessToken": TOKEN, "ServerId": SERVER_ID,
                                   "User": {"Id": USER_ID, "Name": who["Username"]}})
        if not self.signed_in():
            return self.send(401, {})
        m = re.fullmatch(r"/(UserFavoriteItems|UserPlayedItems)/([0-9a-f]{32})", u.path)
        if m and m.group(2) in self.lib.items:
            with self.lib.lock:
                data = self.lib.items[m.group(2)]["UserData"]
                data["IsFavorite" if m.group(1) == "UserFavoriteItems" else "Played"] = self.command == "POST"
            return self.send(200, data)
        if u.path.startswith("/Sessions/Playing"):
            return self.send(204)
        self.send(404, {})

    do_DELETE = do_POST

    def do_GET(self):
        if self.opts.slow:
            time.sleep(self.opts.slow / 1000)
        u = urlparse(self.path)
        q = parse_qs(u.query)
        path = u.path
        lib = self.lib
        if path == "/System/Info/Public":
            return self.send(200, {"Id": SERVER_ID, "ServerName": "Stand-in", "Version": "12.4.0",
                                   "ProductName": "Jellyfin Server", "StartupWizardCompleted": True})
        if not self.signed_in():
            return self.send(401, {})

        if self.opts.old:
            # Before 10.9 these lived under the user.
            if path in ("/UserViews", "/UserItems/Resume", "/Items/Latest"):
                return self.send(404, {})
            m = re.fullmatch(rf"/Users/{USER_ID}/(Views|Items/Resume|Items/Latest)", path)
            if m:
                path = {"Views": "/UserViews", "Items/Resume": "/UserItems/Resume",
                        "Items/Latest": "/Items/Latest"}[m.group(1)]
        elif path.startswith("/Users/") and path != "/Users/Me":
            return self.send(404, {})

        if path == "/UserViews":
            views = [i for i in lib.items.values() if i["Type"] == "CollectionFolder"]
            return self.page(views, {})
        if path == "/UserItems/Resume":
            part = [i for i in lib.items.values() if i["UserData"]["PlaybackPositionTicks"] > 0]
            return self.page(part, q)
        if path == "/Shows/NextUp":
            return self.page([lib.find("Batteries")], q)
        if path == "/Items/Latest":
            parent = q.get("parentId", [""])[0]
            kinds = {"music": ("MusicAlbum",), "movies": ("Movie",), "tvshows": ("Series",)}
            ctype = lib.items.get(parent, {}).get("CollectionType", "")
            rows = [i for i in lib.descendants(parent, True) if i["Type"] in kinds.get(ctype, ("Video",))]
            limit = int(q.get("limit", ["16"])[0])
            return self.send(200, [lib.public(i) for i in reversed(rows)][:limit])   # a bare array
        m = re.fullmatch(r"/Items/([0-9a-f]{32})/Images/(Primary|Backdrop|Thumb)(?:/\d+)?", path)
        if m:
            item = lib.items.get(m.group(1))
            kind = m.group(2)
            has = item and (kind in item["ImageTags"] or (kind == "Backdrop" and item["BackdropImageTags"]))
            if not has:
                return self.send(404, {})
            if q.get("format", ["Jpg"])[0] != "Jpg":
                return self.send(400, {})
            w, h = int(q.get("fillWidth", ["0"])[0]), int(q.get("fillHeight", ["0"])[0])
            return self.send(200, picture(item, kind, w, h), "image/jpeg")
        m = re.fullmatch(r"/Items/([0-9a-f]{32})", path)
        if m:
            item = lib.items.get(m.group(1))
            return self.send(200, lib.public(item, full=True)) if item else self.send(404, {})
        m = re.fullmatch(r"/Shows/([0-9a-f]{32})/Seasons", path)
        if m:
            return self.page([i for i in lib.descendants(m.group(1), False) if i["Type"] == "Season"], q)
        m = re.fullmatch(r"/Shows/([0-9a-f]{32})/Episodes", path)
        if m:
            season = q.get("seasonId", [""])[0]
            rows = [i for i in lib.descendants(m.group(1), True)
                    if i["Type"] == "Episode" and (not season or i["SeasonId"] == season)]
            return self.page(rows, q)
        m = re.fullmatch(r"/Playlists/([0-9a-f]{32})/Items", path)
        if m and m.group(1) in lib.items:
            return self.page(lib.descendants(m.group(1), False), q)
        if path in ("/Artists", "/Artists/AlbumArtists"):
            rows = [i for i in lib.items.values() if i["Type"] == "MusicArtist"]
            term = clean(q.get("searchTerm", [""])[0])
            if term:
                rows = [i for i in rows if term in clean(i["Name"])]
            return self.page(sorted(rows, key=lambda i: i["Name"]), q)
        if path == "/Items":
            return self.items(q)
        self.send(404, {})

    do_HEAD = do_GET

    def items(self, q):
        lib = self.lib
        parent = q.get("parentId", [None])[0]
        recursive = q.get("recursive", ["false"])[0] == "true"
        if parent:
            rows = lib.descendants(parent, recursive)
        elif recursive:
            rows = [lib.items[i] for i in lib.order if lib.items[i]["Type"] != "CollectionFolder"]
        else:
            rows = [i for i in lib.items.values() if i["Type"] == "CollectionFolder"]
        types = q.get("includeItemTypes", [""])[0]
        if types:
            rows = [i for i in rows if i["Type"] in types.split(",")]
        if q.get("isFavorite", [""])[0] == "true":
            rows = [i for i in rows if i["UserData"]["IsFavorite"]]
        artist = q.get("albumArtistIds", [""])[0]
        if artist:
            rows = [i for i in rows if i.get("_artist") == artist]
        term = clean(q.get("searchTerm", [""])[0])
        if term:
            rows = [i for i in rows if term in clean(i["Name"])]
            # Best first: the name starts with it, then a word does, then anywhere.
            rows.sort(key=lambda i: (not clean(i["Name"]).startswith(term),
                                     not any(w.startswith(term) for w in clean(i["Name"]).split()), i["Name"]))
        else:
            keys = q.get("sortBy", [""])[0].split(",")
            orders = q.get("sortOrder", ["Ascending"])[0].split(",")
            for n in reversed(range(len(keys))):
                key, down = keys[n], (orders[n] if n < len(orders) else orders[-1]) == "Descending"
                if key == "SortName":
                    rows = sorted(rows, key=lambda i: i["Name"].lower(), reverse=down)
                elif key == "ProductionYear":
                    rows = sorted(rows, key=lambda i: i.get("ProductionYear", 0), reverse=down)
                elif key == "DateCreated":
                    rows = sorted(rows, key=lambda i: lib.order.index(i["Id"]), reverse=down)
                elif key == "IsFolder":
                    rows = sorted(rows, key=lambda i: bool(i.get("IsFolder")), reverse=down)
                elif key in ("ParentIndexNumber", "IndexNumber"):
                    rows = sorted(rows, key=lambda i: i.get(key, 0), reverse=down)
        self.page(rows, q)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--port-file")
    ap.add_argument("--log")
    ap.add_argument("--old", action="store_true")
    ap.add_argument("--slow", type=int, default=0)
    opts = ap.parse_args()
    Handler.lib = Library()
    Handler.opts = opts
    server = ThreadingHTTPServer(("127.0.0.1", opts.port), Handler)
    server.daemon_threads = True
    port = server.server_address[1]
    if opts.port_file:
        with open(opts.port_file, "w") as f:
            f.write(str(port))
    print(f"stand-in Jellyfin on http://127.0.0.1:{port}  (any user, password: secret)", file=sys.stderr, flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
