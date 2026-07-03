#!/usr/bin/env python3
"""Flatten Doxygen XML into one compact api.json for the Luminoveau website to render.

Doxygen XML is verbose and split across many files; the site wants a single small file it can
fetch and render without a mini XML-to-HTML engine in the browser. This walks the XML once and
emits:

    {
      "project": "Luminoveau",
      "generated": "2026-...T...Z",
      "groups": [
        { "name": "Window", "kind": "class", "brief": "...",
          "members": [
            { "name": "InitWindow", "kind": "function", "return": "void",
              "args": "(const std::string& title, int width, int height)",
              "signature": "void InitWindow(const std::string& title, int width, int height)",
              "brief": "...", "details": "...",
              "params": [ {"name": "title", "desc": "..."} ],
              "returns": "..." },
            ...
          ] }
      ]
    }

Usage:  python3 doxy2json.py <doxygen-xml-dir> <output.json>
"""

import json
import os
import re
import sys
import datetime
import xml.etree.ElementTree as ET

# Compound kinds we surface as documentation groups.
GROUP_KINDS = {"class", "struct", "namespace", "interface"}
# Member kinds we surface within a group.
MEMBER_KINDS = {"function", "enum", "typedef", "variable"}

# Most engine internals are kept out of the docs at the source:
#   * whole internal subsystems (gpu/, renderer/passes|sdl/, interfaces/, imgui/rmlui integration,
#     shaders_generated.h) are EXCLUDE'd in the Doxyfile, so they never reach the XML;
#   * one-off internal types inside otherwise-public headers are wrapped in /// @cond ... @endcond.
# This short list only covers what we can't annotate: bundled third-party libs and Doxygen's
# anonymous compounds. See doxygen/README.md.
EXCLUDE_GROUPS = [re.compile(p) for p in (
    r"^mINI",      # bundled ini parser (third-party, lives in core/settings)
    r"^msdfgen",   # bundled MSDF font-atlas lib (third-party)
    r"@",          # anonymous/unnamed compounds (Doxygen artifacts)
    # Nested / family implementation types that sit inside otherwise-public headers — cheaper to
    # pattern-match here than to @cond each one.
    r"Proxy$",       # operator[] return helpers (EffectAsset::UniformProxy, UniformBuffer::VariableProxy)
    r"Uniforms$",    # per-pass uniform blocks (Renderer::Uniforms, ...)
    r"Internal$",    # nested ::Internal impl structs (PCMSoundAsset::Internal, ...)
    r"detail$",      # ::detail namespaces (Net::detail)
    r"^GPU",         # GPU-side particle structs living beside the public particle API
    r"Backend$",     # PlatformInputBackend / WindowBackend etc. beside the public Window/Input
    r"RenderPass$",  # ParticleRenderPass beside the public Particles/Draw API
)]


def excluded(name):
    # Match the full name AND the outermost compound, so nested structs of an excluded class
    # (e.g. Model3DRenderPass::LightData) are dropped along with their parent.
    root = name.split("::")[0]
    return any(p.search(name) or p.search(root) for p in EXCLUDE_GROUPS)


# A class is filed under a category derived from where it lives in src/. First matching prefix wins,
# so put the more specific paths first. Rendered as sidebar sections in CATEGORY_ORDER.
CATEGORY_RULES = [
    ("platform/window",  "Windowing"),
    ("platform/input",   "Input"),
    ("platform/audio",   "Audio"),
    ("platform/net",     "Networking"),
    ("assets/audio",     "Audio"),
    ("assets/compute",   "Compute"),
    ("draw/text",        "Text"),
    ("draw/particle",    "Particles"),
    ("draw",             "Drawing"),
    ("scene",            "3D & Scene"),
    ("renderer/compute", "Compute"),
    ("renderer",         "Rendering"),
    ("gpu",              "Rendering"),
    ("assets",           "Assets"),
    ("math",             "Math"),
    ("types",            "Types"),
    ("net",              "Networking"),
    ("integrations",     "Integrations"),
    ("profiler",         "Profiling"),
    ("file",             "Files"),
    ("util",             "Utility"),
    ("core",             "Core"),
    ("app",              "Core"),
    ("config",           "Core"),
]

CATEGORY_ORDER = [
    "Core", "Windowing", "Input", "Drawing", "Text", "Rendering", "Compute",
    "Particles", "3D & Scene", "Audio", "Assets", "Math", "Types", "Networking",
    "Files", "Utility", "Profiling", "Integrations", "Other",
]


def category(path):
    if not path:
        return "Other"
    path = path.replace("\\", "/")
    i = path.find("src/")
    rel = path[i + 4:] if i >= 0 else path      # e.g. "platform/window/window.h"
    for prefix, cat in CATEGORY_RULES:
        if rel.startswith(prefix):
            return cat
    return "Other"


def text(node):
    """All text under a node, whitespace-normalized to a single line."""
    if node is None:
        return ""
    return " ".join("".join(node.itertext()).split())


def para_prose(para):
    """Text of a <para>, excluding the structured <parameterlist>/<simplesect> bits."""
    out = []
    if para.text:
        out.append(para.text)
    for child in para:
        if child.tag == "parameterlist" or (child.tag == "simplesect" and child.get("kind") == "return"):
            if child.tail:
                out.append(child.tail)
            continue
        out.append("".join(child.itertext()))
        if child.tail:
            out.append(child.tail)
    return " ".join("".join(out).split())


def parse_description(desc):
    """Return (prose, params, returns) from a brief/detailed <description> element."""
    if desc is None:
        return "", [], ""
    params, returns, prose_parts = [], "", []
    for para in desc.findall("para"):
        for child in para:
            if child.tag == "parameterlist" and child.get("kind") == "param":
                for item in child.findall("parameteritem"):
                    name = item.find(".//parametername")
                    pdesc = item.find("parameterdescription")
                    params.append({"name": text(name), "desc": text(pdesc)})
            elif child.tag == "simplesect" and child.get("kind") == "return":
                returns = text(child)
        prose = para_prose(para)
        if prose:
            prose_parts.append(prose)
    return "\n\n".join(prose_parts), params, returns


def member(md):
    """Convert one <memberdef> to a dict, or None if we skip it."""
    kind = md.get("kind")
    if kind not in MEMBER_KINDS or md.get("prot") not in (None, "public"):
        return None

    name = text(md.find("name"))
    ret  = text(md.find("type"))
    args = text(md.find("argsstring"))

    brief, _, _        = parse_description(md.find("briefdescription"))
    details, params, returns = parse_description(md.find("detaileddescription"))

    # Prefer the richer <param> declarations when the detailed description didn't list them.
    if not params:
        for p in md.findall("param"):
            pn = text(p.find("declname")) or text(p.find("defname"))
            if pn:
                params.append({"name": pn, "desc": ""})

    out = {"name": name, "kind": kind, "brief": brief, "details": details}
    if kind == "function":
        out["return"]    = ret
        out["args"]      = args
        out["signature"] = " ".join(x for x in [ret, name + args] if x)
        out["params"]    = params
        out["returns"]   = returns
    elif kind == "enum":
        out["values"] = [
            {"name": text(ev.find("name")), "brief": parse_description(ev.find("briefdescription"))[0]}
            for ev in md.findall("enumvalue")
        ]
    elif kind == "typedef":
        out["type"] = ret
        out["args"] = args
    elif kind == "variable":
        out["type"] = ret
    return out


def compound(xml_dir, refid):
    """Read one compound's XML file and return a group dict, or None."""
    try:
        root = ET.parse(f"{xml_dir}/{refid}.xml").getroot()
    except (ET.ParseError, FileNotFoundError):
        return None
    cd = root.find("compounddef")
    if cd is None or cd.get("kind") not in GROUP_KINDS:
        return None

    members = []
    for sec in cd.findall("sectiondef"):
        for md in sec.findall("memberdef"):
            m = member(md)
            if m:
                members.append(m)
    if not members:
        return None

    brief, _, _ = parse_description(cd.find("briefdescription"))
    members.sort(key=lambda m: m["name"].lower())
    loc = cd.find("location")
    return {
        "name": text(cd.find("compoundname")),
        "kind": cd.get("kind"),
        "category": category(loc.get("file") if loc is not None else ""),
        "brief": brief,
        "members": members,
    }


def main():
    if len(sys.argv) != 3:
        print("usage: doxy2json.py <doxygen-xml-dir> <output.json>", file=sys.stderr)
        sys.exit(2)
    xml_dir, out_path = sys.argv[1], sys.argv[2]

    index = ET.parse(f"{xml_dir}/index.xml").getroot()
    groups = []
    for comp in index.findall("compound"):
        if comp.get("kind") not in GROUP_KINDS:
            continue
        name_el = comp.find("name")
        if name_el is not None and excluded(name_el.text or ""):
            continue
        g = compound(xml_dir, comp.get("refid"))
        if g:
            groups.append(g)

    # Nested compounds sometimes have no <location>; inherit the category of their outermost class.
    cat_by_root = {g["name"]: g["category"] for g in groups if g["category"] != "Other"}
    for g in groups:
        if g["category"] == "Other":
            g["category"] = cat_by_root.get(g["name"].split("::")[0], "Other")

    groups.sort(key=lambda g: g["name"].lower())
    # Engine concept doc shown as the Docs landing page (rendered as markdown by the site).
    overview_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "overview.md")
    overview = ""
    if os.path.exists(overview_path):
        with open(overview_path, encoding="utf-8") as f:
            overview = f.read()

    data = {
        "project": "Luminoveau",
        "generated": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "overview": overview,
        "categoryOrder": CATEGORY_ORDER,
        "groups": groups,
    }
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    print(f"doxy2json: wrote {len(groups)} groups, "
          f"{sum(len(g['members']) for g in groups)} members -> {out_path}")


if __name__ == "__main__":
    main()
