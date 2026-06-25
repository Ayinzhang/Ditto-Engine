#!/usr/bin/env python3
"""
Synchronize Ditto's hand-maintained Visual Studio project.

The project view is intentionally small: it lists Ditto editor/engine source
files plus a short explicit list of third-party compilation units required for
the build. Third-party headers and implementation detail files such as GLM
.inl files are not added to Solution Explorer.
"""

from datetime import datetime
from pathlib import Path
import shutil
import sys
import xml.etree.ElementTree as ET

PROJECT_ROOT = Path(__file__).parent
VCXPROJ_PATH = PROJECT_ROOT / "Ditto" / "Ditto.vcxproj"
FILTERS_PATH = PROJECT_ROOT / "Ditto" / "Ditto.vcxproj.filters"
DITTO_DIR = PROJECT_ROOT / "Ditto"

PROJECT_CODE_DIRS = ("Editor", "Engine")
HEADER_EXTENSIONS = {".h", ".hpp"}
SOURCE_EXTENSIONS = {".c", ".cpp"}

HEADER_FILTER_ROOT = "Header Files"
SOURCE_FILTER_ROOT = "Source Files"
RESOURCE_FILTER_ROOT = "Resource Files"

EXPLICIT_HEADERS = [
    "resource.h",
]

THIRD_PARTY_SOURCES = [
    "3rdParty/GLAD/glad.c",
    "3rdParty/GLM/detail/glm.cpp",
    "3rdParty/ImGui/imgui.cpp",
    "3rdParty/ImGui/imgui_demo.cpp",
    "3rdParty/ImGui/imgui_draw.cpp",
    "3rdParty/ImGui/imgui_impl_glfw.cpp",
    "3rdParty/ImGui/imgui_impl_opengl3.cpp",
    "3rdParty/ImGui/imgui_impl_vulkan.cpp",
    "3rdParty/ImGui/imgui_impl_win32.cpp",
    "3rdParty/ImGui/imgui_tables.cpp",
    "3rdParty/ImGui/imgui_widgets.cpp",
    "3rdParty/ImGuizmo/ImGuizmo.cpp",
]

VULKAN_CONDITION = "'$(EnableVulkan)'=='true'"


def project_path(path):
    return str(path).replace("/", "\\")


def normalized_item_path(path):
    return path.replace("\\", "/")


def find_project_files(extensions):
    files = []
    for directory in PROJECT_CODE_DIRS:
        root = DITTO_DIR / directory
        if not root.exists():
            continue
        for filepath in root.rglob("*"):
            if filepath.is_file() and filepath.suffix in extensions:
                files.append(filepath.relative_to(DITTO_DIR).as_posix())
    return sorted(files)


def existing_paths(paths):
    return sorted(path for path in paths if (DITTO_DIR / path).exists())


def backup_vcxproj(vcxproj_path):
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_path = vcxproj_path.with_suffix(f".vcxproj.backup_{timestamp}")
    shutil.copy2(vcxproj_path, backup_path)
    print(f"[+] Backup created: {backup_path.name}")
    return backup_path


def parse_project(path):
    tree = ET.parse(path)
    root = tree.getroot()
    ns = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}
    ET.register_namespace("", ns["ms"])
    return tree, root, ns


def has_child(item_group, ns, tag):
    return item_group.find(f"ms:{tag}", ns) is not None


def remove_generated_file_groups(root, ns):
    to_remove = []
    for item_group in root.findall("ms:ItemGroup", ns):
        if has_child(item_group, ns, "ClInclude") or has_child(item_group, ns, "ClCompile"):
            to_remove.append(item_group)
    for item_group in to_remove:
        root.remove(item_group)


def clean_resource_items(root, ns):
    for item_group in root.findall("ms:ItemGroup", ns):
        for none in list(item_group.findall("ms:None", ns)):
            include = normalized_item_path(none.get("Include", ""))
            if include.startswith("3rdParty/GLM/") and include.endswith(".inl"):
                item_group.remove(none)


def insert_file_item_groups(root, ns, headers, sources):
    insert_index = 0
    for index, child in enumerate(root):
        if child.tag.endswith("ItemGroup"):
            insert_index = index + 1
            break

    header_group = ET.Element(f"{{{ns['ms']}}}ItemGroup")
    for header in headers:
        elem = ET.SubElement(header_group, f"{{{ns['ms']}}}ClInclude")
        elem.set("Include", project_path(header))

    source_group = ET.Element(f"{{{ns['ms']}}}ItemGroup")
    for source in sources:
        elem = ET.SubElement(source_group, f"{{{ns['ms']}}}ClCompile")
        elem.set("Include", project_path(source))
        if source == "3rdParty/ImGui/imgui_impl_vulkan.cpp" or source.startswith("Engine/Graphics/RHI/Vulkan/"):
            elem.set("Condition", VULKAN_CONDITION)

    root.insert(insert_index, header_group)
    root.insert(insert_index + 1, source_group)


def filter_for_path(path, root_name):
    parts = Path(path).parts[:-1]
    if not parts:
        return root_name
    return root_name + "\\" + "\\".join(parts)


def stable_guid(label):
    import hashlib

    digest = hashlib.md5(label.encode("utf-8")).hexdigest().upper()
    return "{" + "-".join([
        digest[0:8],
        digest[8:12],
        digest[12:16],
        digest[16:20],
        digest[20:32],
    ]) + "}"


def add_filter_tree(filters, filter_path):
    parts = filter_path.split("\\")
    for index in range(1, len(parts) + 1):
        filters.add("\\".join(parts[:index]))


def collect_resource_items(project_root, ns):
    resources = []
    for item_group in project_root.findall("ms:ItemGroup", ns):
        for tag in ("None", "ResourceCompile", "Image", "Library"):
            for item in item_group.findall(f"ms:{tag}", ns):
                include = normalized_item_path(item.get("Include", ""))
                if include.startswith("3rdParty/GLM/") and include.endswith(".inl"):
                    continue
                resources.append((tag, include))
    return sorted(set(resources), key=lambda value: (value[0], value[1]))


def write_filters_file(headers, sources, resources):
    ns_uri = "http://schemas.microsoft.com/developer/msbuild/2003"
    ET.register_namespace("", ns_uri)
    root = ET.Element(f"{{{ns_uri}}}Project", {"ToolsVersion": "4.0"})

    filters = {HEADER_FILTER_ROOT, SOURCE_FILTER_ROOT, RESOURCE_FILTER_ROOT}
    for header in headers:
        add_filter_tree(filters, filter_for_path(header, HEADER_FILTER_ROOT))
    for source in sources:
        add_filter_tree(filters, filter_for_path(source, SOURCE_FILTER_ROOT))
    for _, resource in resources:
        add_filter_tree(filters, filter_for_path(resource, RESOURCE_FILTER_ROOT))

    filter_group = ET.SubElement(root, f"{{{ns_uri}}}ItemGroup")
    for filter_name in sorted(filters):
        elem = ET.SubElement(filter_group, f"{{{ns_uri}}}Filter", {"Include": filter_name})
        guid = ET.SubElement(elem, f"{{{ns_uri}}}UniqueIdentifier")
        guid.text = stable_guid(filter_name)
        if filter_name == HEADER_FILTER_ROOT:
            extensions = ET.SubElement(elem, f"{{{ns_uri}}}Extensions")
            extensions.text = "h;hh;hpp;hxx;h++;hm"
        elif filter_name == SOURCE_FILTER_ROOT:
            extensions = ET.SubElement(elem, f"{{{ns_uri}}}Extensions")
            extensions.text = "cpp;c;cc;cxx;c++"
        elif filter_name == RESOURCE_FILTER_ROOT:
            extensions = ET.SubElement(elem, f"{{{ns_uri}}}Extensions")
            extensions.text = "rc;ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe;resx;tiff;tif;png;wav"

    header_group = ET.SubElement(root, f"{{{ns_uri}}}ItemGroup")
    for header in headers:
        elem = ET.SubElement(header_group, f"{{{ns_uri}}}ClInclude", {"Include": project_path(header)})
        filter_elem = ET.SubElement(elem, f"{{{ns_uri}}}Filter")
        filter_elem.text = filter_for_path(header, HEADER_FILTER_ROOT)

    source_group = ET.SubElement(root, f"{{{ns_uri}}}ItemGroup")
    for source in sources:
        elem = ET.SubElement(source_group, f"{{{ns_uri}}}ClCompile", {"Include": project_path(source)})
        filter_elem = ET.SubElement(elem, f"{{{ns_uri}}}Filter")
        filter_elem.text = filter_for_path(source, SOURCE_FILTER_ROOT)

    resource_group = ET.SubElement(root, f"{{{ns_uri}}}ItemGroup")
    for tag, resource in resources:
        elem = ET.SubElement(resource_group, f"{{{ns_uri}}}{tag}", {"Include": project_path(resource)})
        filter_elem = ET.SubElement(elem, f"{{{ns_uri}}}Filter")
        filter_elem.text = filter_for_path(resource, RESOURCE_FILTER_ROOT)

    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    tree.write(FILTERS_PATH, encoding="utf-8", xml_declaration=True)


def read_project_file_lists(root, ns):
    headers = []
    sources = []
    for item_group in root.findall("ms:ItemGroup", ns):
        for include in item_group.findall("ms:ClInclude", ns):
            headers.append(normalized_item_path(include.get("Include", "")))
        for compile_item in item_group.findall("ms:ClCompile", ns):
            sources.append(normalized_item_path(compile_item.get("Include", "")))
    return sorted(set(headers)), sorted(set(sources))


def build_expected_lists():
    headers = sorted(set(existing_paths(EXPLICIT_HEADERS) + find_project_files(HEADER_EXTENSIONS)))
    sources = sorted(set(existing_paths(THIRD_PARTY_SOURCES) + find_project_files(SOURCE_EXTENSIONS)))
    return headers, sources


def sync_vcxproj(dry_run=False, check=False, backup=False):
    print(f"[*] Project root: {PROJECT_ROOT}")
    print(f"[*] vcxproj file: {VCXPROJ_PATH}")
    print()

    expected_headers, expected_sources = build_expected_lists()
    print("[*] Scanning project files...")
    print(f"    Found {len(expected_headers)} project headers")
    print(f"    Found {len(expected_sources)} build sources")
    print()

    tree, root, ns = parse_project(VCXPROJ_PATH)

    if check:
        current_headers, current_sources = read_project_file_lists(root, ns)
        missing_headers = sorted(set(expected_headers) - set(current_headers))
        missing_sources = sorted(set(expected_sources) - set(current_sources))
        extra_headers = sorted(set(current_headers) - set(expected_headers))
        extra_sources = sorted(set(current_sources) - set(expected_sources))
        if not (missing_headers or missing_sources or extra_headers or extra_sources):
            print("[+] vcxproj file list is in sync")
            return
        print("[!] vcxproj file list is out of sync")
        for label, values in [
            ("missing headers", missing_headers),
            ("missing sources", missing_sources),
            ("extra headers", extra_headers),
            ("extra sources", extra_sources),
        ]:
            if values:
                print(f"    {label}:")
                for value in values[:20]:
                    print(f"      {value}")
                if len(values) > 20:
                    print(f"      ... and {len(values) - 20} more")
        sys.exit(2)

    if dry_run:
        print("[*] DRY RUN mode - no files will be modified")
        return

    backup_path = backup_vcxproj(VCXPROJ_PATH) if backup else None

    print("[*] Updating vcxproj...")
    remove_generated_file_groups(root, ns)
    clean_resource_items(root, ns)
    insert_file_item_groups(root, ns, expected_headers, expected_sources)

    ET.indent(tree, space="  ")
    tree.write(VCXPROJ_PATH, encoding="utf-8", xml_declaration=True)

    resources = collect_resource_items(root, ns)
    write_filters_file(expected_headers, expected_sources, resources)

    print("[+] vcxproj updated")
    print("[+] filters updated")
    print(f"    Headers shown: {len(expected_headers)}")
    print(f"    Sources shown: {len(expected_sources)}")
    print(f"    Resource items shown: {len(resources)}")
    if backup_path:
        print()
        print(f"[!] Backup created: {backup_path.name}")


def main():
    dry_run = "--dry-run" in sys.argv
    check = "--check" in sys.argv
    backup = "--backup" in sys.argv

    print("=" * 60)
    print("  Ditto Engine - vcxproj Sync Tool")
    print("=" * 60)
    print()

    if not VCXPROJ_PATH.exists():
        print(f"[!] Error: vcxproj not found at {VCXPROJ_PATH}")
        sys.exit(1)

    try:
        sync_vcxproj(dry_run=dry_run, check=check, backup=backup)
        print()
        print("[+] Done!")
    except Exception as exc:
        print(f"[!] Error: {exc}")
        import traceback

        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
