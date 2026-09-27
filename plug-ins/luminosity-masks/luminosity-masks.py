#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Create Luminosity Masks for GIMP 3
# Copyright 2026 David
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 3 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see
# <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

"""Create Luminosity Masks for GIMP 3.

Makes the standard set of luminosity masks as channels: Lights 1 to 5,
Darks 1 to 5 and Midtones 1 to 5, computed by the adj:luminosity-mask
GEGL operation of gegl-adjustments from the visible image or the
selected layer. Channel > Channel to Selection, or a layer mask
initialized from a channel, then uses them.

GIMP 3.2 can keep a filter live on a channel when it is applied from the
menu (Colors > Luminosity Mask with the channel selected), but its PDB
does not let a plug-in do that ("only drawables of type GimpLayer can
have non-destructive effects"), so the channels hold the masks' values.
README.md describes the steps for a live one.
"""

import sys
import traceback

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, GLib, GObject  # noqa: E402

PROC = 'plug-in-adj-luminosity-masks'
PLUG_IN_BINARY = 'luminosity-masks'
OP = 'adj:luminosity-mask'
AUTHOR = 'David'
COPYRIGHT = 'Copyright 2026 David, LGPL-3.0-or-later'
DATE = '2026'

SERIES = [('tk', 'TK (squares: L, L^2, L^4, L^8, L^16)'),
          ('powers', 'Powers (L, L^2, L^3, L^4, L^5)')]
LUMINOSITY = [('gray', 'Photoshop Gray (0.30 R + 0.59 G + 0.11 B)'),
              ('luminance', "Luminance of the image's color space"),
              ('lightness', 'Lightness (CIE L*)')]
SOURCES = [('visible', 'The visible image'), ('layer', 'The selected layer')]
KINDS = [('lights', 'Lights'), ('darks', 'Darks'), ('midtones', 'Midtones')]


def make_choice(table):
    choice = Gimp.Choice.new()
    for i, (nick, label) in enumerate(table):
        choice.add(nick, i, label, '')
    return choice


def enum_value(op, prop, nick):
    """the number of an enum value of the operation, from its nick"""
    for pspec in Gegl.Operation.list_properties(op):
        if pspec.name == prop:
            for v in pspec.enum_class.__enum_values__.values():
                if v.value_nick == nick:
                    return int(v)
    raise KeyError('%s %s=%s' % (op, prop, nick))


class Failure(Exception):
    pass


def luminosity_masks(image, source_drawable, kinds, levels, series, luminosity):
    """Adds the channels; returns their names"""
    if not Gegl.has_operation(OP):
        raise Failure('The GEGL operation %s is not installed (gegl-adjustments, '
                      'luminosity-mask.so).' % OP)
    w, h = image.get_width(), image.get_height()
    rect = Gegl.Rectangle.new(0, 0, w, h)

    temp = None
    if source_drawable is None:
        temp = Gimp.Layer.new_from_visible(image, image, 'visible')
        image.insert_layer(temp, None, 0)
        source = temp.get_buffer()
        ox, oy = 0, 0
    else:
        source = source_drawable.get_buffer()
        ok, ox, oy = source_drawable.get_offsets()

    # the luminosity of every pixel, "Y' float" numbers in 0 to 1: Lights 1
    graph = Gegl.Node()
    src = graph.create_child('gegl:buffer-source')
    src.set_property('buffer', source)
    shift = graph.create_child('gegl:translate')
    shift.set_property('x', float(ox))
    shift.set_property('y', float(oy))
    node = graph.create_child(OP)
    node.set_property('mask', enum_value(OP, 'mask', 'lights'))
    node.set_property('level', 1)
    node.set_property('luminosity', enum_value(OP, 'luminosity', luminosity))
    base = Gegl.Buffer.new("Y' float", 0, 0, w, h)
    sink = graph.create_child('gegl:write-buffer')
    sink.set_property('buffer', base)
    src.link(shift)
    shift.link(node)
    node.link(sink)
    sink.process()
    if temp is not None:
        image.remove_layer(temp)

    names = []
    # a channel holds the mask's numbers as they are: in "Y' u8" for 8 bit
    # perceptual images, in linear "Y ..." formats otherwise (GIMP's
    # gimp_image_get_channel_format); the numbers are written in the
    # channel's own terms so that 0.5 is half selected
    for kind in kinds:
        for level in levels:
            g = Gegl.Node()
            s = g.create_child('gegl:buffer-source')
            s.set_property('buffer', base)
            n = g.create_child(OP)
            n.set_property('mask', enum_value(OP, 'mask', kind))
            n.set_property('level', level)
            n.set_property('series', enum_value(OP, 'series', series))
            out = Gegl.Buffer.new("Y' float", 0, 0, w, h)
            k = g.create_child('gegl:write-buffer')
            k.set_property('buffer', out)
            s.link(n)
            n.link(k)
            k.process()
            name = '%s %d' % (dict(KINDS)[kind], level)
            ch = Gimp.Channel.new(image, name, w, h, 50.0, Gegl.Color.new('black'))
            # below the channels there are, in the order made
            image.insert_channel(ch, None, len(image.get_channels()))
            ch.set_visible(False)
            own = ("Y' float" if image.get_precision() == Gimp.Precision.U8_NON_LINEAR
                   else 'Y float')
            cb = ch.get_buffer()
            cb.set(rect, own, out.get(rect, 1.0, "Y' float", Gegl.AbyssPolicy.NONE))
            cb.flush()
            ch.update(0, 0, w, h)
            names.append(ch.get_name())
    return names


def show_dialog(procedure, config):
    gi.require_version('GimpUi', '3.0')
    from gi.repository import GimpUi
    GimpUi.init(PLUG_IN_BINARY)
    dialog = GimpUi.ProcedureDialog.new(procedure, config, 'Create Luminosity Masks')
    dialog.get_widget('source', GimpUi.IntRadioFrame)
    dialog.fill(['source', 'lights', 'darks', 'midtones', 'levels', 'series', 'luminosity'])
    try:
        return dialog.run()
    finally:
        dialog.destroy()


def run(procedure, run_mode, image, drawables, config, data):
    if run_mode == Gimp.RunMode.INTERACTIVE and not show_dialog(procedure, config):
        return procedure.new_return_values(Gimp.PDBStatusType.CANCEL, GLib.Error())
    kinds = [k for k, _ in KINDS if config.get_property(k)]
    levels = list(range(1, config.get_property('levels') + 1))
    layer = None
    if config.get_property('source') == 'layer':
        layer = next((d for d in drawables if isinstance(d, Gimp.Layer)), None)
        if layer is None:
            return procedure.new_return_values(
                Gimp.PDBStatusType.CALLING_ERROR,
                GLib.Error('Select a layer, or make the masks from the visible image.'))
    image.undo_group_start()
    try:
        names = luminosity_masks(image, layer, kinds, levels, config.get_property('series'),
                                 config.get_property('luminosity'))
    except Failure as e:
        if run_mode != Gimp.RunMode.NONINTERACTIVE:
            Gimp.message(str(e))
        return procedure.new_return_values(Gimp.PDBStatusType.EXECUTION_ERROR,
                                           GLib.Error(str(e)))
    except Exception as e:
        traceback.print_exc()
        return procedure.new_return_values(Gimp.PDBStatusType.EXECUTION_ERROR,
                                           GLib.Error('Create Luminosity Masks failed: %s' % e))
    finally:
        image.undo_group_end()
    Gimp.displays_flush()
    retval = procedure.new_return_values(Gimp.PDBStatusType.SUCCESS, GLib.Error())
    retval.remove(1)
    retval.insert(1, GObject.Value(GObject.TYPE_STRING, '\n'.join(names)))
    return retval


class LuminosityMasks(Gimp.PlugIn):

    def do_set_i18n(self, name):
        return False

    def do_query_procedures(self):
        return [PROC]

    def do_create_procedure(self, name):
        Gegl.init(None)
        flags = GObject.ParamFlags.READWRITE
        procedure = Gimp.ImageProcedure.new(self, name, Gimp.PDBProcType.PLUGIN, run, None)
        procedure.set_menu_label('Create Luminosity _Masks...')
        procedure.set_documentation(
            'Makes Lights, Darks and Midtones luminosity masks as channels',
            'Makes the standard set of luminosity masks as channels: Lights 1 to 5, Darks '
            '1 to 5 and Midtones 1 to 5 (Tony Kuyper\'s recipe, or powers), from the '
            'visible image or the selected layer, with the adj:luminosity-mask operation. '
            'Channel to Selection then selects by them.',
            name)
        procedure.add_choice_argument('source', 'Make them from',
                                      'The visible image or the selected layer',
                                      make_choice(SOURCES), 'visible', flags)
        procedure.add_boolean_argument('lights', '_Lights', 'Lights 1 to n', True, flags)
        procedure.add_boolean_argument('darks', '_Darks', 'Darks 1 to n', True, flags)
        procedure.add_boolean_argument('midtones', '_Midtones', 'Midtones 1 to n', True, flags)
        procedure.add_int_argument('levels', 'L_evels', 'How many of each: 1 to n', 1, 5, 5,
                                   flags)
        procedure.add_choice_argument('series', '_Series',
                                      'How each level narrows the one before',
                                      make_choice(SERIES), 'tk', flags)
        procedure.add_choice_argument('luminosity', 'L_uminosity',
                                      'The gray the tones are measured with',
                                      make_choice(LUMINOSITY), 'gray', flags)
        procedure.add_string_return_value('channels', 'Channels',
                                          'The names of the channels made, one per line',
                                          '', flags)
        procedure.set_image_types('RGB*, GRAY*')
        procedure.set_sensitivity_mask(Gimp.ProcedureSensitivityMask.DRAWABLE |
                                       Gimp.ProcedureSensitivityMask.DRAWABLES |
                                       Gimp.ProcedureSensitivityMask.NO_DRAWABLES)
        procedure.set_attribution(AUTHOR, COPYRIGHT, DATE)
        procedure.add_menu_path('<Image>/Colors')
        return procedure


if __name__ == '__main__':
    Gimp.main(LuminosityMasks.__gtype__, sys.argv)
