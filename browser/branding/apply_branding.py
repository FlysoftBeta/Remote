#!/usr/bin/env python3
"""Generate UI branding from committed resources without changing XML metadata.

Only complete allowlisted message bodies and their XTB translations are changed.
The checkout's GRIT fingerprint function keeps translated message IDs in sync.
Generated files stay uncommitted; export rejects resource changes in patch commits.
"""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args])


def generate(root, config):
    sys.path.insert(0, str(root / "tools/grit"))
    from grit.extern.tclib import GenerateMessageId

    deny = config["denylist"]
    source = re.compile(r"\b" + re.escape(config["source"]) + r"\b")
    protected = "|".join(deny["protected_tokens"])
    elements = "|".join(
        rf"<{re.escape(tag)}\b[^>]*(?:/>|>.*?</{re.escape(tag)}>)"
        for tag in deny["preserve_elements"]
    )
    # Keep placeholders (including examples), tags, URLs and machine tokens intact.
    tokens = re.compile(
        rf"({elements}|<[^>]+>|{protected})",
        re.DOTALL,
    )

    def replace(body):
        return "".join(
            part if i % 2 else source.sub(config["brand"], part)
            for i, part in enumerate(tokens.split(body))
        )

    def message_id(element):
        # This is GRIT's presentable content: placeholder names replace <ph>.
        text = element.text or ""
        for child in element:
            if child.tag != "ph":
                raise ValueError(f"Unexpected message child: {child.tag}")
            text += child.attrib["name"].upper() + (child.tail or "")
        if element.get("use_name_for_id") == "true":
            return element.attrib["name"]
        text = text.strip()
        if text.startswith("'''"):
            text = text[3:]
        if text.endswith("'''"):
            text = text[:-3]
        return GenerateMessageId(text.strip(), element.get("meaning", ""))

    message_pattern = re.compile(r"(<message\b[^>]*>)(.*?)(</message>)", re.DOTALL)
    translation_pattern = re.compile(
        r'(<translation\b[^>]*\bid="([^"]+)"[^>]*>)(.*?)(</translation>)',
        re.DOTALL,
    )
    deny_names = set(deny["message_names"])
    deny_pattern = re.compile("|".join(deny["message_name_patterns"]))
    result = {}
    mapping = {}
    preserved_ids = set()
    samples = []

    for relative in config["allowlist"]["messages"]:
        original = git(root, "show", f"HEAD:{relative}")
        count = 0

        def message(match):
            nonlocal count
            element = ET.fromstring(match[0])
            name = element.attrib["name"]
            old_id = message_id(element)
            if name in deny_names or deny_pattern.search(name):
                preserved_ids.add(old_id)
                return match[0]
            body = replace(match[2])
            if body == match[2]:
                preserved_ids.add(old_id)
                return match[0]
            changed = match[1] + body + match[3]
            new_id = message_id(ET.fromstring(changed))
            if old_id in mapping and mapping[old_id] != new_id:
                raise ValueError(f"Conflicting translation mapping: {name}")
            mapping[old_id] = new_id
            count += 1
            if len(samples) < 5:
                samples.append({"name": name, "before": match[2].strip(), "after": body.strip()})
            return changed

        generated = message_pattern.sub(message, original.decode("utf-8")).encode("utf-8")
        result[relative] = (original, generated, count)

    for pattern in config["allowlist"]["translations"]:
        files = git(root, "ls-files", pattern).decode("utf-8").splitlines()
        if not files:
            raise ValueError(f"No resources matched {pattern}")
        for relative in files:
            original = git(root, "show", f"HEAD:{relative}")
            count = 0

            def translation(match):
                nonlocal count
                if match[2] not in mapping:
                    return match[0]
                opening = match[1].replace(f'id="{match[2]}"', f'id="{mapping[match[2]]}"')
                changed = opening + replace(match[3]) + match[4]
                count += changed != match[0]
                # Identical source text can be shared by a denied and a UI message.
                if match[2] in preserved_ids and mapping[match[2]] != match[2]:
                    return match[0] + "\n" + changed
                return changed

            generated = translation_pattern.sub(translation, original.decode("utf-8")).encode("utf-8")
            result[relative] = (original, generated, count)
    return result, samples


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--src", type=Path, default=Path("C:/src/chromium/src"))
    parser.add_argument("--config", type=Path, default=Path(__file__).with_name("config.json"))
    parser.add_argument("--restore", action="store_true", help="restore generated resources to HEAD")
    parser.add_argument("--check-committed", action="store_true", help="reject generated resource edits in upstream..HEAD")
    parser.add_argument("--report", type=Path, help="write per-file counts and samples as JSON")
    args = parser.parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8"))
    if args.check_committed:
        paths = config["allowlist"]["messages"] + config["allowlist"]["translations"]
        changed = git(args.src, "log", "--format=", "--name-only", "upstream..rb", "--", *paths)
        if changed:
            raise RuntimeError("Resource edits must remain generated, outside patch commits:\n" + changed.decode())
        return
    result, samples = generate(args.src, config)
    # Validate every file before writing any: never overwrite a manual resource edit.
    for relative, (original, generated, _) in result.items():
        if (args.src / relative).read_bytes() not in (original, generated):
            raise RuntimeError(f"Manual changes in {relative}; save or commit them before branding")
    written = 0
    for relative, (original, generated, count) in result.items():
        path = args.src / relative
        desired = original if args.restore else generated
        if path.read_bytes() != desired:
            path.write_bytes(desired)
            written += 1
        if count:
            print(f"{relative}: {count} messages")
    report = {"mode": "restore" if args.restore else "brand", "files_written": written,
              "messages_per_file": {p: item[2] for p, item in result.items()}, "samples": samples}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{'Restored' if args.restore else 'Branded'} resources; {written} files written")


if __name__ == "__main__":
    main()
