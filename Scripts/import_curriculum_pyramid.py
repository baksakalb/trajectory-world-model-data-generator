"""Import the curriculum's genuine solid pyramid static mesh."""

import os
import unreal


PROJECT_DIR = unreal.Paths.project_dir()
SOURCE_FILE = os.path.join(
    PROJECT_DIR,
    "ContentSource",
    "CurriculumPyramid.obj",
)
DESTINATION_PATH = "/Game/Curriculum/Meshes"
ASSET_PATH = f"{DESTINATION_PATH}/SM_CurriculumPyramid"


def main():
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", SOURCE_FILE)
    task.set_editor_property("destination_path", DESTINATION_PATH)
    task.set_editor_property("destination_name", "SM_CurriculumPyramid")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)

    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property(
        "mesh_type_to_import",
        unreal.FBXImportType.FBXIT_STATIC_MESH,
    )
    options.static_mesh_import_data.set_editor_property("combine_meshes", True)
    options.static_mesh_import_data.set_editor_property(
        "normal_import_method",
        unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
    )
    options.static_mesh_import_data.set_editor_property(
        "generate_lightmap_u_vs",
        True,
    )
    task.set_editor_property("options", options)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
    if not mesh:
        raise RuntimeError(f"Failed to import {ASSET_PATH}")

    body_setup = mesh.get_editor_property("body_setup")
    if body_setup:
        body_setup.set_editor_property(
            "collision_trace_flag",
            unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE,
        )

    mesh.set_editor_property("light_map_resolution", 64)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    unreal.log("Imported genuine solid curriculum pyramid.")


main()
