#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验 protocol/schemas/ 下的 JSON Schema 文件。

用法:
    python3 tools/validate_schemas.py            # 在仓库根目录执行
    python3 /abs/path/to/tools/validate_schemas.py

检查项(全部只用标准库,任何环境都能跑):
    1. 每个 .json 都是合法 JSON;
    2. 必需顶层键存在:$schema / $id / title / description(均非空字符串);
    3. $schema 指向 draft-07,$id 与文件名一致且全局唯一;
       draft-07 的关键字是 `definitions`,**不允许**再出现 2019-09 的 `$defs`
       (历史上混用过,已统一;见 protocol/schemas/README.md §6.2);
    4. msg-*.schema.json 声明了 type,common.schema.json 声明了非空 definitions;
    5. 凡是带非空 properties 的对象节点,都必须显式声明 additionalProperties
       (开发期严格校验,见 protocol/schemas/README.md §4);
    6. `required` 里的每个名字都必须能落到「有效 properties」上:
       本节点 properties ∪ 同节点 $ref 目标 properties ∪ allOf 子项 properties ∪
       父级 schema 的 properties(用于 oneOf/anyOf/then/else 分支,如 TX_REQUEST
       两阶段模型)。这条能抓住「required 里拼错/凭空多出一个字段」的错误;
    7. 每个 enum 非空且无重复值;
    8. 递归遍历全文件,每个 $ref 指向的本地文件存在,且 #/definitions/... 这一
       JSON Pointer 在目标文件里可以解析到实际节点;统计绝对 URL $ref 数量并如实打印。

附加检查(依赖可用性自动启用,不强制安装):
    9. 若 `import jsonschema` 成功:对 37 个 schema 跑 Draft7 check_schema,
       再用 `protocol/schemas/fixtures/` 的真实载荷(取固件/契约的实际字段形态)
       做正例求值,并跑内置负例(必须被拒绝)——这才算真正的「载荷合规性」;
       若本机没有 jsonschema,跳过这一步并明确说明,结构校验仍照常执行。
   10. 若 protocol/schemas/README.md 顶部登记了契约 sha256,与
       docs/04-protocol.md 的实际 sha256 比对(契约一改就必须同步 schema 镜像)。

【重要口径】结构校验不等于 schema 求值。历史上本脚本的 docstring/README 曾写
「本机没有 pip、没有 jsonschema」,这是**事实错误**(本机有 jsonschema 4.x)。
现在策略是「机会式校验」:库在就跑真求值,库不在就只跑结构检查并如实降级说明。
需要手工跑单条载荷时,跨文件 $ref 必须给 base URI(裸相对 $id 否则会被当成
无法抓取的 URL):
    cd protocol/schemas && python3 -m jsonschema --base-uri "file://$PWD/" \\
        -i payload.json msg-caps.schema.json

退出码:0 = 全部通过;1 = 存在失败项。
"""

import hashlib
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCHEMA_DIR = REPO_ROOT / "protocol" / "schemas"
FIXTURE_DIR = SCHEMA_DIR / "fixtures"
README = SCHEMA_DIR / "README.md"
PROTOCOL_DOC = REPO_ROOT / "docs" / "04-protocol.md"

DRAFT07 = "http://json-schema.org/draft-07/schema#"
REQUIRED_TOP_KEYS = ("$schema", "$id", "title", "description")

# fixture 文件 → 用于求值的消息 schema
FIXTURES = {
    "hello.json": "msg-hello.schema.json",
    "hello_pairing.json": "msg-hello.schema.json",
    "hello_ack.json": "msg-hello_ack.schema.json",
    "hello_ack_first_bond.json": "msg-hello_ack.schema.json",
    "hello_ack_already_bound.json": "msg-hello_ack.schema.json",
    "caps.json": "msg-caps.schema.json",
    "ping.json": "msg-ping.schema.json",
    "pong.json": "msg-pong.schema.json",
    "error.json": "msg-error.schema.json",
    "tx_request_stage1.json": "msg-tx_request.schema.json",
    "tx_request_stage2.json": "msg-tx_request.schema.json",
}

# 内置负例:(schema 文件, 载荷, 为什么必须被拒绝)
NEGATIVE_CASES = [
    ("msg-caps.schema.json", {"proto": 1, "fw": "0.2.0", "device_id": "X",
                              "active_slot": 9, "modules": [], "policy": {
                                  "tx_whitelist": [], "tx_power_cap_dbm": 20,
                                  "tx_requires_token": True},
                              "limits": {"ble_frag_total_max": 15,
                                         "ble_frame_max": 3600,
                                         "usb_frame_max": 16384,
                                         "max_edges": 16384,
                                         "stream_rate_hz": 50,
                                         "arm_timeout_ms": 5000},
                              "transports": ["usb"], "active_transport": "usb"},
     "active_slot 超出 1..3"),
    ("msg-tx_request.schema.json", {"armed_ack": True, "reason": "sneaky"},
     "第二阶段只允许 {armed_ack:true},不得夹带第一阶段字段"),
    ("msg-tx_request.schema.json",
     {"f_hz": 433920000, "mod": "ook", "payload_rle": "AQIDBA", "repeats": 3,
      "power_dbm": 10, "reason": "x", "armed_ack": True},
     "两阶段形态互斥,不能混在一起"),
    ("msg-ping.schema.json", {"t_ms": "not-a-number"}, "t_ms 必须是 number"),
]


class Report:
    def __init__(self):
        self.errors = []
        self.notes = []
        self.warns = []

    def error(self, where, msg):
        self.errors.append(f"[FAIL] {where}: {msg}")

    def note(self, msg):
        self.notes.append(msg)

    def warn(self, msg):
        self.warns.append(f"[WARN] {msg}")


def iter_nodes(node, path="#"):
    """递归产出 (JSON 路径, dict 节点)。"""
    if isinstance(node, dict):
        yield path, node
        for key, value in node.items():
            yield from iter_nodes(value, f"{path}/{key}")
    elif isinstance(node, list):
        for idx, value in enumerate(node):
            yield from iter_nodes(value, f"{path}/{idx}")


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


def iter_objects(node, path="#", combinator_branch=False):
    """递归找出所有「带非空 properties 的对象」节点。

    `oneOf`/`anyOf`/`allOf`/`then`/`else` 的分支**不要求**自己声明
    additionalProperties:分支与父 schema 校验同一个实例,分支里写
    additionalProperties:false 会把父级 properties 全当成"额外属性"
    (README §6.2 对 allOf 的警告对 oneOf 同理)。因此跳过对分支节点本身的
    检查,但分支内部的嵌套 properties 仍会被检查。
    """
    if isinstance(node, dict):
        props = node.get("properties")
        if isinstance(props, dict) and props and not combinator_branch:
            yield path, node
        for key, value in node.items():
            here = f"{path}/{key}"
            if key in ("oneOf", "anyOf", "allOf") and isinstance(value, list):
                for idx, branch in enumerate(value):
                    yield from iter_objects(branch, f"{here}/{idx}", True)
            elif key in ("then", "else") and isinstance(value, dict):
                yield from iter_objects(value, here, True)
            else:
                yield from iter_objects(value, here, False)
    elif isinstance(node, list):
        for idx, value in enumerate(node):
            yield from iter_objects(value, f"{path}/{idx}", False)


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


def split_ref(ref):
    file_part, _, fragment = ref.partition("#")
    return file_part, "#" + fragment


def resolve_ref(ref, rel, docs):
    """把 $ref 解析成 (目标文件, 目标节点);失败返回 (None, 原因)。"""
    file_part, fragment = split_ref(ref)
    target_rel = rel if file_part == "" else file_part
    target_doc = docs.get(target_rel)
    if target_doc is None:
        return None, f"目标文件不存在或未纳入校验: {target_rel}"
    ok, value = resolve_pointer(target_doc, fragment)
    if not ok:
        return None, str(value)
    return target_rel, value


def effective_props(node, rel, docs, inherited=frozenset(), seen=frozenset()):
    """一个 schema 对象「有效 properties」的名字集合。

    覆盖 oneOf/then/else 分支需要引用父 schema properties 的场景(如 TX_REQUEST)。
    只解析 $ref 指向的 definitions 节点,避免无限递归。
    """
    props = set(inherited)
    own = node.get("properties")
    if isinstance(own, dict):
        props |= set(own)

    ref = node.get("$ref")
    if isinstance(ref, str):
        key = (rel, ref)
        if key not in seen:
            target = resolve_ref(ref, rel, docs)
            if target[0] is not None:
                props |= effective_props(target[1], target[0], docs,
                                         frozenset(), seen | {key})

    for keyword in ("allOf", "anyOf", "oneOf"):
        branches = node.get(keyword)
        if isinstance(branches, list):
            for branch in branches:
                if isinstance(branch, dict):
                    props |= effective_props(branch, rel, docs, frozenset(), seen)
    return props


def check_required(rep, rel, doc, docs):
    """required ⊆ 有效 properties(含 oneOf 分支继承父级 properties)。"""
    def walk(node, inherited, path):
        if not isinstance(node, dict):
            return
        own = effective_props(node, rel, docs, inherited)
        required = node.get("required")
        if isinstance(required, list):
            for name in required:
                if name not in own:
                    rep.error(rel, f"{path}/required 里的 {name!r} 不在有效 properties 中"
                                    f"(本节点/ $ref / allOf / 父级 properties 均未声明)")
        for keyword in ("oneOf", "anyOf", "allOf", "then", "else"):
            branches = node.get(keyword)
            if isinstance(branches, list):
                for i, branch in enumerate(branches):
                    walk(branch, frozenset(own), f"{path}/{keyword}/{i}")
            elif keyword in ("then", "else") and isinstance(branches, dict):
                walk(branches, frozenset(own), f"{path}/{keyword}")
        props = node.get("properties")
        if isinstance(props, dict):
            for name, sub in props.items():
                # 子对象校验的是实例的下一层,不继承父级 properties
                walk(sub, frozenset(), f"{path}/properties/{name}")

    walk(doc, frozenset(), "#")


def check_enums(rep, rel, doc):
    for path, node in iter_nodes(doc):
        enum = node.get("enum")
        if enum is None:
            continue
        if not isinstance(enum, list) or not enum:
            rep.error(rel, f"{path}/enum 必须是非空数组")
            continue
        seen = set()
        for item in enum:
            marker = json.dumps(item, sort_keys=True, ensure_ascii=False)
            if marker in seen:
                rep.error(rel, f"{path}/enum 含重复值: {marker}")
            seen.add(marker)


def try_json_schema(rep, docs):
    """机会式真求值:jsonschema 在就校验 fixtures + 负例,不在就如实跳过。"""
    try:
        import jsonschema
        from jsonschema.validators import validator_for
    except ImportError:
        rep.note("载荷求值:未安装 jsonschema,已跳过(结构校验仍已完成;"
                 "安装 `python3 -m pip install jsonschema` 后自动启用)")
        return

    version = "unknown"
    try:
        import importlib.metadata as _md
        version = _md.version("jsonschema")
    except Exception:  # pragma: no cover - 仅用于打印
        try:
            version = jsonschema.__version__  # type: ignore[attr-defined]
        except Exception:
            pass

    registry = None
    try:
        from referencing import Registry, Resource
        registry = Registry().with_resources(
            [(d["$id"], Resource.from_contents(d)) for d in docs.values()])
    except Exception:
        registry = None  # 退回默认解析;纯本地 schema 仍可能可用

    def validator_for_doc(schema):
        cls = validator_for(schema)
        if registry is not None:
            return cls(schema, registry=registry)
        return cls(schema)

    # 1) 元 schema 自检
    for rel, doc in docs.items():
        try:
            validator_for(doc).check_schema(doc)
        except Exception as exc:  # jsonschema.SchemaError
            rep.error(rel, f"元 schema 校验失败: {exc.__class__.__name__}: {exc}")

    # 2) fixtures 正例
    if not FIXTURE_DIR.is_dir():
        rep.error("protocol/schemas/fixtures", "fixtures 目录不存在")
        return
    got = 0
    for name, schema_rel in FIXTURES.items():
        path = FIXTURE_DIR / name
        if not path.is_file():
            rep.error(str(path.relative_to(REPO_ROOT)), "登记在 FIXTURES 里的样例文件不存在")
            continue
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            rep.error(str(path.relative_to(REPO_ROOT)), f"不是合法 JSON: {exc}")
            continue
        errs = list(validator_for_doc(docs[schema_rel]).iter_errors(payload))
        if errs:
            rep.error(str(path.relative_to(REPO_ROOT)),
                      f"正例未通过 {schema_rel}: {errs[0].message}")
        got += 1

    # 3) 负例必须被拒绝
    for schema_rel, payload, why in NEGATIVE_CASES:
        errs = list(validator_for_doc(docs[schema_rel]).iter_errors(payload))
        if not errs:
            rep.error(schema_rel, f"负例竟然通过了(应被拒绝):{why}")
        got += 1

    rep.note(f"载荷求值:jsonschema {version} 可用 → {len(FIXTURES)} 个正例 + "
             f"{len(NEGATIVE_CASES)} 个负例,共 {got} 条(真 Draft7 求值)")


def check_contract_sha(rep):
    """README 顶部登记的契约 sha256 必须与 docs/04-protocol.md 实际一致。"""
    if not README.is_file() or not PROTOCOL_DOC.is_file():
        rep.warn("找不到 protocol/schemas/README.md 或 docs/04-protocol.md,跳过契约 sha256 比对")
        return
    text = README.read_text(encoding="utf-8")
    m = re.search(r"契约文档 sha256:`?([0-9a-fA-F]{64})`?", text[:1500])
    if not m:
        rep.warn("README 顶部未登记契约文档 sha256,跳过比对")
        return
    want = m.group(1).lower()
    have = hashlib.sha256(PROTOCOL_DOC.read_bytes()).hexdigest()
    if want != have:
        rep.error("protocol/schemas/README.md",
                  f"顶部登记的契约 sha256 与 docs/04-protocol.md 不一致 "
                  f"(登记 {want[:12]}…,实际 {have[:12]}…)。契约已变更 ⇒ 按 §4 同步 schema,"
                  f"并更新 README 顶部 sha256")
    else:
        rep.note(f"契约 sha256 与 docs/04-protocol.md 一致({have[:12]}…)")


def main():
    rep = Report()

    if not SCHEMA_DIR.is_dir():
        print(f"[FAIL] schema 目录不存在: {SCHEMA_DIR}")
        return 1

    files = sorted(SCHEMA_DIR.glob("*.json"))
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
            if not isinstance(doc.get("definitions"), dict) or not doc.get("definitions"):
                rep.error(rel, "common.schema.json 必须包含非空 definitions")

        # draft-07 只认 definitions,不接受 2019-09 的 $defs
        for path, node in iter_nodes(doc):
            if "$defs" in node:
                rep.error(rel, f"{path} 使用了 2019-09 关键字 $defs;draft-07 应为 definitions")
        # 5:显式 additionalProperties
        for where, node in iter_objects(doc):
            if "additionalProperties" not in node:
                rep.error(rel, f"{where} 带非空 properties 但未声明 additionalProperties"
                                f"(开发期严格校验要求;若确为文档未定义字段的开放对象,"
                                f"请在 README §7 登记)")
        # 6/7:required 与 enum
        check_required(rep, rel, doc, docs)
        check_enums(rep, rel, doc)

    # 8:$ref 可解析,并如实统计
    ref_total = 0
    abs_refs = 0
    for rel, doc in docs.items():
        for where, ref in iter_refs(doc):
            ref_total += 1
            file_part, _ = split_ref(ref)
            if file_part.startswith(("http://", "https://")):
                abs_refs += 1
                rep.error(rel, f"{where} 使用了绝对 URL $ref: {ref}(本仓库要求相对路径)")
                continue
            if file_part and file_part not in docs:
                target_path = SCHEMA_DIR / file_part
                if target_path.is_file():
                    rep.error(rel, f"{where} 的 $ref 指向未纳入本次校验的文件: {file_part}")
                else:
                    rep.error(rel, f"{where} 的 $ref 指向不存在的文件: {file_part}")
                continue
            target_rel, target = resolve_ref(ref, rel, docs)
            if target_rel is None:
                rep.error(rel, f"{where} 的 $ref 无法解析: {ref}({target})")
            elif not isinstance(target, dict):
                rep.error(rel, f"{where} 的 $ref 未指向 schema 对象: {ref}")

    # 9/10:机会式真求值 + 契约 sha256
    try_json_schema(rep, docs)
    check_contract_sha(rep)

    # 输出
    for line in rep.errors:
        print(line)
    for line in rep.warns:
        print(line)
    for line in rep.notes:
        print(f"[INFO] {line}")

    total_defs = 0
    for doc in docs.values():
        if isinstance(doc.get("definitions"), dict):
            total_defs += len(doc["definitions"])
    msg_files = [r for r in docs if r.startswith("msg-")]
    fixture_files = sorted(p.name for p in FIXTURE_DIR.glob("*.json")) if FIXTURE_DIR.is_dir() else []

    print(f"[INFO] schema 文件 {len(docs)} 个(消息 {len(msg_files)} 个 + common 1 个)")
    print(f"[INFO] definitions 定义 {total_defs} 个,$ref 引用 {ref_total} 处"
          f"({'全部本地相对路径' if abs_refs == 0 else f'其中 {abs_refs} 处绝对 URL,见上'})")
    print(f"[INFO] fixtures {len(fixture_files)} 个,内置负例 {len(NEGATIVE_CASES)} 个")
    print("[INFO] 结构校验:JSON/元数据/required⊆properties/enum/$ref 可解析 —— 始终执行")
    if rep.errors:
        print(f"[FAIL] {len(rep.errors)} 项失败")
        return 1
    print("[OK] 全部通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
