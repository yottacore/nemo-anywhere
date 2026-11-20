#!/usr/bin/env python3
"""Write the C table of settings search aliases, from the "# aliases:" rows in
the schema. Usage: gen-settings-aliases.py <schema.shcl> <out.h>

The row is a comment because SHCL refuses a schema key it doesn't know, and the
installed schema has to stay one `shcl check --schema` takes."""

import sys

ROW = "# aliases:"


def split_aliases(text: str, where: str) -> list[str]:
    text = text.strip()
    if text.startswith("[") and text.endswith("]"):
        text = text[1:-1]
    out: list[str] = []
    word = ""
    quoted = False
    for ch in text:
        if ch == '"':
            quoted = not quoted
        elif ch == "," and not quoted:
            out.append(word.strip())
            word = ""
        else:
            word += ch
    if quoted:
        sys.exit(f"{where}: unclosed quote")
    out.append(word.strip())
    for alias in out:
        if not alias:
            sys.exit(f"{where}: empty alias")
        if not alias.isprintable() or "\\" in alias:
            sys.exit(f"{where}: alias {alias!r} has a character the table can't take")
    return out


def read_schema(path: str) -> list[tuple[str, list[str]]]:
    found: list[tuple[str, list[str]]] = []
    field = None
    with open(path, encoding="utf-8") as schema:
        for num, raw in enumerate(schema, 1):
            line = raw.rstrip("\r\n")
            where = f"{path}:{num}"
            if line.startswith("field: "):
                field = line[len("field: "):].strip()
            elif line.strip() == "":
                continue
            elif not line.startswith("\t"):
                field = None
            elif line.strip().startswith(ROW):
                if field is None:
                    sys.exit(f"{where}: aliases outside a field")
                if found and found[-1][0] == field:
                    sys.exit(f"{where}: {field} has 2 alias rows")
                found.append((field, split_aliases(line.strip()[len(ROW):], where)))
    return found


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit("usage: gen-settings-aliases.py <schema.shcl> <out.h>")
    table = read_schema(sys.argv[1])

    lines = ["/* Made at build time from the schema's alias rows, by gen-settings-aliases.py. */", ""]
    for i, (_field, aliases) in enumerate(table):
        quoted = ", ".join(f'"{alias}"' for alias in aliases)
        lines.append(f"static const char *const settings_aliases_{i}[] = {{ {quoted}, NULL }};")
    lines.append("")
    lines.append("static const struct {")
    lines.append("\tconst char        *key;")
    lines.append("\tconst char *const *aliases;")
    lines.append("} settings_aliases[] = {")
    for i, (field, _aliases) in enumerate(table):
        lines.append(f'\t{{ "{field}", settings_aliases_{i} }},')
    lines.append("\t{ NULL, NULL }")
    lines.append("};")

    with open(sys.argv[2], "w", encoding="utf-8", newline="\n") as out:
        out.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
