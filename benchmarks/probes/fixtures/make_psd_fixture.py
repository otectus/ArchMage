# GIMP 3.2 Python-Fu batch script: authors the layered PSD fixture of the PSD decode probe (plan S1-6.5,
# corpus plan "Layered PSD"). Run:
#   ARCHMAGE_PSD_OUT=<file.psd> gimp-console-3.2 -i --quit --batch-interpreter=python-fu-eval \
#       -b "exec(open('benchmarks/probes/fixtures/make_psd_fixture.py').read())"
# Layers, bottom to top: an opaque background; a multiply disc; a screen bar at 70 %; a layer with a mask;
# a group holding a normal square. Masks and groups are features ArchMage Stage 1 cannot read.
import os

import gi

gi.require_version("Gimp", "3.0")
gi.require_version("Gegl", "0.4")
from gi.repository import Gegl, Gimp, Gio  # noqa: E402

WIDTH, HEIGHT = 512, 384
out = os.environ["ARCHMAGE_PSD_OUT"]
image = Gimp.Image.new(WIDTH, HEIGHT, Gimp.ImageBaseType.RGB)


def color(hex_value):
    value = Gegl.Color.new(hex_value)
    return value


def add_layer(name, mode, opacity, parent=None):
    layer = Gimp.Layer.new(image, name, WIDTH, HEIGHT, Gimp.ImageType.RGBA_IMAGE, opacity, mode)
    image.insert_layer(layer, parent, 0)
    return layer


def fill_selection(layer, hex_value):
    Gimp.context_set_foreground(color(hex_value))
    layer.edit_fill(Gimp.FillType.FOREGROUND)
    Gimp.Selection.none(image)


background = add_layer("Background", Gimp.LayerMode.NORMAL, 100.0)
Gimp.context_set_foreground(color("#c8d2e6"))
background.fill(Gimp.FillType.FOREGROUND)

disc = add_layer("Red disc (multiply)", Gimp.LayerMode.MULTIPLY, 100.0)
image.select_ellipse(Gimp.ChannelOps.REPLACE, 60, 60, 220, 220)
fill_selection(disc, "#d23c32")

bar = add_layer("Green bar (screen 70%)", Gimp.LayerMode.SCREEN, 70.0)
image.select_rectangle(Gimp.ChannelOps.REPLACE, 0, 150, WIDTH, 90)
fill_selection(bar, "#3cb45a")

masked = add_layer("Orange with mask", Gimp.LayerMode.NORMAL, 100.0)
image.select_rectangle(Gimp.ChannelOps.REPLACE, 300, 40, 180, 300)
fill_selection(masked, "#f0a028")
mask = masked.create_mask(Gimp.AddMaskType.BLACK)
masked.add_mask(mask)
image.select_ellipse(Gimp.ChannelOps.REPLACE, 320, 80, 140, 220)
Gimp.context_set_foreground(color("#ffffff"))
mask.edit_fill(Gimp.FillType.FOREGROUND)
Gimp.Selection.none(image)

group = Gimp.GroupLayer.new(image, "Group")
image.insert_layer(group, None, 0)
square = add_layer("Blue square (in group)", Gimp.LayerMode.NORMAL, 100.0, group)
image.select_rectangle(Gimp.ChannelOps.REPLACE, 200, 220, 120, 120)
fill_selection(square, "#285adc")

Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(out), None)
print("make_psd_fixture: wrote", out)
