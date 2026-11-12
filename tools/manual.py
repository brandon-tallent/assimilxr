"""Builds docs/manual.html from manual/body.html, with the screenshots in
manual/screens inlined so the guide is one file."""
import base64
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
MANUAL = ROOT / "manual"
HEAD = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="description" content="User guide for AssimiLXR, unofficial firmware that runs the Erica Synths LXR-02 sound engine on the Rossum Electro-Music Assimil8or.">
<meta name="theme-color" content="#fafaf8">
<title>AssimiLXR: User Guide</title>
"""


def inline(match):
    data = (MANUAL / match.group(1)).read_bytes()
    return 'src="data:image/png;base64,' + base64.b64encode(data).decode() + '"'


def main():
    body = re.sub(r'src="(screens/[a-z0-9_-]+\.png)"', inline, (MANUAL / "body.html").read_text())
    style = (MANUAL / "style.html").read_text()
    out = ROOT / "docs" / "manual.html"
    out.write_text(HEAD + style + "\n</head>\n<body>\n" + body + "</body>\n</html>\n")
    print(out, out.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
