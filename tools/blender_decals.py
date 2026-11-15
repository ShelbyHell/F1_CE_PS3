"""F1 CE car liveries in Blender: layers the decal sheet over the paint on models exported by tools/sg.py gltf.

The game's livery materials have two colour textures: the paint (_ca, first UV map) and the sponsor decal sheet
(_cb, second UV map), which the shader lays over the paint by the sheet's alpha. glTF has one base colour texture,
so sg.py names the decal sheet in each material's extras (custom property f1ce_decals). This script adds it to the
material nodes: Image (decals, UVMap.001) -> Mix by alpha over the paint -> Base Color.

Use: File > Import > glTF 2.0 (.glb), then Scripting tab > Open this file > Run Script. Running it again does
nothing to materials already done. The PNGs are loaded from their files: edit them (paint / decals) and use
Image > Reload in Blender to see the change.
"""
import os

import bpy


def second_uv_map(mat):
    """The name of the decal UV map (the second one) on a mesh that uses the material."""
    for ob in bpy.data.objects:
        if ob.type == 'MESH' and any(s.material == mat for s in ob.material_slots) and len(ob.data.uv_layers) > 1:
            return ob.data.uv_layers[1].name
    return None


def add_decals(mat):
    nt = mat.node_tree
    bsdf = next((n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'), None)
    if bsdf is None:
        return 'no Principled BSDF'
    base_in = bsdf.inputs['Base Color']
    paint = base_in.links[0].from_node if base_in.links else None
    # the PNG paths in the extras are relative to the .glb: find it from the paint image's file
    if paint is None or paint.type != 'TEX_IMAGE' or paint.image is None:
        return 'no paint texture'
    glb_dir = os.path.dirname(bpy.path.abspath(paint.image.filepath))
    rel_base = os.path.dirname(mat['f1ce_base']) if 'f1ce_base' in mat else ''
    path = os.path.normpath(os.path.join(glb_dir, os.path.relpath(mat['f1ce_decals'], rel_base or '.')))
    if not os.path.exists(path):
        return f'{path} not found'
    uv_name = second_uv_map(mat)
    if uv_name is None:
        return 'no mesh with a second UV map'

    x, y = paint.location
    uv = nt.nodes.new('ShaderNodeUVMap')
    uv.uv_map = uv_name
    uv.location = (x - 300, y - 320)
    img = nt.nodes.new('ShaderNodeTexImage')
    img.image = bpy.data.images.load(path, check_existing=True)
    img.label = 'F1 CE decals'
    img.location = (x, y - 320)
    nt.links.new(uv.outputs['UV'], img.inputs['Vector'])
    try:        # Blender 3.4+
        mix = nt.nodes.new('ShaderNodeMix')
        mix.data_type = 'RGBA'
        fac, a, b, result = mix.inputs[0], mix.inputs[6], mix.inputs[7], mix.outputs[2]
    except RuntimeError:
        mix = nt.nodes.new('ShaderNodeMixRGB')
        fac, a, b, result = mix.inputs[0], mix.inputs[1], mix.inputs[2], mix.outputs[0]
    mix.label = 'Decals over paint'
    mix.location = (x + 300, y - 120)
    nt.links.new(base_in.links[0].from_socket, a)
    nt.links.new(img.outputs['Color'], b)
    nt.links.new(img.outputs['Alpha'], fac)
    nt.links.new(result, base_in)
    mat['f1ce_decals_done'] = 1
    return None


def main():
    done = 0
    for mat in bpy.data.materials:
        if 'f1ce_decals' not in mat or mat.get('f1ce_decals_done') or not mat.use_nodes:
            continue
        err = add_decals(mat)
        if err:
            print(f'F1 CE decals: {mat.name}: {err}')
        else:
            done += 1
    print(f'F1 CE decals: {done} material(s) updated')


main()
