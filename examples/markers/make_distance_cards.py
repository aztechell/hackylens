"""Create two actual-size A4 cards from the checked-in tag36h11 ID 42."""

from argparse import ArgumentParser
from pathlib import Path

from PIL import Image
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.pdfgen.canvas import Canvas


def marker_modules():
    source = Path(__file__).with_name("AprilTag-36h11-42.png")
    with Image.open(source) as image:
        black = image.convert("L").point(lambda value: 255 if value < 128 else 0)
        x, y, right, bottom = black.getbbox()
        assert right - x == bottom - y and (right - x) % 8 == 0
        step = (right - x) // 8
        return [
            [black.getpixel((x + column * step + step // 2, y + row * step + step // 2))
             for column in range(8)]
            for row in range(8)
        ]


def draw_card(pdf, modules, side):
    width, height = A4
    pdf.setFont("Helvetica-Bold", 21)
    pdf.drawString(20 * mm, height - 24 * mm, f"AprilTag 42  /  {side} mm")
    pdf.setFont("Helvetica", 11)
    pdf.drawString(20 * mm, height - 34 * mm, "A4  |  Actual size (100%)  |  Black and white")
    pdf.drawString(20 * mm, height - 41 * mm, "Measure the black square. Keep the white margin intact.")

    x = (width - side * mm) / 2
    y = height * 0.50 - side * mm / 2
    module = side * mm / 8
    shape = pdf.beginPath()
    for row, line in enumerate(modules):
        start = 0
        while start < 8:
            if not line[start]:
                start += 1
                continue
            end = start + 1
            while end < 8 and line[end]:
                end += 1
            shape.rect(x + start * module, y + (7 - row) * module,
                       (end - start) * module, module)
            start = end
    pdf.drawPath(shape, stroke=0, fill=1)

    left, ruler_y = 20 * mm, 44 * mm
    pdf.setLineWidth(0.6)
    pdf.line(left, ruler_y, left + 100 * mm, ruler_y)
    pdf.setFont("Helvetica", 9)
    for tick in range(11):
        tick_x = left + tick * 10 * mm
        pdf.line(tick_x, ruler_y, tick_x, ruler_y + (4 if tick in (0, 5, 10) else 2) * mm)
        if tick in (0, 5, 10):
            pdf.drawCentredString(tick_x, ruler_y - 5 * mm, str(tick * 10))
    pdf.setFont("Helvetica", 11)
    pdf.drawString(left, 28 * mm, "Check that the outer ruler ticks are 100 mm apart.")
    pdf.drawString(left, 21 * mm, f"Enter {side} in APRILTAG > Tag mm if the square measures {side} mm.")
    pdf.showPage()


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    pdf = Canvas(str(args.output), pagesize=A4, invariant=1)
    pdf.setTitle("HackyLens AprilTag distance cards")
    pdf.setViewerPreference("PrintScaling", "None")
    modules = marker_modules()
    for side in (60, 100):
        draw_card(pdf, modules, side)
    pdf.save()


if __name__ == "__main__":
    main()
