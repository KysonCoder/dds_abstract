#!/usr/bin/env python3
"""Generate the dds_abstract UML diagram booklet as DOCX and PDF."""

from __future__ import annotations

import os
import base64
import html
import argparse
from pathlib import Path
from typing import Iterable

import fitz
from PIL import Image, ImageDraw, ImageFont
from docx import Document
from docx.enum.section import WD_ORIENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Inches, Pt, RGBColor
from reportlab.lib.pagesizes import landscape, letter
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.cidfonts import UnicodeCIDFont
from reportlab.platypus import Image as PdfImage
from reportlab.platypus import PageBreak, Paragraph, SimpleDocTemplate, Spacer


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs"
ASSETS = ROOT / "tmp" / "uml_assets"
QA = ROOT / "tmp" / "pdfs" / "dds_abstract_uml_qa"

NAVY = "#17365D"
BLUE = "#2E75B6"
LIGHT_BLUE = "#DDEBF7"
PALE_BLUE = "#EFF6FC"
GREEN = "#548235"
LIGHT_GREEN = "#E2F0D9"
ORANGE = "#C55A11"
LIGHT_ORANGE = "#FCE4D6"
GRAY = "#5B6573"
LIGHT_GRAY = "#F2F4F7"
DARK = "#202B38"
WHITE = "#FFFFFF"
RED = "#C00000"


def find_font() -> str:
    candidates = [
        "/mnt/c/Windows/Fonts/msyh.ttc",
        "/mnt/c/Windows/Fonts/simhei.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ]
    for candidate in candidates:
        if Path(candidate).exists():
            return candidate
    raise RuntimeError("No suitable font found")


FONT_PATH = find_font()


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    bold_candidates = [
        "/mnt/c/Windows/Fonts/msyhbd.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    ]
    bold_path = next((item for item in bold_candidates if Path(item).exists()), FONT_PATH)
    path = bold_path if bold else FONT_PATH
    return ImageFont.truetype(path, size=size)


def canvas(title: str, subtitle: str = "") -> tuple[Image.Image, ImageDraw.ImageDraw]:
    image = Image.new("RGB", (1800, 1050), WHITE)
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, 1800, 88), fill=NAVY)
    draw.text((48, 18), title, font=font(38, True), fill=WHITE)
    if subtitle:
        draw.text((1750, 31), subtitle, font=font(21), fill="#D9EAF7", anchor="ra")
    draw.line((42, 1010, 1758, 1010), fill="#CCD5DF", width=2)
    draw.text((48, 1018), "dds_abstract 1.0.0", font=font(17), fill=GRAY)
    return image, draw


def wrapped(draw: ImageDraw.ImageDraw, text: str, box: tuple[int, int, int, int],
            size: int = 24, color: str = DARK, bold: bool = False,
            align: str = "center") -> None:
    x1, y1, x2, y2 = box
    fnt = font(size, bold)
    lines: list[str] = []
    for paragraph in text.split("\n"):
        current = ""
        for character in paragraph:
            trial = current + character
            if draw.textbbox((0, 0), trial, font=fnt)[2] <= x2 - x1 - 24:
                current = trial
            else:
                if current:
                    lines.append(current)
                current = character
        lines.append(current)
    line_height = size + 9
    top = (y1 + y2 - line_height * len(lines)) // 2
    for index, line in enumerate(lines):
        if align == "left":
            position = (x1 + 14, top + index * line_height)
            anchor = "la"
        else:
            position = ((x1 + x2) // 2, top + index * line_height)
            anchor = "ma"
        draw.text(position, line, font=fnt, fill=color, anchor=anchor)


def box(draw: ImageDraw.ImageDraw, bounds: tuple[int, int, int, int], text: str,
        fill: str = LIGHT_BLUE, outline: str = BLUE, size: int = 24,
        radius: int = 18, bold: bool = False) -> None:
    draw.rounded_rectangle(bounds, radius=radius, fill=fill, outline=outline, width=3)
    wrapped(draw, text, bounds, size=size, bold=bold)


def arrow(draw: ImageDraw.ImageDraw, start: tuple[int, int], end: tuple[int, int],
          color: str = GRAY, width: int = 4, dashed: bool = False) -> None:
    if dashed:
        x1, y1 = start
        x2, y2 = end
        for step in range(0, 100, 10):
            a = step / 100
            b = min(step + 6, 100) / 100
            draw.line((x1 + (x2-x1)*a, y1 + (y2-y1)*a,
                       x1 + (x2-x1)*b, y1 + (y2-y1)*b), fill=color, width=width)
    else:
        draw.line((*start, *end), fill=color, width=width)
    import math
    angle = math.atan2(end[1] - start[1], end[0] - start[0])
    length = 18
    for delta in (2.55, -2.55):
        point = (end[0] + length * math.cos(angle + delta),
                 end[1] + length * math.sin(angle + delta))
        draw.line((*end, *point), fill=color, width=width)


def label(draw: ImageDraw.ImageDraw, position: tuple[int, int], text: str,
          size: int = 20, color: str = GRAY, anchor: str = "mm") -> None:
    draw.text(position, text, font=font(size), fill=color, anchor=anchor)


def save(image: Image.Image, filename: str) -> Path:
    path = ASSETS / filename
    image.save(path, "PNG", optimize=True)
    return path


def architecture_diagram() -> Path:
    image, draw = canvas("总体架构图", "协议无关的双通道通信抽象")
    layers = [
        (130, "应用层", [(390, "C++ Application"), (1050, "Python Application")], LIGHT_GRAY, GRAY),
        (310, "公开 API", [(300, "DdsServer"), (750, "dds_node.hpp"), (1200, "DdsClient"), (1510, "pybind11")], LIGHT_BLUE, BLUE),
        (500, "核心层", [(350, "DdsServer::Impl"), (750, "DdsNodeCore"), (1150, "DdsClient::Impl"), (1510, "线程/回调")], PALE_BLUE, NAVY),
        (690, "传输抽象", [(600, "DdsTransport"), (1100, "Transport Factory")], LIGHT_GREEN, GREEN),
        (865, "协议后端", [(350, "ZeroMQ"), (800, "Zenoh"), (1300, "ROS DDS / CAN\n未来扩展")], LIGHT_ORANGE, ORANGE),
    ]
    for y, layer_name, items, fill_color, line_color in layers:
        draw.text((62, y + 45), layer_name, font=font(23, True), fill=line_color, anchor="lm")
        for x, text in items:
            box(draw, (x, y, x + 300, y + 105), text, fill_color, line_color, 22, bold=True)
    arrow(draw, (540, 235), (450, 310), BLUE)
    arrow(draw, (1200, 235), (1500, 310), BLUE)
    arrow(draw, (900, 415), (900, 500), NAVY)
    arrow(draw, (500, 605), (690, 690), GREEN)
    arrow(draw, (1250, 605), (1180, 690), GREEN)
    arrow(draw, (750, 795), (500, 865), ORANGE)
    arrow(draw, (900, 795), (950, 865), ORANGE)
    arrow(draw, (1100, 795), (1450, 865), ORANGE, dashed=True)
    return save(image, "01_architecture.png")


def block_diagram() -> Path:
    image, draw = canvas("双通道模块框图", "广播与请求可独立选择 ZeroMQ 或 Zenoh")
    box(draw, (70, 180, 400, 310), "DdsServer / DdsClient\nConfig", LIGHT_GRAY, GRAY, 25, bold=True)
    box(draw, (550, 160, 930, 330), "DdsNodeCore\n生命周期 · 工作线程 · 回调分发", PALE_BLUE, NAVY, 25, bold=True)
    arrow(draw, (400, 245), (550, 245), NAVY)
    box(draw, (1080, 125, 1430, 245), "Broadcast Address\n独立解析", LIGHT_BLUE, BLUE, 23)
    box(draw, (1080, 285, 1430, 405), "Request Address\n独立解析", LIGHT_GREEN, GREEN, 23)
    arrow(draw, (930, 220), (1080, 185), BLUE)
    arrow(draw, (930, 270), (1080, 345), GREEN)
    box(draw, (1480, 125, 1740, 245), "Broadcast\nDdsTransport", LIGHT_BLUE, BLUE, 22, bold=True)
    box(draw, (1480, 285, 1740, 405), "Request\nDdsTransport", LIGHT_GREEN, GREEN, 22, bold=True)
    arrow(draw, (1430, 185), (1480, 185), BLUE)
    arrow(draw, (1430, 345), (1480, 345), GREEN)
    box(draw, (170, 560, 560, 710), "ZeroMQ Broadcast\nPUB / SUB", LIGHT_ORANGE, ORANGE, 26, bold=True)
    box(draw, (610, 560, 1000, 710), "Zenoh Broadcast\nPublisher / Subscriber", LIGHT_ORANGE, ORANGE, 26, bold=True)
    box(draw, (170, 790, 560, 940), "ZeroMQ Request\nROUTER / DEALER", LIGHT_GREEN, GREEN, 26, bold=True)
    box(draw, (610, 790, 1000, 940), "Zenoh Request\nQueryable / Query", LIGHT_GREEN, GREEN, 26, bold=True)
    box(draw, (1160, 640, 1650, 855), "统一回调接口\nServer: OnRequest / OnError\nClient: OnBroadcast / OnError", PALE_BLUE, NAVY, 25, bold=True)
    arrow(draw, (1610, 245), (560, 560), BLUE)
    arrow(draw, (1610, 245), (800, 560), BLUE)
    arrow(draw, (1610, 405), (560, 790), GREEN)
    arrow(draw, (1610, 405), (800, 790), GREEN)
    arrow(draw, (1000, 635), (1160, 700), NAVY)
    arrow(draw, (1000, 865), (1160, 800), NAVY)
    return save(image, "02_block.png")


def class_diagram() -> Path:
    image, draw = canvas("核心类图", "公开 API 通过 PImpl 屏蔽协议实现")
    classes = [
        ((80, 150, 510, 390), "DdsServer", ["+ OnRequest()", "+ OnError()", "+ Start() / Stop()", "+ Publish()", "- unique_ptr<Impl>"]),
        ((80, 580, 510, 850), "DdsClient", ["+ OnBroadcast()", "+ OnError()", "+ Start() / Stop()", "+ Request(timeout)", "- unique_ptr<Impl>"]),
        ((680, 310, 1130, 700), "DdsNodeCore", ["- role", "- worker thread", "- broadcast transport", "- request transport", "+ ReceiveLoop()", "+ Dispatch()"]),
        ((1320, 130, 1720, 420), "DdsTransport", ["<<interface>>", "+ Open() / Close()", "+ Publish()", "+ Request()", "+ TryReceive()", "+ Reply()"]),
        ((1240, 600, 1470, 850), "ZeroMQ", ["PUB/SUB", "ROUTER/DEALER"]),
        ((1510, 600, 1740, 850), "Zenoh", ["Pub/Sub", "Query/Reply"]),
    ]
    for bounds, name, methods in classes:
        x1, y1, x2, y2 = bounds
        draw.rounded_rectangle(bounds, radius=16, fill=WHITE, outline=BLUE if name != "DdsTransport" else GREEN, width=4)
        draw.rectangle((x1, y1, x2, y1 + 64), fill=NAVY if name != "DdsTransport" else GREEN)
        draw.text(((x1+x2)//2, y1+31), name, font=font(25, True), fill=WHITE, anchor="mm")
        draw.line((x1, y1+64, x2, y1+64), fill="#AAB5C2", width=2)
        for index, method in enumerate(methods):
            draw.text((x1+24, y1+88+index*43), method, font=font(20), fill=DARK)
    arrow(draw, (510, 270), (680, 400), NAVY)
    label(draw, (590, 310), "PImpl", 19, NAVY)
    arrow(draw, (510, 710), (680, 610), NAVY)
    label(draw, (590, 680), "PImpl", 19, NAVY)
    arrow(draw, (1130, 460), (1320, 300), GREEN)
    label(draw, (1215, 350), "1..2", 19, GREEN)
    arrow(draw, (1360, 600), (1430, 420), GREEN, dashed=True)
    arrow(draw, (1620, 600), (1580, 420), GREEN, dashed=True)
    return save(image, "03_classes.png")


def sequence_diagram(filename: str, title: str, participants: list[str],
                     events: list[tuple[int, int, str, bool]]) -> Path:
    image, draw = canvas(title, "UML Sequence Diagram")
    left, right = 110, 1690
    count = len(participants)
    xs = [int(left + i * (right-left)/(count-1)) for i in range(count)]
    for x, name in zip(xs, participants):
        box(draw, (x-125, 125, x+125, 205), name, LIGHT_BLUE, BLUE, 19, bold=True)
        draw.line((x, 205, x, 950), fill="#AAB5C2", width=2)
    y = 270
    for source, target, text, dashed in events:
        direction = 1 if xs[target] > xs[source] else -1
        arrow(draw, (xs[source], y), (xs[target]-direction*8, y), BLUE if not dashed else GRAY, 3, dashed)
        label(draw, ((xs[source]+xs[target])//2, y-19), text, 18, DARK)
        y += 78
    return save(image, filename)


def deployment_diagram() -> Path:
    image, draw = canvas("跨主机部署图", "ZeroMQ 广播 + Zenoh 请求/应答的混合部署")
    hosts = [(60, 150, 500, 900, "Server Host"), (650, 150, 1130, 900, "Network"), (1280, 150, 1740, 900, "Client Hosts")]
    for x1, y1, x2, y2, name in hosts:
        draw.rounded_rectangle((x1, y1, x2, y2), radius=22, fill=LIGHT_GRAY, outline=GRAY, width=3)
        draw.rectangle((x1, y1, x2, y1+58), fill=NAVY)
        draw.text(((x1+x2)//2, y1+29), name, font=font(24, True), fill=WHITE, anchor="mm")
    box(draw, (120, 280, 440, 430), "Server Application\nDdsServer", LIGHT_BLUE, BLUE, 25, bold=True)
    box(draw, (120, 520, 440, 640), "ZeroMQ PUB\n192.168.1.10:8080", LIGHT_ORANGE, ORANGE, 22)
    box(draw, (120, 710, 440, 830), "Zenoh Queryable\nplant/rpc", LIGHT_GREEN, GREEN, 22)
    arrow(draw, (280, 430), (280, 520), ORANGE)
    arrow(draw, (330, 430), (330, 710), GREEN)
    box(draw, (720, 300, 1060, 470), "TCP/IP Network", LIGHT_ORANGE, ORANGE, 28, bold=True)
    box(draw, (720, 620, 1060, 790), "Zenoh Router /\nPeer Network", LIGHT_GREEN, GREEN, 28, bold=True)
    box(draw, (1340, 240, 1680, 430), "Client A\nC++ DdsClient", LIGHT_BLUE, BLUE, 25, bold=True)
    box(draw, (1340, 610, 1680, 800), "Client B..N\nPython / C++", LIGHT_BLUE, BLUE, 25, bold=True)
    arrow(draw, (440, 580), (720, 385), ORANGE)
    arrow(draw, (1060, 385), (1340, 330), ORANGE)
    arrow(draw, (1060, 385), (1340, 680), ORANGE)
    arrow(draw, (440, 770), (720, 705), GREEN)
    arrow(draw, (1340, 380), (1060, 680), GREEN)
    arrow(draw, (1340, 730), (1060, 730), GREEN)
    label(draw, (890, 250), "广播：一对多", 23, ORANGE)
    label(draw, (890, 575), "请求/应答：多对一", 23, GREEN)
    return save(image, "07_deployment.png")


def add_page_number(paragraph) -> None:
    paragraph.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    run = paragraph.add_run("第 ")
    field = OxmlElement("w:fldSimple")
    field.set(qn("w:instr"), "PAGE")
    run._r.addnext(field)
    paragraph.add_run(" 页")


def set_word_font(run, size: float, bold: bool = False, color: str = DARK) -> None:
    run.font.name = "Microsoft YaHei"
    run._element.get_or_add_rPr().rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.color.rgb = RGBColor.from_string(color.lstrip("#"))


def build_docx(figures: list[tuple[str, str, Path]]) -> Path:
    document = Document()
    section = document.sections[0]
    section.orientation = WD_ORIENT.LANDSCAPE
    section.page_width = Inches(11)
    section.page_height = Inches(8.5)
    section.top_margin = Inches(0.65)
    section.bottom_margin = Inches(0.65)
    section.left_margin = Inches(0.75)
    section.right_margin = Inches(0.75)
    section.header_distance = Inches(0.35)
    section.footer_distance = Inches(0.35)

    normal = document.styles["Normal"]
    normal.font.name = "Microsoft YaHei"
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
    normal.font.size = Pt(10.5)
    normal.paragraph_format.space_after = Pt(6)
    for style_name, size in (("Title", 27), ("Heading 1", 17), ("Heading 2", 13)):
        style = document.styles[style_name]
        style.font.name = "Microsoft YaHei"
        style._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
        style.font.size = Pt(size)
        style.font.color.rgb = RGBColor.from_string(NAVY.lstrip("#"))

    header = section.header.paragraphs[0]
    header.text = "DDS ABSTRACT | UML ARCHITECTURE"
    set_word_font(header.runs[0], 8.5, True, GRAY)
    add_page_number(section.footer.paragraphs[0])

    document.add_paragraph().paragraph_format.space_after = Pt(36)
    title = document.add_paragraph(style="Title")
    title.alignment = WD_ALIGN_PARAGRAPH.CENTER
    title.add_run("dds_abstract UML 架构与交互设计")
    subtitle = document.add_paragraph()
    subtitle.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = subtitle.add_run("架构图 · 模块框图 · 类图 · 时序图 · 部署图")
    set_word_font(run, 16, False, BLUE)
    document.add_paragraph().paragraph_format.space_after = Pt(22)
    metadata = document.add_paragraph()
    metadata.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = metadata.add_run("软件版本 1.0.0  |  C++17  |  ZeroMQ + Zenoh  |  2026-08-30")
    set_word_font(run, 10.5, False, GRAY)
    document.add_paragraph().paragraph_format.space_after = Pt(26)
    lead = document.add_paragraph()
    lead.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = lead.add_run("本文档描述协议无关通信库的静态结构、运行时交互和典型部署拓扑。")
    set_word_font(run, 12, False, DARK)

    document.add_page_break()
    document.add_heading("图册目录与阅读说明", level=1)
    intro = document.add_paragraph(
        "公开 API 仅暴露 DdsServer、DdsClient 和二进制消息类型；具体协议通过内部 "
        "DdsTransport 接口隔离。广播地址和请求地址独立解析，因此可采用不同协议。"
    )
    intro.paragraph_format.space_after = Pt(12)
    for index, (name, description, _) in enumerate(figures, 1):
        paragraph = document.add_paragraph(style="List Number")
        run = paragraph.add_run(f"{name}：{description}")
        set_word_font(run, 10.5)

    for index, (name, description, path) in enumerate(figures, 1):
        document.add_page_break()
        heading = document.add_heading(f"图 {index}  {name}", level=1)
        heading.paragraph_format.space_after = Pt(4)
        paragraph = document.add_paragraph(description)
        paragraph.paragraph_format.space_after = Pt(7)
        picture_paragraph = document.add_paragraph()
        picture_paragraph.add_run().add_picture(str(path), width=Inches(9.15))
        picture_paragraph.alignment = WD_ALIGN_PARAGRAPH.CENTER
        caption = document.add_paragraph()
        caption.alignment = WD_ALIGN_PARAGRAPH.CENTER
        run = caption.add_run(f"图 {index} - {name}")
        set_word_font(run, 9, False, GRAY)

    path = OUTPUT / "dds_abstract_UML架构与交互设计.docx"
    document.save(path)
    return path


def build_pdf(figures: list[tuple[str, str, Path]]) -> Path:
    pdfmetrics.registerFont(UnicodeCIDFont("STSong-Light"))
    path = OUTPUT / "dds_abstract_UML架构与交互设计.pdf"
    page_width, page_height = landscape(letter)
    document = SimpleDocTemplate(
        str(path), pagesize=(page_width, page_height),
        leftMargin=0.65*inch, rightMargin=0.65*inch,
        topMargin=0.55*inch, bottomMargin=0.45*inch,
        title="dds_abstract UML 架构与交互设计",
        author="dds_abstract project",
    )
    styles = getSampleStyleSheet()
    title_style = ParagraphStyle("CnTitle", parent=styles["Title"], fontName="STSong-Light", fontSize=25, leading=32, textColor=NAVY, alignment=1, spaceAfter=16)
    subtitle_style = ParagraphStyle("CnSubtitle", parent=styles["Normal"], fontName="STSong-Light", fontSize=14, leading=20, textColor=BLUE, alignment=1, spaceAfter=18)
    heading_style = ParagraphStyle("CnHeading", parent=styles["Heading1"], fontName="STSong-Light", fontSize=16, leading=21, textColor=NAVY, spaceAfter=5)
    body_style = ParagraphStyle("CnBody", parent=styles["BodyText"], fontName="STSong-Light", fontSize=9.5, leading=14, textColor=DARK, spaceAfter=6)
    caption_style = ParagraphStyle("CnCaption", parent=styles["Normal"], fontName="STSong-Light", fontSize=8.5, leading=11, textColor=GRAY, alignment=1)
    story = [Spacer(1, 1.1*inch), Paragraph("dds_abstract UML 架构与交互设计", title_style), Paragraph("架构图 · 模块框图 · 类图 · 时序图 · 部署图", subtitle_style), Spacer(1, 0.25*inch), Paragraph("软件版本 1.0.0　|　C++17　|　ZeroMQ + Zenoh　|　2026-08-30", subtitle_style), Spacer(1, 0.4*inch), Paragraph("协议无关通信抽象的静态结构、运行时交互与部署视图", subtitle_style)]
    for index, (name, description, path_image) in enumerate(figures, 1):
        story.extend([PageBreak(), Paragraph(f"图 {index}　{name}", heading_style), Paragraph(description, body_style), PdfImage(str(path_image), width=9.5*inch, height=5.54*inch), Paragraph(f"图 {index} - {name}", caption_style)])
    document.build(story)
    return path


def render_pdf(pdf: Path) -> list[Path]:
    QA.mkdir(parents=True, exist_ok=True)
    for old in QA.glob("page-*.png"):
        old.unlink()
    source = fitz.open(pdf)
    pages = []
    for index, page in enumerate(source):
        pixmap = page.get_pixmap(matrix=fitz.Matrix(1.5, 1.5), alpha=False)
        path = QA / f"page-{index+1:02d}.png"
        pixmap.save(path)
        pages.append(path)
    return pages


def build_html(figures: list[tuple[str, str, Path]]) -> Path:
    figure_sections = []
    navigation = []
    for index, (name, description, image_path) in enumerate(figures, 1):
        encoded = base64.b64encode(image_path.read_bytes()).decode("ascii")
        navigation.append(
            f'<a href="#figure-{index}"><span>{index:02d}</span>{html.escape(name)}</a>'
        )
        figure_sections.append(f"""
        <section class="figure-card" id="figure-{index}">
          <div class="figure-heading">
            <div><span class="figure-number">FIGURE {index:02d}</span>
              <h2>{html.escape(name)}</h2></div>
            <a class="back" href="#top">返回顶部 ↑</a>
          </div>
          <p class="description">{html.escape(description)}</p>
          <div class="diagram-frame">
            <img src="data:image/png;base64,{encoded}" alt="{html.escape(name)}">
          </div>
          <p class="caption">图 {index} - {html.escape(name)}</p>
        </section>""")
    content = f"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>dds_abstract UML 架构与交互设计</title>
  <style>
    :root {{--navy:#17365d;--blue:#2e75b6;--ink:#202b38;--muted:#657180;--line:#d8e0e8;--paper:#fff;--canvas:#eef3f7;}}
    * {{box-sizing:border-box}}
    html {{scroll-behavior:smooth}}
    body {{margin:0;background:var(--canvas);color:var(--ink);font-family:"Microsoft YaHei","Noto Sans CJK SC",Arial,sans-serif;line-height:1.65}}
    .hero {{background:linear-gradient(135deg,#102d50,#245b8d);color:white;padding:64px max(6vw,32px) 58px}}
    .hero-inner {{max-width:1200px;margin:auto}}
    .kicker {{font-size:13px;letter-spacing:.18em;color:#bcd9f2;font-weight:700}}
    h1 {{font-size:clamp(32px,5vw,56px);line-height:1.15;margin:14px 0 12px}}
    .subtitle {{font-size:clamp(16px,2vw,22px);color:#dbeaf7;margin:0 0 28px}}
    .meta {{display:flex;flex-wrap:wrap;gap:10px}}
    .meta span {{border:1px solid #5d86aa;border-radius:999px;padding:5px 13px;font-size:13px;background:#ffffff0e}}
    main {{max-width:1240px;margin:0 auto;padding:36px 24px 72px}}
    .overview,.figure-card {{background:var(--paper);border:1px solid var(--line);border-radius:18px;box-shadow:0 12px 35px #17365d12}}
    .overview {{padding:30px 34px;margin-top:-66px;position:relative}}
    .overview h2 {{margin:0 0 8px;color:var(--navy);font-size:25px}}
    .overview p {{margin:0 0 22px;color:var(--muted)}}
    nav {{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px}}
    nav a {{display:flex;gap:12px;align-items:center;text-decoration:none;color:var(--navy);font-weight:700;padding:10px 12px;border-radius:10px;background:#f2f7fb;border:1px solid #d9e8f4}}
    nav a:hover {{background:#e2f0fb;transform:translateY(-1px)}}
    nav span {{color:var(--blue);font-size:12px;letter-spacing:.08em}}
    .figure-card {{padding:28px 30px 22px;margin-top:28px;scroll-margin-top:18px}}
    .figure-heading {{display:flex;align-items:flex-end;justify-content:space-between;gap:20px;border-bottom:2px solid var(--navy);padding-bottom:10px}}
    .figure-number {{font-size:11px;letter-spacing:.16em;color:var(--blue);font-weight:800}}
    .figure-card h2 {{font-size:27px;color:var(--navy);margin:2px 0 0}}
    .back {{font-size:13px;color:var(--blue);text-decoration:none;white-space:nowrap}}
    .description {{color:var(--muted);margin:14px 0}}
    .diagram-frame {{background:#fff;border:1px solid #dfe6ed;border-radius:12px;overflow:hidden;padding:8px}}
    img {{display:block;width:100%;height:auto}}
    .caption {{text-align:center;color:var(--muted);font-size:13px;margin:10px 0 0}}
    footer {{text-align:center;color:#708090;padding:22px;font-size:13px}}
    @media(max-width:640px) {{main{{padding:26px 12px 50px}}.overview,.figure-card{{padding:20px 16px}}.figure-heading{{align-items:flex-start}}.back{{display:none}}}}
    @media print {{body{{background:white}}.hero{{padding:30px;background:var(--navy)!important;-webkit-print-color-adjust:exact;print-color-adjust:exact}}main{{padding:0}}.overview{{margin:20px 0;box-shadow:none}}.figure-card{{break-before:page;margin:0;border:0;box-shadow:none;padding:16px 0}}.back{{display:none}}}}
  </style>
</head>
<body id="top">
  <header class="hero"><div class="hero-inner">
    <div class="kicker">DDS ABSTRACT · TECHNICAL DESIGN</div>
    <h1>UML 架构与交互设计</h1>
    <p class="subtitle">架构图 · 模块框图 · 类图 · 时序图 · 部署图</p>
    <div class="meta"><span>版本 1.0.0</span><span>C++17</span><span>ZeroMQ + Zenoh</span><span>2026-08-30</span></div>
  </div></header>
  <main>
    <section class="overview">
      <h2>图册导航</h2>
      <p>描述协议无关通信库的静态结构、运行时交互以及典型部署拓扑。HTML 为单文件，可离线浏览和打印。</p>
      <nav>{''.join(navigation)}</nav>
    </section>
    {''.join(figure_sections)}
  </main>
  <footer>dds_abstract 1.0.0 · UML Architecture and Interaction Design</footer>
</body>
</html>"""
    path = OUTPUT / "dds_abstract_UML架构与交互设计.html"
    path.write_text(content, encoding="utf-8")
    return path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--html-only", action="store_true")
    args = parser.parse_args()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)
    figures = [
        ("总体架构图", "展示应用层、公开 API、核心协调层、传输抽象层和协议后端之间的依赖关系。", architecture_diagram()),
        ("双通道模块框图", "广播和请求地址分别解析，并分别创建传输实例，可组合 ZeroMQ 与 Zenoh。", block_diagram()),
        ("核心类图", "展示公开类、PImpl 核心和 DdsTransport 后端实现之间的结构关系。", class_diagram()),
        ("节点启动时序图", "展示通道打开、工作线程创建、线程配置以及启动就绪同步。", sequence_diagram("04_startup_sequence.png", "节点启动时序图", ["Application", "DdsNode", "DdsNodeCore", "Transport", "Worker"], [(0,1,"注册回调 / Start()",False),(1,2,"Start()",False),(2,3,"Open()",False),(2,4,"创建工作线程",False),(4,4,"配置线程",False),(4,2,"ready",True),(2,1,"启动完成",True),(1,0,"Start() 返回",True)])),
        ("一对多广播时序图", "一个 server 发布后，各 client 的接收线程分别触发广播回调。", sequence_diagram("05_broadcast_sequence.png", "一对多广播时序图", ["Server App", "DdsServer", "Backend", "Client 1", "Client 2", "Client N"], [(0,1,"Publish(message)",False),(1,2,"Publish",False),(2,3,"broadcast",False),(2,4,"broadcast",False),(2,5,"broadcast",False),(3,3,"OnBroadcast",False),(4,4,"OnBroadcast",False),(5,5,"OnBroadcast",False)])),
        ("请求/应答交互时序图", "多个 client 可连接同一 server；关联信息由协议后端维护，业务只处理请求载荷。", sequence_diagram("06_request_sequence.png", "请求/应答交互时序图", ["Client App", "DdsClient", "Protocol", "Server Worker", "Server App"], [(0,1,"Request(payload, timeout)",False),(1,2,"发送请求",False),(2,3,"请求 + correlation",False),(3,4,"OnRequest(payload)",False),(4,3,"response",True),(3,2,"Reply(correlation)",True),(2,1,"response",True),(1,0,"DdsBytes",True)])),
        ("跨主机部署图", "示例采用 ZeroMQ TCP 广播和 Zenoh 请求/应答，C++ 与 Python client 可以共同接入。", deployment_diagram()),
    ]
    html_path = build_html(figures)
    print(f"HTML={html_path}")
    if not args.html_only:
        docx = build_docx(figures)
        pdf = build_pdf(figures)
        pages = render_pdf(pdf)
        print(f"DOCX={docx}")
        print(f"PDF={pdf}")
        print(f"PDF_PAGES={len(pages)}")


if __name__ == "__main__":
    main()
