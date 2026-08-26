"""Display a pie chart of the enclosure opening areas."""

import base64
from io import BytesIO
from pathlib import Path
import tempfile
import webbrowser

import matplotlib
import matplotlib.pyplot as plt
from matplotlib.widgets import Button


INTAKE_AREA_MM2 = 880
BUCK_OUTLET_AREA_MM2 = 60
MOSFET_OUTLET_AREA_MM2 = 387
INDUCTOR_OUTLET_AREA_MM2 = 75
USELESS_OUTLET_AREA_MM2 = 225

FIGURE_WIDTH_INCHES = 6
FIGURE_HEIGHT_INCHES = 4
FIGURE_DPI = 100


def save_chart(_event) -> None:
    """Open Matplotlib's normal save dialog from the on-chart button."""
    toolbar = getattr(figure.canvas.manager, "toolbar", None)
    if toolbar is not None and hasattr(toolbar, "save_figure"):
        save_button_axes.set_visible(False)
        try:
            toolbar.save_figure()
        finally:
            save_button_axes.set_visible(True)
            figure.canvas.draw_idle()
    else:
        figure.savefig("airflow_pie_chart.png", dpi=FIGURE_DPI)


def show_in_browser() -> None:
    """Show the chart and a save control without requiring a GUI backend."""
    image = BytesIO()
    figure.savefig(image, format="png", dpi=FIGURE_DPI)
    encoded_image = base64.b64encode(image.getvalue()).decode("ascii")
    image_uri = f"data:image/png;base64,{encoded_image}"
    html = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Airflow Napkin Math</title>
<style>
html, body {{ margin: 0; background: #f0f0f0; font-family: sans-serif; }}
main {{ width: 600px; margin: 20px auto; }}
img {{ display: block; width: 600px; height: 400px; background: white; }}
.controls {{ margin-top: 10px; text-align: right; }}
button {{ padding: 6px 14px; cursor: pointer; }}
</style>
</head>
<body>
<main>
<img src="{image_uri}" width="600" height="400" alt="Airflow opening areas pie chart">
<div class="controls">
<a href="{image_uri}" download="airflow_pie_chart.png"><button type="button">Save PNG...</button></a>
</div>
</main>
</body>
</html>
"""
    output_path = Path(tempfile.gettempdir()) / "hot_wand_airflow_pie_chart.html"
    output_path.write_text(html, encoding="utf-8")
    if not webbrowser.open(output_path.as_uri()):
        print(f"Open this file in a browser: {output_path}")


labels = [
    "Intake",
    "Buck converter outlets",
    "MOSFET outlet",
    "Inductor outlets",
    "Useless outlets",
]
areas_mm2 = [
    INTAKE_AREA_MM2,
    BUCK_OUTLET_AREA_MM2,
    MOSFET_OUTLET_AREA_MM2,
    INDUCTOR_OUTLET_AREA_MM2,
    USELESS_OUTLET_AREA_MM2,
]
legend_labels = [f"{label}: {area} mm^2" for label, area in zip(labels, areas_mm2)]

figure, axes = plt.subplots(
    figsize=(FIGURE_WIDTH_INCHES, FIGURE_HEIGHT_INCHES),
    dpi=FIGURE_DPI,
)
figure.canvas.manager.set_window_title("Airflow Napkin Math")
figure.subplots_adjust(left=0.04, right=0.67, bottom=0.15, top=0.88)

wedges, _, _ = axes.pie(
    areas_mm2,
    autopct="%1.1f%%",
    pctdistance=0.72,
    startangle=90,
    counterclock=False,
    wedgeprops={"edgecolor": "white", "linewidth": 1},
)
axes.set_title("Airflow Opening Areas")
axes.axis("equal")

figure.legend(
    wedges,
    legend_labels,
    loc="center right",
    bbox_to_anchor=(0.99, 0.56),
    frameon=False,
    fontsize=8,
)

if matplotlib.get_backend().lower() == "agg":
    show_in_browser()
else:
    save_button_axes = figure.add_axes((0.78, 0.06, 0.16, 0.08))
    save_button = Button(save_button_axes, "Save...")
    save_button.on_clicked(save_chart)
    plt.show()
