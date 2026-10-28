import glob
import os
import re

def filename_to_symbol(filename):
    """
    Converts a filename into a valid C identifier name.
    Example: 'Inter-Regular.ttf' -> 'Inter_Regular_ttf'
             'dejavu_sans_otf.otf' -> 'dejavu_sans_otf_otf'
    """
    symbol = re.sub(r'[^a-zA-Z0-9]', '_', filename)
    if symbol and symbol[0].isdigit():
        symbol = '_' + symbol
    return symbol

def generate_fonts_header():
    # define the directory containing this script (src/ui)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    fonts_dir = os.path.join(script_dir, "fonts")
    out_path = os.path.join(script_dir, "font_data.h")

    # find all font files under the fonts directory
    font_files = sorted(
        glob.glob(os.path.join(fonts_dir, "**", "*.ttf"), recursive=True)
        + glob.glob(os.path.join(fonts_dir, "**", "*.otf"), recursive=True)
        + glob.glob(os.path.join(fonts_dir, "**", "*.ttc"), recursive=True)
    )

    if not font_files:
        print(f"No font files found under {fonts_dir}")
        return

    sections = []
    seen_symbols = {}

    for font_path in font_files:
        filename = os.path.basename(font_path)
        var_name = filename_to_symbol(filename)

        # handle duplicate filenames in different subdirectories safely
        if var_name in seen_symbols:
            rel_dir = os.path.dirname(os.path.relpath(font_path, fonts_dir))
            prefix = filename_to_symbol(rel_dir)
            var_name = f"{prefix}_{var_name}"

        seen_symbols[var_name] = font_path

        with open(font_path, "rb") as f:
            data = f.read()

        hex_bytes = [f"0x{b:02x}" for b in data]
        lines = []
        for i in range(0, len(hex_bytes), 12):
            lines.append("    " + ", ".join(hex_bytes[i:i+12]))

        rel_path = os.path.relpath(font_path, script_dir)
        section = f"// {rel_path} byte array\n" \
                  f"inline const unsigned char {var_name}[] = {{\n" \
                  f"{',\n'.join(lines)}\n" \
                  f"}};\n" \
                  f"inline const unsigned int {var_name}_len = {len(data)};"
        sections.append(section)

    content = f'''#pragma once

// Generated automatically by generate_fonts.py
// Contains raw byte arrays for all font files under src/ui/fonts

{'\n\n'.join(sections)}
'''

    with open(out_path, "w") as f:
        f.write(content)

    print(f"Generated {out_path} with {len(font_files)} fonts.")

if __name__ == "__main__":
    generate_fonts_header()
