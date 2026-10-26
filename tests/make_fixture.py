import json
import os
import sqlite3
import sys
import zipfile


def proto_varint(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def proto_key(field, wire):
    return proto_varint((field << 3) | wire)


def proto_str(field, text):
    raw = text.encode("utf-8")
    return proto_key(field, 2) + proto_varint(len(raw)) + raw


def proto_var(field, value):
    return proto_key(field, 0) + proto_varint(value)


def write_db(path, script, rows):
    if os.path.exists(path):
        os.remove(path)
    con = sqlite3.connect(path)
    con.executescript(script)
    for sql, values in rows:
        con.execute(sql, values)
    con.commit()
    con.close()


LEGACY_SQL = """
CREATE TABLE col (
  id integer primary key, crt integer, mod integer, scm integer, ver integer,
  dty integer, usn integer, ls integer, conf text, models text, decks text,
  dconf text, tags text
);
CREATE TABLE notes (
  id integer primary key, guid text, mid integer, mod integer, usn integer,
  tags text, flds text, sfld integer, csum integer, flags integer, data text
);
CREATE TABLE cards (
  id integer primary key, nid integer, did integer, ord integer, mod integer,
  usn integer, type integer, queue integer, due integer, ivl integer,
  factor integer, reps integer, lapses integer, left integer, odue integer,
  odid integer, flags integer, data text
);
"""

MODERN_SQL = """
CREATE TABLE col (id integer primary key, crt integer);
CREATE TABLE notetypes (
  id integer primary key, name text, mtime_secs integer, usn integer, config blob
);
CREATE TABLE fields (
  ntid integer, ord integer, name text, config blob
);
CREATE TABLE templates (
  ntid integer, ord integer, name text, mtime_secs integer, usn integer, config blob
);
CREATE TABLE decks (
  id integer primary key, name text, mtime_secs integer, usn integer,
  common blob, kind blob
);
CREATE TABLE notes (
  id integer primary key, guid text, mid integer, mod integer, usn integer,
  tags text, flds text, sfld integer, csum integer, flags integer, data text
);
CREATE TABLE cards (
  id integer primary key, nid integer, did integer, ord integer, mod integer,
  usn integer, type integer, queue integer, due integer, ivl integer,
  factor integer, reps integer, lapses integer, left integer, odue integer,
  odid integer, flags integer, data text
);
"""


def basic_models():
    return {
        "1500000000000": {
            "id": 1500000000000,
            "name": "Basic",
            "type": 0,
            "flds": [{"name": "Front", "ord": 0}, {"name": "Back", "ord": 1}],
            "tmpls": [{
                "name": "Card 1",
                "ord": 0,
                "qfmt": "{{Front}}",
                "afmt": "{{FrontSide}}<hr id=answer>{{Back}}",
            }],
        }
    }


def decks():
    return {"1": {"id": 1, "name": "Spanish"}}


def add_legacy_col(rows, models):
    rows.append((
        "INSERT INTO col VALUES (1,1600000000,0,0,11,0,0,0,'',?,?, '', '')",
        (json.dumps(models), json.dumps(decks())),
    ))


def add_card(rows, nid, mid, flds, did=1):
    rows.append((
        "INSERT INTO notes VALUES (?,?,?,0,0,'',?,0,0,0,'')",
        (nid, "g%d" % nid, mid, flds),
    ))
    rows.append((
        "INSERT INTO cards VALUES (?,?,?,?,0,0,0,0,0,0,2500,0,0,0,0,0,0,'')",
        (nid, nid, did, 0),
    ))


def zip_pkg(path, files):
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, payload in files.items():
            if isinstance(payload, str):
                payload = payload.encode("utf-8")
            archive.writestr(name, payload)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build/fixtures"
    os.makedirs(out, exist_ok=True)
    text_db = os.path.join(out, "text.anki2")
    media_db = os.path.join(out, "media.anki2")
    modern_db = os.path.join(out, "modern.anki21")
    stub_db = os.path.join(out, "stub.anki2")

    text_rows = []
    add_legacy_col(text_rows, basic_models())
    add_card(text_rows, 1, 1500000000000, "Hello\x1fWorld")
    write_db(text_db, LEGACY_SQL, text_rows)
    zip_pkg(os.path.join(out, "text.apkg"), {
        "collection.anki2": open(text_db, "rb").read(),
        "media": b"{}",
    })

    media_rows = []
    add_legacy_col(media_rows, basic_models())
    add_card(media_rows, 2, 1500000000000, 'See <img src="pic.png">\x1fListen [sound:a.mp3]')
    write_db(media_db, LEGACY_SQL, media_rows)
    zip_pkg(os.path.join(out, "media.apkg"), {
        "collection.anki2": open(media_db, "rb").read(),
        "media": json.dumps({"0": "pic.png", "1": "a.mp3"}).encode("utf-8"),
        "0": b"\x89PNG fake",
        "1": b"not-really-mp3",
    })

    modern_rows = [
        ("INSERT INTO col VALUES (1,1600000000)", ()),
        ("INSERT INTO notetypes VALUES (9,'Cloze',0,0,?)", (proto_var(1, 1),)),
        ("INSERT INTO fields VALUES (9,0,'Text',?)", (b"",)),
        ("INSERT INTO templates VALUES (9,0,'Cloze',0,0,?)", (
            proto_str(1, "{{cloze:Text}}") + proto_str(2, "{{cloze:Text}}"),
        )),
        ("INSERT INTO decks VALUES (3,'Geography',0,0,?,?)", (b"", b"")),
        ("INSERT INTO notes VALUES (4,'g4',9,0,0,'',?,0,0,0,'')", (
            "The {{c1::capital}} of France",
        )),
        ("INSERT INTO cards VALUES (4,4,3,0,0,0,0,0,0,0,2500,0,0,0,0,0,0,'')", ()),
    ]
    write_db(modern_db, MODERN_SQL, modern_rows)
    stub_rows = []
    add_legacy_col(stub_rows, basic_models())
    add_card(stub_rows, 1, 1500000000000, "Please update to the latest Anki version\x1f")
    write_db(stub_db, LEGACY_SQL, stub_rows)
    zip_pkg(os.path.join(out, "modern.apkg"), {
        "collection.anki2": open(stub_db, "rb").read(),
        "collection.anki21": open(modern_db, "rb").read(),
        "media": b"{}",
    })
    print("fixtures written to", out)


if __name__ == "__main__":
    main()
