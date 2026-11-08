

import matplotlib

def apply():
    matplotlib.rcParams.update({
        # computer modern, the LaTeX default,
        # and matplotlib ships cmr10. cmr10 has no unicode minus, so switch it off
        "font.family": "serif",
        "font.serif": ["cmr10", "DejaVu Serif"],
        "mathtext.fontset": "cm",
        "axes.unicode_minus": False,
        "axes.formatter.use_mathtext": True,
        "font.size": 8,
        "axes.labelsize": 8,
        "xtick.labelsize": 7,
        "ytick.labelsize": 7,
        "legend.fontsize": 6.2,
        "axes.linewidth": 0.7,
        "xtick.major.width": 0.7,
        "ytick.major.width": 0.7,
        "grid.linewidth": 0.25,
        "grid.alpha": 0.4,
        "savefig.dpi": 300,
    })
