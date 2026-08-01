"""Create deterministic opaque matte materials for the curriculum objects.

Run with UnrealEditor-Cmd.exe and -ExecutePythonScript. The script is
idempotent so the authored assets can be recreated after cloning the project.
"""

import unreal


ASSET_FOLDER = "/Game/Curriculum/Materials"
MASTER_NAME = "M_CurriculumMatte"
MASTER_PATH = f"{ASSET_FOLDER}/{MASTER_NAME}"

COLORS = {
    "MI_Curriculum_Floor": unreal.LinearColor(0.32, 0.32, 0.32, 1.0),
    "MI_Curriculum_Wall": unreal.LinearColor(0.28, 0.28, 0.28, 1.0),
    "MI_Curriculum_Rectangle": unreal.LinearColor(0.24, 0.08, 0.025, 1.0),
    "MI_Curriculum_Triangle": unreal.LinearColor(0.70, 0.48, 0.04, 1.0),
    "MI_Curriculum_Sphere": unreal.LinearColor(0.72, 0.18, 0.02, 1.0),
    "MI_Curriculum_Hoop": unreal.LinearColor(0.60, 0.04, 0.20, 1.0),
    "MI_Curriculum_Ramp": unreal.LinearColor(0.55, 0.025, 0.02, 1.0),
}


def load_or_create_master(asset_tools):
    master = unreal.EditorAssetLibrary.load_asset(MASTER_PATH)
    if master:
        return master

    factory = unreal.MaterialFactoryNew()
    master = asset_tools.create_asset(
        MASTER_NAME,
        ASSET_FOLDER,
        unreal.Material,
        factory,
    )
    if not master:
        raise RuntimeError(f"Could not create {MASTER_PATH}")

    master.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    master.set_editor_property(
        "shading_model",
        unreal.MaterialShadingModel.MSM_DEFAULT_LIT,
    )
    master.set_editor_property("two_sided", False)

    base_color = unreal.MaterialEditingLibrary.create_material_expression(
        master,
        unreal.MaterialExpressionVectorParameter,
        -420,
        -100,
    )
    base_color.set_editor_property("parameter_name", "BaseColor")
    base_color.set_editor_property(
        "default_value",
        unreal.LinearColor(0.35, 0.35, 0.35, 1.0),
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        base_color,
        "",
        unreal.MaterialProperty.MP_BASE_COLOR,
    )

    roughness = unreal.MaterialEditingLibrary.create_material_expression(
        master,
        unreal.MaterialExpressionScalarParameter,
        -420,
        40,
    )
    roughness.set_editor_property("parameter_name", "Roughness")
    roughness.set_editor_property("default_value", 1.0)
    unreal.MaterialEditingLibrary.connect_material_property(
        roughness,
        "",
        unreal.MaterialProperty.MP_ROUGHNESS,
    )

    specular = unreal.MaterialEditingLibrary.create_material_expression(
        master,
        unreal.MaterialExpressionScalarParameter,
        -420,
        160,
    )
    specular.set_editor_property("parameter_name", "Specular")
    specular.set_editor_property("default_value", 0.0)
    unreal.MaterialEditingLibrary.connect_material_property(
        specular,
        "",
        unreal.MaterialProperty.MP_SPECULAR,
    )

    unreal.MaterialEditingLibrary.recompile_material(master)
    unreal.EditorAssetLibrary.save_loaded_asset(master)
    return master


def load_or_create_instance(asset_tools, master, name, color):
    asset_path = f"{ASSET_FOLDER}/{name}"
    instance = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not instance:
        factory = unreal.MaterialInstanceConstantFactoryNew()
        instance = asset_tools.create_asset(
            name,
            ASSET_FOLDER,
            unreal.MaterialInstanceConstant,
            factory,
        )
    if not instance:
        raise RuntimeError(f"Could not create {asset_path}")

    instance.set_editor_property("parent", master)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        instance,
        "BaseColor",
        color,
    )
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        instance,
        "Roughness",
        1.0,
    )
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        instance,
        "Specular",
        0.0,
    )
    unreal.EditorAssetLibrary.save_loaded_asset(instance)


def main():
    unreal.EditorAssetLibrary.make_directory(ASSET_FOLDER)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    master = load_or_create_master(asset_tools)
    for name, color in COLORS.items():
        load_or_create_instance(asset_tools, master, name, color)
    unreal.log("Created opaque matte curriculum materials.")


main()
