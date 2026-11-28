#!/usr/bin/env python3
"""Write town.osm.pbf and town.poly: a made-up town for the Maps tests.

    pip install osmium && python3 make-town.py

It sits where the simulated GPS walks (base/shell/src/hal/mock_hal.cpp:
51.500729 N, 0.124625 W, heading 64 degrees), so the simulator, the
screenshots in CI and the unit tests all have a map under the marker.
Nothing in it is real. Positions below are metres east and north of that
spot. The .pbf is committed, so the tests don't need this script.
"""
import math
import os

import osmium

HERE = os.path.dirname(os.path.abspath(__file__))
LAT0, LON0 = 51.500729, -0.124625
M_LAT = 111320.0
M_LON = M_LAT * math.cos(math.radians(LAT0))

nodes, ways, relations = [], [], []
roads = []   # (points, tags): joined where they cross, then written as ways


def node(x, y, tags=None):
    nodes.append((len(nodes) + 1, LON0 + x / M_LON, LAT0 + y / M_LAT, tags or {}))
    return len(nodes)


def way(points, tags=None, closed=False):
    tags = tags or {}
    if "highway" in tags:   # roads share a node where they cross, as in OpenStreetMap
        roads.append((list(points), tags))
        return None
    ids = [node(x, y) for x, y in points]
    if closed:
        ids.append(ids[0])
    ways.append((len(ways) + 1, ids, tags))
    return len(ways)


def cross(a, b, c, d):
    """Where segment ab crosses cd, or None."""
    r = (b[0] - a[0], b[1] - a[1])
    q = (d[0] - c[0], d[1] - c[1])
    den = r[0] * q[1] - r[1] * q[0]
    if abs(den) < 1e-9:
        return None
    t = ((c[0] - a[0]) * q[1] - (c[1] - a[1]) * q[0]) / den
    u = ((c[0] - a[0]) * r[1] - (c[1] - a[1]) * r[0]) / den
    if 0 <= t <= 1 and 0 <= u <= 1:
        return (round(a[0] + t * r[0], 2), round(a[1] + t * r[1], 2))
    return None


def write_roads():
    """Put a shared point into both roads wherever two cross, then make the ways."""
    motorway = lambda t: t["highway"] == "motorway"

    def join_once():
        for i, (pi, ti) in enumerate(roads):
            for j in range(i + 1, len(roads)):
                pj, tj = roads[j]
                if motorway(ti) != motorway(tj):
                    continue   # the motorway goes over the town's roads
                for k in range(len(pi) - 1):
                    for m in range(len(pj) - 1):
                        x = cross(pi[k], pi[k + 1], pj[m], pj[m + 1])
                        if not x:
                            continue
                        added = False
                        for pts, at in ((pi, k), (pj, m)):
                            if x not in pts:
                                pts.insert(at + 1, x)
                                added = True
                        if added:
                            return True   # the lists changed under the loops: look again
        return False

    while join_once():
        pass
    at = {}
    for pts, tags in roads:
        ids = []
        for x, y in pts:
            key = (round(x, 2), round(y, 2))
            if key not in at:
                at[key] = node(x, y)
            ids.append(at[key])
        ways.append((len(ways) + 1, ids, tags))


def along(bearing, a, b, step=150):
    """Points on a straight road through the origin."""
    s, c = math.sin(math.radians(bearing)), math.cos(math.radians(bearing))
    n = max(1, round((b - a) / step))
    return [((a + (b - a) * i / n) * s, (a + (b - a) * i / n) * c) for i in range(n + 1)]


# The road the simulated GPS walks along.
way(along(64, -1100, 1100), {"highway": "primary", "name": "High Street", "ref": "A 1", "maxspeed": "30 mph"})
way([(-1100, 520), (-300, 560), (400, 500), (1100, 540)], {"highway": "motorway", "ref": "M 99", "maxspeed": "70 mph"})
way([(-250, -420), (-200, -100), (-260, 250), (-220, 560)], {"highway": "secondary", "name": "Station Road", "maxspeed": "50"})
for i, name in enumerate(["Mill Lane", "Church Street", "Park Avenue", "Orchard Way"]):
    x = -80 + 150 * i
    way([(x, -350), (x + 20, 0), (x, 380)], {"highway": "residential", "name": name})
for i, name in enumerate(["North Row", "Market Street", "South Row"]):
    y = 260 - 220 * i
    way([(-240, y), (120, y + 15), (520, y)], {"highway": "tertiary" if i == 1 else "residential", "name": name})
way([(300, 60), (340, 90), (330, 130)], {"highway": "service"})
way([(310, 70), (318, 60)], {"highway": "service", "service": "driveway"})   # left out of the map
way([(-1100, 330), (-400, 300), (300, 330), (1100, 300)], {"railway": "rail"})
way([(-600, 700), (-520, 300), (-560, -100), (-500, -470)], {"waterway": "river", "name": "River Zero"})
way([(40, 60), (130, 60), (130, 160), (40, 160)], {"leisure": "park", "name": "Town Park"}, closed=True)
way([(560, 380), (800, 400), (820, 640), (600, 620)], {"natural": "wood"}, closed=True)
way([(-420, -150), (-330, -150), (-330, -80), (-420, -80)], {"natural": "beach"}, closed=True)

# A lake with an island, as a multipolygon.
outer = way([(500, -200), (760, -200), (760, 40), (500, 40)], closed=True)
inner = way([(600, -110), (660, -110), (660, -50), (600, -50)], closed=True)
relations.append((1, [("w", outer, "outer"), ("w", inner, "inner")], {"type": "multipolygon", "natural": "water"}))

# The coast, in two ways that join: land on the left, sea to the south.
c1 = [(-1300, -500), (-700, -480), (-200, -520)]
c2 = [(-200, -520), (300, -470), (900, -510), (1300, -490)]
ids1 = [node(x, y) for x, y in c1]
ids2 = [ids1[-1]] + [node(x, y) for x, y in c2[1:]]
ways.append((len(ways) + 1, ids1, {"natural": "coastline"}))
ways.append((len(ways) + 1, ids2, {"natural": "coastline"}))
way([(100, -640), (220, -640), (220, -570), (100, -570)], {"natural": "coastline"}, closed=True)   # an island

write_roads()
node(60, 200, {"place": "town", "name": "Zeroville"})

# House numbers: points, and a building (its middle is the address).
node(40, 25, {"addr:housenumber": "12", "addr:street": "High Street"})
node(60, 35, {"addr:housenumber": "12A", "addr:street": "High Street"})
node(80, 45, {"addr:housenumber": "120", "addr:street": "High Street"})
node(150, 195, {"addr:housenumber": "7", "addr:street": "Market St.", "addr:city": "Zeroville"})
way([(180, -200), (220, -200), (220, -170), (180, -170)],
    {"building": "house", "addr:housenumber": "20", "addr:street": "South Row"}, closed=True)
node(5000, 5000, {"addr:housenumber": "1", "addr:street": "High Street"})   # outside the region: left out

# Gas, food and parking for Go to ("gas", "food", "parking").
node(-150, 90, {"amenity": "fuel", "brand": "Zero Gas"})
node(-60, 30, {"amenity": "fuel"})                                    # no name: "Gas station"
node(110, 80, {"amenity": "cafe", "name": "Cafe Zero"})
node(300, 200, {"amenity": "restaurant"})                             # no name: left out
way([(330, 110), (380, 110), (380, 150), (330, 150)], {"amenity": "parking"}, closed=True)   # its middle
node(250, -100, {"amenity": "parking", "access": "private"})          # private: left out
node(-380, -260, {"place": "suburb", "name": "Old Quay"})

path = os.path.join(HERE, "town.osm.pbf")
with osmium.SimpleWriter(path, overwrite=True) as w:
    for i, lon, lat, tags in nodes:
        w.add_node(osmium.osm.mutable.Node(id=i, location=(lon, lat), tags=tags))
    for i, ids, tags in ways:
        w.add_way(osmium.osm.mutable.Way(id=i, nodes=ids, tags=tags))
    for i, members, tags in relations:
        w.add_relation(osmium.osm.mutable.Relation(id=i, members=members, tags=tags))

# The region: a box 2 km by 1.4 km. The coast and the long roads run past it.
box = [(-1000, -700), (1000, -700), (1000, 700), (-1000, 700), (-1000, -700)]
with open(os.path.join(HERE, "town.poly"), "w") as f:
    f.write("town\n1\n")
    for x, y in box:
        f.write(f"   {LON0 + x / M_LON:.7f}   {LAT0 + y / M_LAT:.7f}\n")
    f.write("END\nEND\n")
print(f"wrote {path}: {len(nodes)} nodes, {len(ways)} ways, {len(relations)} relation")
