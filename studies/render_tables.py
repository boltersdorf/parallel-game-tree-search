#!/usr/bin/env python3
"""
render each table png by compiling the generated .tex at 14 cm text width
and rasterising the page.

the caption and label are stripped first, so the png carries the table body
alone. the .tex keeps its caption.

!!! the png is rendered from the same .tex, never typed a second time, so a
number cannot differ between the two
"""

import subprocess
import tempfile
from pathlib import Path

AN = Path(__file__).resolve().parent
FIG = AN / "figures"

SOURCES = {
    "T7_ladder": "results/T7_ladder.tex",
    "T8_intent": "results/T8_intent.tex",
}

# preview with the floats option crops tightly around the table environment,
# so no pdfcrop dependency and no page margins in the png
WRAP = r"""\documentclass{article}
\usepackage[textwidth=14cm]{geometry}
\usepackage{booktabs,array}
\usepackage[active,tightpage,floats]{preview}
\begin{document}
\input{%s}
\end{document}
"""


def render(name, tex):
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        body = "\n".join(
            l for l in (AN / tex).read_text().splitlines()
            if not l.lstrip().startswith((r"\caption{", r"\label{")))
        (td / "table.tex").write_text(body + "\n")
        (td / "wrap.tex").write_text(WRAP % (td / "table.tex"))
        r = subprocess.run(
            ["pdflatex", "-interaction=nonstopmode", "wrap.tex"],
            cwd=td, capture_output=True, text=True)
        if not (td / "wrap.pdf").exists():
            raise SystemExit(f"pdflatex failed for {tex}:\n" + r.stdout[-2000:])
        over = [l for l in r.stdout.splitlines() if l.startswith("Overfull")]
        subprocess.run(
            ["gs", "-dSAFER", "-dBATCH", "-dNOPAUSE", "-sDEVICE=png16m",
             "-r300", "-dTextAlphaBits=4", "-dGraphicsAlphaBits=4",
             f"-sOutputFile={FIG / (name + '.png')}", "wrap.pdf"],
            cwd=td, check=True, capture_output=True)
    print(f"  figures/{name}.png  <- {tex}" +
          (f"   !!! {len(over)} overfull" if over else ""))


def main():
    FIG.mkdir(exist_ok=True)
    for name, tex in SOURCES.items():
        render(name, tex)


if __name__ == "__main__":
    main()
