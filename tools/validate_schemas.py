#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验 protocol/schemas/ 下的 JSON Schema 文件。

用法:
    python3 tools/validate_schemas.py            # 在仓库根目录执行
    python3 /abs/path/to/tools/validate_schemas.py

检查项:
    1. 每个 .json 都是合法 JSON;
    2. 必需顶层键存在:$schema / $id / title / description(均非空字符串);
    3. $schema 指向 draft-07,$id 与文件名一致,且全局唯一;
    4. msg-*.schema.json 声明了 type,common.schema.json 声明了 $defs;
    5. 凡是带非空 properties 的对象节点,都必须显式声明 additionalProperties
       (开发期严格校验,见 protocol/schemas/README.md §4.1);
    6. 递归遍历全文件,每个 $ref 指向的本地文件存在,且 #/$defs/... 这一 JSON
       Pointer 在目标文件里可以解析到实际节点。

【重要限制】本机没有 pip,无法安装 jsonschema / check-jsonschema 等第三方库,
所以本脚本 **只做结构完整性与引用可解析性检查**,不做完整的 schema 求值:
    它不会验证某份载荷是否真的满足 type / enum / required / minimum / additionalProperties,
    也不会检查 allOf、oneOf 之类组合关键字的语义。
真正的"载荷合规性"校验需要在有依赖的环境(CI / 开发者机器)用:
    python3 -m jsonschema -i payload.json protocol/schemas/msg-hello.schema.json

退出码:0 = 全部通过;1 = 存在失败项。
"""

import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCHEMA_DIR = REPO_ROOT / "protocol" / "schemas"

DRAFT07 = "http://json-schema.org/draft-07/schema#"
REQUIRED_TOP_KEYS = ("$schema", "$id", "title", "description")


class Report:
    def __init__(self):
        self.errors = []
        self.notes = []

    def error(self, where, msg):
        self.errors.append(f"[FAIL] {where}: {msg}")

    def note(self, msg):
        self.notes.append(msg)


def iter_refs(node, path="#"):
    """递归找出所有 $ref 字符串,附带其 JSON 路径。"""
    if isinstance(node, dict):
        for key, value in node.items():
            here = f"{path}/{key}"
            if key == "$ref" and isinstance(value, str):
                yield here, value
            else:
                yield from iter_refs(value, here)
    elif isinstance(node, list):
        for idx, value in enumerate(node):
            yield from iter_refs(value, f"{path}/{idx}")


def iter_objects(node, path="#"):
    """递归找出所有"带非空 properties 的对象"节点。"""
    if isinstance(node, dict):
        props = node.get("properties")
        if isinstance(props, dict) and props:
            yield path, node
        for key, value in node.items():
            yield from iter_objects(value, f"{path}/{key}")
    elif isinstance(node, list):
        for idx, value in enumerate(node):
            yield from iter_objects(value, f"{path}/{idx}")


def resolve_pointer(doc, fragment):
    """解析 #/a/b 形式的 JSON Pointer(RFC 6901);返回 (ok, value_or_reason)。"""
    if fragment in ("", "#"):
        return True, doc
    ptr = fragment[1:] if fragment.startswith("#") else fragment
    if not ptr.startswith("/"):
        return False, f"不支持的 fragment 形式: {fragment!r}(只支持 # 与 #/... )"
    current = doc
    walked = []
    for raw_token in ptr.split("/")[1:]:
        token = raw_token.replace("~1", "/").replace("~0", "~")
        walked.append(token)
        if isinstance(current, dict):
            if token not in current:
                return False, "JSON Pointer 断在 " + "/".join(walked)
            current = current[token]
        elif isinstance(current, list):
            if not token.isdigit() or int(token) >= len(current):
                return False, "JSON Pointer 数组下标无效 " + "/".join(walked)
            current = current[int(token)]
        else:
            return False, "JSON Pointer 断在 " + "/".join(walked)
    return True, current


def main():
    rep = Report()

    if not SCHEMA_DIR.is_dir():
        print(f"[FAIL] schema 目录不存在: {SCHEMA_DIR}")
        return 1

    files = sorted(p for p in SCHEMA_DIR.glob("*.json"))
    if not files:
        print(f"[FAIL] {SCHEMA_DIR} 下没有 .json 文件")
        return 1

    docs = {}
    for path in files:
        rel = path.name
        try:
            with path.open("r", encoding="utf-8") as fh:
                docs[rel] = json.load(fh)
        except json.JSONDecodeError as exc:
            rep.error(rel, f"不是合法 JSON: {exc}")
        except OSError as exc:  # pragma: no cover
            rep.error(rel, f"无法读取: {exc}")

    # 2/3/4:顶层结构与元数据
    seen_ids = {}
    for rel, doc in docs.items():
        if not isinstance(doc, dict):
            rep.error(rel, "根节点不是 JSON 对象")
            continue
        for key in REQUIRED_TOP_KEYS:
            value = doc.get(key)
            if not isinstance(value, str) or not value.strip():
                rep.error(rel, f"缺少或将空的顶层键: {key}")
        if doc.get("$schema") != DRAFT07:
            rep.error(rel, f"$schema 应为 {DRAFT07!r},实际 {doc.get('$schema')!r}")
        if doc.get("$id") != rel:
            rep.error(rel, f"$id 应与文件名一致: 期望 {rel!r},实际 {doc.get('$id')!r}")
        if doc.get("$id") in seen_ids:
            rep.error(rel, f"$id 与 {seen_ids[doc.get('$id')]} 重复: {doc.get('$id')!r}")
        seen_ids[doc.get("$id")] = rel

        if rel.startswith("msg-"):
            if "type" not in doc:
                rep.error(rel, "消息 schema 必须声明 type")
        if rel == "common.schema.json":
            if not isinstance(doc.get("$defs"), dict) or not doc.get("$defs"):
                rep.error(rel, "common.schema.json 必须包含非空 $defs")

        # 5:显式 additionalProperties
        for where, node in iter_objects(doc):
            if "additionalProperties" not in node:
                rep.error(rel, f"{where} 带非空 properties 但未声明 additionalProperties"
                                f"(开发期严格校验要求;若确为文档未定义字段的开放对象,"
                                f"请在 README §7 登记)")

    # 6:$ref 可解析
    ref_total = 0
    for rel, doc in docs.items():
        for where, ref in iter_refs(doc):
            ref_total += 1
            file_part, _, fragment = ref.partition("#")
            if file_part.startswith(("http://", "https://")):
                rep.error(rel, f"{where} 使用了绝对 URL $ref: {ref}(本仓库要求相对路径)")
                continue
            if file_part == "":
                target_rel = rel
                target_doc = doc
            else:
                target_rel = file_part
                target_doc = docs.get(target_rel)
                if target_doc is None:
                    if (SCHEMA_DIR / target_rel).is_file():
                        rep.error(rel, f"{where} 的 $ref 指向未纳入本次校验的文件: {target_rel}")
                    else:
                        rep.error(rel, f"{where} 的 $ref 指向不存在的文件: {target_rel}")
                    continue
            ok, result = resolve_pointer(target_doc, "#" + fragment)
            if not ok:
                rep.error(rel, f"{where} 的 $ref 无法解析: {ref}({result})")

    # 输出
    for line in rep.errors:
        print(line)
    for line in rep.notes:
        print(f"[INFO] {line}")

    total_defs = 0
    for rel, doc in docs.items():
        if isinstance(doc.get("$defs"), dict):
            total_defs += len(doc["$defs"])

    msg_files = [r for r in docs if r.startswith("msg-")]
    print(f"[INFO] schema 文件 {len(docs)} 个(消息 {len(msg_files)} 个 + common 1 个)")
    print(f"[INFO] $defs 定义 {total_defs} 个,$ref 引用 {ref_total} 处,全部本地相对路径")
    print("[INFO] 仅结构校验(无 jsonschema 依赖,不做 schema 求值)——见脚本 docstring")
    if rep.errors:
        print(f"[FAIL] {len(rep.errors)} 项失败")
        return 1
    print("[OK] 全部通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
