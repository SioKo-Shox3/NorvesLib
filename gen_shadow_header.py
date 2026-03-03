import os
import sys

shader_dir = r"c:\Users\KINGkawamura\Documents\NorvesLib\Library\Core\Private\Rendering\Shaders"

def spv_to_header(spv_path, array_name):
    with open(spv_path, "rb") as f:
        data = f.read()
    size = len(data)
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        line = ", ".join("0x{:02x}".format(b) for b in chunk)
        lines.append("        " + line)
    body = ",\n".join(lines)
    return "    // {} SPIR-V bytecode ({} bytes)\n    static const uint8_t {}[] = {{\n{}\n    }};\n".format(
        array_name, size, array_name, body
    )

header = "#pragma once\n\n#include <cstdint>\n\nnamespace NorvesLib::Core::Rendering\n{\n\n"
header += spv_to_header(os.path.join(shader_dir, "shadow.vert.spv"), "ShadowVertexShaderSpirV")
header += "\n"
header += spv_to_header(os.path.join(shader_dir, "shadow.frag.spv"), "ShadowFragmentShaderSpirV")
header += "\n} // namespace NorvesLib::Core::Rendering\n"

out_path = os.path.join(shader_dir, "ShadowMapShaders.h")
os.makedirs(os.path.dirname(out_path), exist_ok=True)
with open(out_path, "w", encoding="utf-8") as f:
    f.write(header)

print("ShadowMapShaders.h generated successfully at: " + out_path)
