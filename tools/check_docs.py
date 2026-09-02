#!/usr/bin/env python3
"""Validate HackyLens documentation governance contracts."""

from __future__ import annotations

from datetime import date
from pathlib import Path
import re
import sys
from typing import Iterable, NamedTuple
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]

SEMVER_RE = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$"
)
RELEASE_SEMVER_RE = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
CONTRACT_ID_RE = re.compile(r"^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*$")
OWNER_RE = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
ENCODED_MAJOR_RE = re.compile(r"^(0|[1-9]\d*)$")
ADR_FILE_RE = re.compile(r"^(\d{4})-[a-z0-9]+(?:-[a-z0-9]+)*\.md$")
MARKDOWN_LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
REFERENCE_LINK_RE = re.compile(r"!?\[([^\]]*)\]\[([^\]]*)\]")
REFERENCE_DEFINITION_RE = re.compile(r"^\s{0,3}\[([^\]]+)\]:\s*(.+?)\s*$")
ADR_REFERENCE_RE = re.compile(r"^\d{4}$")
HEADING_RE = re.compile(r"^\s{0,3}(#{1,6})\s+(.+?)\s*#*\s*$")
COMPATIBILITY_RANGE_RE = re.compile(
    r"^>=((?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)),"
    r"<((?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*))$"
)

STABILITIES = {"experimental", "stable", "deprecated"}

TECHNICAL_CONTRACTS = (
    Path("docs/HMPY_PROTOCOL.md"),
    Path("docs/MICROPYTHON_API.md"),
    Path("docs/EXTERNAL_LINK_PROTOCOL.md"),
    Path("docs/APP_LIFECYCLE.md"),
    Path("docs/AI_MODELS.md"),
)

ENTRY_DOCUMENTS = (
    Path("README.md"),
    Path("docs/ARCHITECTURE.md"),
    Path("docs/ARCHITECTURE_VISION.md"),
    Path("docs/CURRENT_STATE.md"),
    Path("docs/ROADMAP.md"),
    Path("docs/MODULES.md"),
)

CLAIM_SURFACES = (
    Path("README.md"),
    Path("docs/ARCHITECTURE.md"),
    Path("docs/MODULES.md"),
)

PREVIEW_MARKER = (
    "hackylens v0.4 is a layered k210 reference firmware and micropython "
    "technology preview"
)

FORBIDDEN_CLAIMS = (
    "openmv-class",
    "hardware-independent platform",
    "hardware-independent robotics platform",
    "open application standard",
)

CLAIM_QUALIFIERS = (
    "candidate",
    "target",
    "roadmap goal",
    "not yet",
    "does not yet",
    "not demonstrated",
)

ADR_SECTIONS = (
    "Context",
    "Decision",
    "Alternatives",
    "Consequences",
    "Compatibility and Migration",
    "Evidence",
    "References",
)

PHASE3_CONTRACTS = {
    "hackylens.app-runtime": {
        "path": Path("docs/spec/APP_RUNTIME.md"),
        "compatibility": {
            "compatibility-app-manifest": "hackylens.native-app-manifest",
            "compatibility-capability-api": "hackylens.capability-api",
        },
    },
    "hackylens.native-app-manifest": {
        "path": Path("docs/spec/APP_MANIFEST.md"),
        "compatibility": {
            "compatibility-app-runtime": "hackylens.app-runtime",
            "compatibility-capability-api": "hackylens.capability-api",
        },
    },
    "hackylens.feature-app-sdk": {
        "path": Path("docs/spec/APP_SDK.md"),
        "compatibility": {
            "compatibility-app-runtime": "hackylens.app-runtime",
            "compatibility-app-manifest": "hackylens.native-app-manifest",
            "compatibility-capability-api": "hackylens.capability-api",
        },
    },
}

PHASE4_SCHEMA_PATTERNS = (
    re.compile(r"(?m)^\s*\[runtime\]\s*$"),
    re.compile(r"(?m)^\s*runtime\s*=\s*[\"']micropython[\"']\s*$"),
    re.compile(r"(?m)^\s*entry\s*=\s*[\"']main\.py[\"']\s*$"),
    re.compile(r"(?m)^\s*heap_bytes\s*="),
    re.compile(r"(?m)^\s*(?:dynamic_loading|program_manager|project_format|"
               r"ide_workspace|runtime_toml_parser)\s*=\s*true\s*$"),
    re.compile(r"(?i)firmware\s+(?:must|shall)\s+parse\s+app\.toml\s+at\s+runtime"),
)

TEARDOWN_DEADLINE_REQUIREMENTS = {
    Path("docs/spec/APP_RUNTIME.md"): (
        (
            r"creates\s+exactly\s+one\s+finite\s+absolute\s+monotonic\s+"
            r"teardown\s+deadline",
            "App Runtime must define the single teardown-deadline origin",
        ),
        (
            r"same\s+composed\s+public\s+Time\s+Capability\s+provider",
            "App Runtime teardown must reuse the public Time provider",
        ),
        (
            r"hk_time_deadline_after_us[\s\S]*exactly\s+once",
            "App Runtime must derive the deadline exactly once through Time API",
        ),
        (
            r"hk_app_context_teardown_deadline",
            "App Runtime must expose teardown deadline through app context",
        ),
        (
            r"MUST\s+NOT\s+be\s+refreshed\s+between\s+stages[\s\S]*providers",
            "App Runtime must prohibit stage/provider deadline refresh",
        ),
        (
            r"owner-wide\s+cleanup\s+MUST\s+still\s+be\s+attempted[\s\S]*"
            r"same\s+already-expired\s+absolute\s+deadline",
            "expired app cleanup must not skip owner-wide cleanup",
        ),
    ),
    Path("docs/spec/APP_SDK.md"): (
        (
            r"hk_app_context_teardown_deadline",
            "Feature App SDK must expose the teardown-deadline accessor",
        ),
        (
            r"Repeated\s+calls\s+return\s+the\s+same[\s\S]*MUST\s+NOT\s+"
            r"refresh",
            "Feature App SDK accessor must preserve the no-refresh rule",
        ),
        (
            r"already-expired\s+value[\s\S]*later\s+owner-wide\s+cleanup",
            "Feature App SDK must preserve expired deadline propagation",
        ),
    ),
    Path("docs/adr/0007-adopt-generation-checked-app-lifecycle.md"): (
        (
            r"creates\s+one\s+finite\s+absolute\s+monotonic\s+teardown\s+"
            r"deadline",
            "ADR-0007 must record the teardown-deadline decision",
        ),
        (
            r"never\s+refreshed\s+per\s+stage,\s+provider",
            "ADR-0007 must record the no-refresh decision",
        ),
    ),
}

APP_RUNTIME_INTEGRATION_REQUIREMENTS = {
    Path("docs/spec/APP_RUNTIME.md"): (
        (
            r"exactly\s+five\s+kinds[\s\S]*Input[\s\S]*SD/media[\s\S]*"
            r"timer[\s\S]*runtime\s+close[\s\S]*app-private\s+wakeup",
            "App Runtime must keep the bounded five-kind event model",
        ),
        (
            r"existing\s+`hk_input_event_t`[\s\S]*MUST\s+NOT\s+sample\s+a\s+"
            r"second\s+button\s+path",
            "v2 input must use the existing Input event path",
        ),
        (
            r"BACK\s+is\s+runtime\s+navigation[\s\S]*consumed[\s\S]*not\s+"
            r"delivered\s+as\s+an\s+ordinary\s+Input\s+event",
            "BACK must remain runtime-consumed navigation",
        ),
        (
            r"strictly\s+increasing\s+runtime\s+sequence[\s\S]*restarts\s+"
            r"only\s+for\s+a\s+newly\s+launched\s+generation",
            "app events must remain ordered per generation",
        ),
        (
            r"There\s+is\s+no\s+catch-up\s+loop[\s\S]*completion_now\s*\+\s*"
            r"tick_interval_us",
            "tick scheduling must remain bounded without catch-up",
        ),
        (
            r"at\s+most\s+eight\s+fixed-capacity\s+dirty\s+rectangles"
            r"[\s\S]*Runtime\s+alone\s+presents\s+or\s+aborts",
            "render invalidation and Display ownership must remain bounded",
        ),
        (
            r"one\s+fixed-capacity\s+foreground\s+switch[\s\S]*menu\s+"
            r"selection[\s\S]*BACK[\s\S]*autostart[\s\S]*debug-forced"
            r"[\s\S]*safe-mode",
            "all foreground entry paths must share one switch algorithm",
        ),
        (
            r"wakeup\s+payload[\s\S]*slot[\s\S]*context\s+generation"
            r"[\s\S]*instance\s+epoch[\s\S]*rejected\s+before\s+calling\s+"
            r"app\s+code",
            "deferred wakeups must remain generation checked",
        ),
        (
            r"terminal\s+`event`,\s+`tick`,\s+or\s+`render`\s+callback\s+"
            r"failure[\s\S]*exactly\s+one\s+Runtime\s+Close[\s\S]*cannot\s+"
            r"replace\s+the\s+original\s+callback\s+diagnostic",
            "callback failure must deliver one close event without replacing its cause",
        ),
        (
            r"`render`\s+requests\s+another\s+invalidation[\s\S]*schedule\s+"
            r"the\s+next\s+poll\s+immediately[\s\S]*MUST\s+NOT\s+delay",
            "render-time invalidation must schedule an immediate later pass",
        ),
    ),
    Path("docs/spec/APP_SDK.md"): (
        (
            r"sdk/include/hackylens/app/runtime\.h[\s\S]*bounded[\s\S]*"
            r"Input[\s\S]*SD/media[\s\S]*Timer[\s\S]*Runtime\s+Close"
            r"[\s\S]*Wakeup",
            "Feature App SDK must publish the bounded event ABI",
        ),
        (
            r"hk_app_surface_t[\s\S]*opaque\s+and\s+borrowed[\s\S]*"
            r"no\s+present[\s\S]*LCD[\s\S]*HAL",
            "Feature App SDK surface must not expose display ownership",
        ),
        (
            r"foreground\s+switch\s+boundary[\s\S]*legacy[\s\S]*menu"
            r"[\s\S]*BACK[\s\S]*autostart[\s\S]*safe-mode",
            "Feature App SDK must preserve one legacy/v2 switch boundary",
        ),
        (
            r"invalidation\s+requested\s+from\s+inside\s+`render`[\s\S]*"
            r"schedules\s+that\s+pending\s+pass\s+immediately",
            "Feature App SDK must preserve immediate render rescheduling",
        ),
    ),
    Path("docs/adr/0007-adopt-generation-checked-app-lifecycle.md"): (
        (
            r"one\s+fixed-capacity\s+foreground\s+switch\s+for\s+v2\s+and"
            r"\s+legacy[\s\S]*same\s+close/open\s+algorithm",
            "ADR-0007 must record the single foreground switch decision",
        ),
        (
            r"bounded\s+ordered\s+union[\s\S]*generation-checked\s+Wakeup"
            r"[\s\S]*no\s+catch-up\s+loop",
            "ADR-0007 must record ordered events and bounded scheduling",
        ),
        (
            r"callback\s+failure\s+retains\s+its\s+original\s+diagnostic"
            r"[\s\S]*one\s+Runtime\s+Close[\s\S]*immediate\s+next\s+poll",
            "ADR-0007 must record callback close and render rescheduling",
        ),
    ),
}

APP_SDK_CORE_REQUIREMENTS = {
    Path("docs/spec/APP_SDK.md"): (
        (
            r"canonical\s+umbrella\s+is\s+`sdk/include/hackylens/app\.h`"
            r"[\s\S]*SDK\s+`0\.1\.0`[\s\S]*\[0\.1\.0,\s*0\.2\.0\)"
            r"[\s\S]*schema\s+major\s+`1`",
            "Feature App SDK must publish compile-time compatibility metadata",
        ),
        (
            r"app/runtime\.h`\s+owns\s+the\s+public[\s\S]*lifecycle\s+callback"
            r"[\s\S]*`hk_app_v2_entry_t`[\s\S]*MUST\s+NOT\s+maintain\s+a\s+"
            r"private\s+parallel\s+lifecycle\s+ABI",
            "Feature App SDK must own the lifecycle entry ABI",
        ),
        (
            r"host_fake\.h`[\s\S]*allocation-free[\s\S]*caller-owned\s+fixed"
            r"[\s\S]*Time,\s+Input,\s+Display[\s\S]*grant[\s\S]*lifecycle"
            r"[\s\S]*failure-injection",
            "Feature App SDK must define the deterministic fixed host fake",
        ),
        (
            r"manifest-equivalent[\s\S]*`state_bytes`[\s\S]*automatic\s+"
            r"initial\s+full\s+invalidation[\s\S]*only\s+in\s+`RUNNING`"
            r"[\s\S]*`HK_ERR_INVALID_STATE`",
            "Feature App SDK host fake must match Runtime state and limit semantics",
        ),
        (
            r"every\s+Input\s+lease[\s\S]*independent\s+sequence\s+cursor"
            r"[\s\S]*`HK_ERR_OVERFLOW`[\s\S]*`HK_TIME_MAX_SLEEP_US`"
            r"[\s\S]*`UINT64_MAX`[\s\S]*zero-area\s+rectangles"
            r"[\s\S]*`set_clip\(NULL\)`[\s\S]*validation\s+failure",
            "Feature App SDK host fake must preserve Phase 2 Capability semantics",
        ),
        (
            r"add_subdirectory[\s\S]*HackyLens::AppSDK[\s\S]*"
            r"HackyLens::AppHostFake[\s\S]*hackylens-app-sdk\.mk",
            "Feature App SDK must define CMake and Make standalone integration",
        ),
        (
            r"recursively\s+checks\s+public\s+header\s+closure[\s\S]*C11"
            r"[\s\S]*C\+\+17[\s\S]*both\s+CMake\s+and\s+Make",
            "Feature App SDK must require standalone closure and language tests",
        ),
        (
            r"architecture\s+guard\s+rejects[\s\S]*repository-private\s+layers"
            r"[\s\S]*rejects\s+a\s+lifecycle-v2\s+production\s+app\s+"
            r"dependency\s+outside\s+the\s+App\s+SDK[\s\S]*app's\s+own\s+"
            r"private\s+headers",
            "Feature App SDK must preserve the SDK-only app dependency rule",
        ),
        (
            r"bounded\s+allocation-free\s+C\+\+17\s+standard-header"
            r"[\s\S]*`<array>`[\s\S]*`<cstdint>`[\s\S]*third-party\s+headers",
            "Feature App SDK must define the bounded C++ standard-header policy",
        ),
    ),
}

APP_CONTEXT_GRANT_REQUIREMENTS = {
    Path("docs/spec/APP_RUNTIME.md"): (
        (
            r"Before\s+`probe`[\s\S]*resource-free\s+preflight[\s\S]*"
            r"neither\s+an\s+app\s+owner\s+nor\s+a\s+lease",
            "App Runtime must preflight grants without an owner before probe",
        ),
        (
            r"missing\s+required\s+capability[\s\S]*incompatible\s+version"
            r"[\s\S]*unavailable\s+required\s+feature[\s\S]*before\s+any\s+"
            r"lifecycle\s+callback",
            "required grant mismatches must exclude an app before probe",
        ),
        (
            r"Only\s+after\s+successful\s+`probe`[\s\S]*one\s+app\s+owner"
            r"[\s\S]*acquire\s+the\s+preflighted\s+grants",
            "owner and handle acquisition must follow successful probe",
        ),
        (
            r"HK_ERR_NOT_DECLARED[\s\S]*before\s+provider\s+access",
            "undeclared grants must be rejected before provider access",
        ),
        (
            r"owner-wide\s+cleanup\s+follows\s+app\s+cleanup[\s\S]*only\s+then"
            r"[\s\S]*handles\s+and\s+the\s+context\s+invalidated",
            "grant invalidation must follow app and owner-wide cleanup",
        ),
        (
            r"lifecycle\s+callback\s+receives[\s\S]{0,120}"
            r"const\s+hk_app_context_t\s*\*",
            "App Runtime callbacks must receive a const app context",
        ),
        (
            r"availability\s+is\s+stable\s+for\s+the\s+launch[\s\S]*"
            r"acquisition[\s\S]*launch\s+fails[\s\S]*MUST\s+NOT[\s\S]*"
            r"fallback\s+after\s+`probe`",
            "probe-observed optional availability must not become fallback",
        ),
        (
            r"authoritative\s+owner\s+and\s+preflight\s+availability[\s\S]*"
            r"private\s+instance\s+state[\s\S]*never\s+fields\s+read\s+back"
            r"[\s\S]*cannot\s+suppress\s+owner-wide\s+cleanup",
            "owner and preflight authority must remain private",
        ),
    ),
    Path("docs/spec/APP_SDK.md"): (
        (
            r"sdk/include/hackylens/app/context\.h[\s\S]*16\s+declared\s+"
            r"capability\s+grants[\s\S]*16\s+app-scoped\s+service\s+handles",
            "Feature App SDK must define fixed public context capacities",
        ),
        (
            r"During\s+`probe`[\s\S]*owner\s+remains\s+zero[\s\S]*"
            r"HK_ERR_INVALID_STATE",
            "Feature App SDK must distinguish probe status from handle access",
        ),
        (
            r"Copying\s+either\s+the\s+context\s+or\s+a\s+handle[\s\S]*"
            r"later\s+generation",
            "Feature App SDK must reject copied stale authority",
        ),
        (
            r"Lifecycle\s+callbacks\s+receive[\s\S]{0,120}"
            r"const\s+hk_app_context_t\s*\*",
            "Feature App SDK callbacks must receive a const app context",
        ),
        (
            r"Availability\s+observed\s+by\s+`probe`\s+is\s+stable[\s\S]*"
            r"failure\s+to\s+acquire[\s\S]*fails\s+launch[\s\S]*"
            r"cannot\s+become\s+an\s+implicit\s+fallback",
            "Feature App SDK must keep probe availability stable",
        ),
        (
            r"authoritative\s+owner\s+and\s+resolved\s+availability\s+are\s+"
            r"private[\s\S]*cleanup\s+never\s+trusts\s+owner\s+data\s+read\s+"
            r"back",
            "Feature App SDK must keep runtime grant authority private",
        ),
    ),
    Path("docs/adr/0007-adopt-generation-checked-app-lifecycle.md"): (
        (
            r"Before\s+`probe`[\s\S]*without\s+opening\s+an\s+owner\s+or\s+"
            r"acquiring\s+a\s+lease",
            "ADR-0007 must record resource-free preflight before probe",
        ),
        (
            r"After\s+a\s+successful\s+`probe`[\s\S]*opens\s+one[\s\S]*owner"
            r"[\s\S]*injects\s+only\s+those\s+preflighted\s+grants",
            "ADR-0007 must record post-probe exact grant injection",
        ),
        (
            r"Preflight\s+availability\s+is\s+stable[\s\S]*"
            r"acquisition[\s\S]*launch\s+fails[\s\S]*owner-wide\s+unwind"
            r"[\s\S]*instead\s+of\s+changing\s+the\s+status",
            "ADR-0007 must keep probe availability stable",
        ),
        (
            r"authoritative\s+owner\s+and\s+preflight\s+availability[\s\S]*"
            r"private\s+instance\s+state[\s\S]*never\s+trust\s+fields\s+read\s+"
            r"back[\s\S]*cannot\s+suppress\s+owner-wide\s+cleanup",
            "ADR-0007 must keep runtime grant authority private",
        ),
    ),
}

APP_MANIFEST_SCHEMA_REQUIREMENTS = (
    (
        r"root\s+is\s+one\s+TOML\s+table\s+with\s+exactly\s+these\s+"
        r"fields[\s\S]*every\s+field\s+is\s+required[\s\S]*no\s+implicit\s+defaults",
        "Native App Manifest schema must keep exact fields without defaults",
    ),
    (
        r"generated_symbol[\s\S]*collision-checks\s+app\s+ID,\s+entry,\s+"
        r"generated\s+symbol,\s+menu\s+order[\s\S]*autostart\s+ID",
        "Native App Manifest schema must keep every identity collision guard",
    ),
    (
        r"minimum`?\s+is\s+inclusive[\s\S]*exclusive\s+maximum[\s\S]*"
        r"optional\s+request[\s\S]*required[\s\S]*fallback",
        "Native App Manifest schema must preserve capability range and fallback rules",
    ),
    (
        r"static_ram_bytes[\s\S]*stack_bytes[\s\S]*state_bytes[\s\S]*"
        r"tick_interval_us[\s\S]*tick_budget_us[\s\S]*render_budget_us",
        "Native App Manifest schema must keep the complete finite limits table",
    ),
    (
        r"metadata\.help`?\s+and\s+`?metadata\.debug[\s\S]{0,240}"
        r"1024\s+UTF-8\s+bytes",
        "Native App Manifest schema must specify the metadata string bounds",
    ),
    (
        r"symlink/junction\s+escape[\s\S]*rejected\s+before\s+compilation",
        "Native App Manifest schema must preserve real-directory path confinement",
    ),
    (
        r"check_app_manifests\.py\s+--scan-root[\s\S]*canonical\s+UTF-8\s+JSON",
        "Native App Manifest contract must name the deterministic validator command",
    ),
    (
        r"Firmware\s+does\s+not\s+read\s+its\s+TOML\s+input[\s\S]*"
        r"immutable\s+descriptors\s+only",
        "Native App Manifest schema must remain build-time-only",
    ),
    (
        r"hackylens\.service\.legacy-<driver-kind>[\s\S]*"
        r"MUST\s+NOT\s+generate\s+an\s+SDK/runtime\s+handle[\s\S]*"
        r"forbidden\s+for\s+lifecycle\s+`v2`",
        "Native App Manifest must confine transitional legacy services to build composition",
    ),
    (
        r"gen_app_composition\.py\s+--check[\s\S]*missing\s+or\s+stale[\s\S]*"
        r"same\s+in-memory\s+canonical\s+model",
        "Native App Manifest must require composition freshness from one canonical model",
    ),
    (
        r"\.c`,\s+`\.cc`,\s+`\.cpp`,\s+and\s+`\.cxx`[\s\S]*"
        r"without\s+a\s+manifest\s+owner[\s\S]*"
        r"undeclared\s+header\s+directory\s+MUST\s+NOT\s+enter",
        "Native App Manifest must govern C/C++ ownership and private include roots",
    ),
    (
        r"lifecycle\s*=\s*\"legacy\"[\s\S]*hk_legacy_app_entry_t[\s\S]*"
        r"lifecycle\s*=\s*\"v2\"[\s\S]*hk_app_v2_entry_t[\s\S]*"
        r"never\s+guesses\s+callback",
        "Native App Manifest must define typed legacy/v2 entry objects",
    ),
    (
        r"no\s+manual\s+central\s+app\s+descriptor\s+table[\s\S]*"
        r"only\s+iterates\s+generated\s+const\s+arrays",
        "Native App Manifest must keep generated descriptors as the sole registry",
    ),
)


class Issue(NamedTuple):
    path: Path
    line: int
    message: str


class FrontMatter(NamedTuple):
    values: dict[str, str]
    lines: dict[str, int]


def relative_path(root: Path, path: Path) -> Path:
    try:
        return path.resolve().relative_to(root.resolve())
    except ValueError:
        return path


def issue(root: Path, path: Path, line: int, message: str) -> Issue:
    return Issue(relative_path(root, path), line, message)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def parse_front_matter(path: Path) -> FrontMatter:
    lines = read_text(path).splitlines()
    if not lines or lines[0].strip() != "---":
        return FrontMatter({}, {})

    values: dict[str, str] = {}
    key_lines: dict[str, int] = {}
    for index, source in enumerate(lines[1:], start=2):
        if source.strip() == "---":
            return FrontMatter(values, key_lines)
        match = re.fullmatch(r"([a-z][a-z0-9-]*):(?:\s*(.*))?", source)
        if match:
            key = match.group(1)
            values[key] = (match.group(2) or "").strip()
            key_lines[key] = index
    return FrontMatter({}, {})


def contract_paths(root: Path) -> list[Path]:
    specs = sorted(
        path
        for path in (root / "docs" / "spec").rglob("*.md")
        if path.name != "README.md"
    )
    return [
        root / "docs" / "ARCHITECTURE_VISION.md",
        *specs,
        *(root / path for path in TECHNICAL_CONTRACTS),
    ]


def validate_contract_documents(
    root: Path, paths: Iterable[Path]
) -> tuple[list[Issue], dict[str, tuple[Path, FrontMatter]]]:
    issues: list[Issue] = []
    contracts: dict[str, tuple[Path, FrontMatter]] = {}

    for path in paths:
        if not path.is_file():
            issues.append(issue(root, path, 1, "required contract document is missing"))
            continue

        front = parse_front_matter(path)
        required = ("contract-id", "owner", "version", "stability")
        for key in required:
            if not front.values.get(key):
                issues.append(issue(root, path, 1, f"missing front matter field {key!r}"))

        contract_id = front.values.get("contract-id", "")
        owner = front.values.get("owner", "")
        version = front.values.get("version", "")
        stability = front.values.get("stability", "")

        if contract_id and not CONTRACT_ID_RE.fullmatch(contract_id):
            issues.append(
                issue(
                    root,
                    path,
                    front.lines.get("contract-id", 1),
                    f"invalid contract-id {contract_id!r}",
                )
            )
        if owner and not OWNER_RE.fullmatch(owner):
            issues.append(
                issue(
                    root,
                    path,
                    front.lines.get("owner", 1),
                    f"invalid logical owner {owner!r}",
                )
            )
        if version and not SEMVER_RE.fullmatch(version):
            issues.append(
                issue(
                    root,
                    path,
                    front.lines.get("version", 1),
                    f"invalid semantic version {version!r}",
                )
            )
        if stability and stability not in STABILITIES:
            issues.append(
                issue(
                    root,
                    path,
                    front.lines.get("stability", 1),
                    f"invalid stability {stability!r}",
                )
            )

        if stability == "deprecated":
            deprecated_since = front.values.get("deprecated-since", "")
            removal_version = front.values.get("removal-version", "")
            migration_guide = front.values.get("migration-guide", "")
            replacement_contract = front.values.get("replacement-contract", "")
            if not deprecated_since:
                issues.append(
                    issue(root, path, 1, "deprecated contract lacks deprecated-since")
                )
            if not removal_version:
                issues.append(
                    issue(root, path, 1, "deprecated contract lacks removal-version")
                )
            if not migration_guide and not replacement_contract:
                issues.append(
                    issue(
                        root,
                        path,
                        1,
                        "deprecated contract lacks migration-guide or replacement-contract",
                    )
                )
            if deprecated_since and not RELEASE_SEMVER_RE.fullmatch(deprecated_since):
                issues.append(
                    issue(
                        root,
                        path,
                        front.lines.get("deprecated-since", 1),
                        "deprecated-since must be a release semantic version",
                    )
                )
            if removal_version and not RELEASE_SEMVER_RE.fullmatch(removal_version):
                issues.append(
                    issue(
                        root,
                        path,
                        front.lines.get("removal-version", 1),
                        "removal-version must be a release semantic version",
                    )
                )
            if RELEASE_SEMVER_RE.fullmatch(deprecated_since) and RELEASE_SEMVER_RE.fullmatch(
                removal_version
            ):
                deprecated_tuple = tuple(int(part) for part in deprecated_since.split("."))
                removal_tuple = tuple(int(part) for part in removal_version.split("."))
                minimum = (deprecated_tuple[0], deprecated_tuple[1] + 1, 0)
                if removal_tuple < minimum:
                    issues.append(
                        issue(
                            root,
                            path,
                            front.lines.get("removal-version", 1),
                            f"removal-version must be at least {minimum[0]}.{minimum[1]}.{minimum[2]}",
                        )
                    )

        if contract_id:
            if contract_id in contracts:
                first_path = relative_path(root, contracts[contract_id][0])
                issues.append(
                    issue(
                        root,
                        path,
                        front.lines.get("contract-id", 1),
                        f"duplicate contract-id {contract_id!r}; first declared in {first_path}",
                    )
                )
            else:
                contracts[contract_id] = (path, front)

    anchor_cache: dict[Path, set[str]] = {}
    for contract_id, (path, front) in contracts.items():
        if front.values.get("stability") != "deprecated":
            continue

        migration_guide = front.values.get("migration-guide", "")
        if migration_guide:
            line = front.lines.get("migration-guide", 1)
            lower = migration_guide.lower()
            if lower.startswith(("http://", "https://", "mailto:", "data:")):
                issues.append(
                    issue(
                        root,
                        path,
                        line,
                        "migration-guide must be a repository-local Markdown target",
                    )
                )
            else:
                decoded = unquote(migration_guide)
                location = decoded.partition("#")[0].split("?", 1)[0]
                destination = path if not location else (path.parent / location).resolve()
                if destination.suffix.lower() != ".md":
                    issues.append(
                        issue(
                            root,
                            path,
                            line,
                            "migration-guide must target a Markdown document",
                        )
                    )
                issues.extend(
                    check_link_target(
                        root, path, line, migration_guide, anchor_cache
                    )
                )

        replacement = front.values.get("replacement-contract", "")
        if replacement:
            line = front.lines.get("replacement-contract", 1)
            if not CONTRACT_ID_RE.fullmatch(replacement):
                issues.append(
                    issue(root, path, line, "replacement-contract must be a contract-id")
                )
            elif replacement == contract_id:
                issues.append(
                    issue(root, path, line, "replacement-contract cannot reference itself")
                )
            elif replacement not in contracts:
                issues.append(
                    issue(
                        root,
                        path,
                        line,
                        f"replacement-contract references unknown contract {replacement!r}",
                    )
                )
            elif contracts[replacement][1].values.get("stability") == "deprecated":
                issues.append(
                    issue(
                        root,
                        path,
                        line,
                        f"replacement-contract {replacement!r} is also deprecated",
                    )
                )

    return issues, contracts


def markdown_files(root: Path) -> list[Path]:
    files: set[Path] = set()
    for direct in (root / "README.md", root / "isp_stub" / "NOTICE.md"):
        if direct.is_file():
            files.add(direct)
    for directory in ("docs", "models", "sdcard", ".github"):
        base = root / directory
        if base.is_dir():
            files.update(base.rglob("*.md"))
    return sorted(files)


def lines_outside_fences(text: str) -> list[tuple[int, str]]:
    result: list[tuple[int, str]] = []
    fence: str | None = None
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = line.lstrip()
        marker = stripped[:3]
        if marker in ("```", "~~~"):
            if fence is None:
                fence = marker
            elif fence == marker:
                fence = None
            continue
        if fence is None:
            result.append((number, line))
    return result


def heading_anchors(text: str) -> set[str]:
    anchors: set[str] = set()
    counts: dict[str, int] = {}
    for _, line in lines_outside_fences(text):
        match = HEADING_RE.match(line)
        if not match:
            continue
        heading = re.sub(r"[`*_~]", "", match.group(2)).lower()
        heading = re.sub(r"[^\w\- ]", "", heading, flags=re.UNICODE)
        base = re.sub(r"\s+", "-", heading.strip())
        count = counts.get(base, 0)
        anchor = base if count == 0 else f"{base}-{count}"
        counts[base] = count + 1
        anchors.add(anchor)
    return anchors


def link_target(raw: str) -> str:
    value = raw.strip()
    if value.startswith("<") and ">" in value:
        return value[1 : value.index(">")]
    match = re.match(r"([^\s]+)", value)
    return match.group(1) if match else value


def reference_label(raw: str) -> str:
    return re.sub(r"\s+", " ", raw.strip()).casefold()


def check_link_target(
    root: Path,
    path: Path,
    number: int,
    target: str,
    anchor_cache: dict[Path, set[str]],
) -> list[Issue]:
    lower = target.lower()
    if lower.startswith(("http://", "https://", "mailto:", "data:")):
        return []

    decoded = unquote(target)
    location, separator, fragment = decoded.partition("#")
    location = location.split("?", 1)[0]
    destination = path if not location else (path.parent / location).resolve()

    try:
        destination.relative_to(root.resolve())
    except ValueError:
        return [issue(root, path, number, f"local link escapes repository: {target!r}")]

    if not destination.exists():
        return [issue(root, path, number, f"broken local link: {target!r}")]

    if separator and fragment and destination.is_file():
        anchor = fragment.lower()
        if destination not in anchor_cache:
            anchor_cache[destination] = heading_anchors(read_text(destination))
        if anchor not in anchor_cache[destination]:
            return [issue(root, path, number, f"missing Markdown anchor: {target!r}")]
    return []


def check_links(root: Path, paths: Iterable[Path]) -> list[Issue]:
    issues: list[Issue] = []
    anchor_cache: dict[Path, set[str]] = {}

    for path in paths:
        text = read_text(path)
        source_lines = lines_outside_fences(text)
        definitions: dict[str, tuple[int, str]] = {}

        for number, line in source_lines:
            match = REFERENCE_DEFINITION_RE.match(line)
            if not match or match.group(1).startswith("^"):
                continue
            label = reference_label(match.group(1))
            target = link_target(match.group(2))
            if label in definitions:
                issues.append(
                    issue(root, path, number, f"duplicate Markdown reference definition: {label!r}")
                )
            else:
                definitions[label] = (number, target)

        for number, line in source_lines:
            for match in MARKDOWN_LINK_RE.finditer(line):
                target = link_target(match.group(1))
                issues.extend(check_link_target(root, path, number, target, anchor_cache))

            for match in REFERENCE_LINK_RE.finditer(line):
                label = reference_label(match.group(2) or match.group(1))
                if label not in definitions:
                    issues.append(
                        issue(root, path, number, f"missing Markdown reference definition: {label!r}")
                    )

        for number, target in definitions.values():
            issues.extend(check_link_target(root, path, number, target, anchor_cache))
    return issues


def plain_text(text: str) -> str:
    text = re.sub(r"(?m)^\s*>\s?", "", text)
    text = text.replace("`", "")
    return re.sub(r"\s+", " ", text).strip().lower()


def check_preview_markers(root: Path, paths: Iterable[Path]) -> list[Issue]:
    issues: list[Issue] = []
    for path in paths:
        if not path.is_file():
            issues.append(issue(root, path, 1, "required entry document is missing"))
            continue
        if PREVIEW_MARKER not in plain_text(read_text(path)):
            issues.append(issue(root, path, 1, "missing canonical v0.4 technology-preview marker"))
    return issues


def markdown_paragraphs(text: str) -> list[tuple[int, str]]:
    paragraphs: list[tuple[int, str]] = []
    current: list[str] = []
    start = 1
    for number, line in lines_outside_fences(text):
        if line.strip():
            if not current:
                start = number
            current.append(line.strip())
        elif current:
            paragraphs.append((start, " ".join(current)))
            current = []
    if current:
        paragraphs.append((start, " ".join(current)))
    return paragraphs


def check_forbidden_claims(root: Path, paths: Iterable[Path]) -> list[Issue]:
    issues: list[Issue] = []
    for path in paths:
        for number, paragraph in markdown_paragraphs(read_text(path)):
            lowered = paragraph.lower()
            claim = next((item for item in FORBIDDEN_CLAIMS if item in lowered), None)
            if claim and not any(item in lowered for item in CLAIM_QUALIFIERS):
                issues.append(
                    issue(root, path, number, f"unqualified forbidden claim {claim!r}")
                )
    return issues


def contract_encoded_major(
    root: Path,
    contracts: dict[str, tuple[Path, FrontMatter]],
    contract_id: str,
    field: str,
) -> tuple[int | None, list[Issue]]:
    if contract_id not in contracts:
        return None, [issue(root, root / "docs" / "spec" / "README.md", 1, f"missing contract {contract_id!r}")]
    path, front = contracts[contract_id]
    value = front.values.get(field, "")
    if not value:
        return None, [issue(root, path, 1, f"{contract_id} lacks {field}")]
    if not ENCODED_MAJOR_RE.fullmatch(value):
        return None, [
            issue(
                root,
                path,
                front.lines.get(field, 1),
                f"{field} must be a non-negative decimal integer",
            )
        ]
    return int(value), []


def fixed_constant(
    root: Path, relative: str, pattern: str, label: str
) -> tuple[int | None, list[Issue]]:
    path = root / relative
    if not path.is_file():
        return None, [issue(root, path, 1, f"canonical {label} source is missing")]
    text = read_text(path)
    match = re.search(pattern, text, flags=re.MULTILINE)
    if not match:
        return None, [issue(root, path, 1, f"canonical {label} constant is missing")]
    return int(match.group(1)), []


def check_canonical_versions(
    root: Path, contracts: dict[str, tuple[Path, FrontMatter]]
) -> list[Issue]:
    issues: list[Issue] = []
    version_path = root / "VERSION"
    if not version_path.is_file():
        issues.append(issue(root, version_path, 1, "canonical firmware VERSION is missing"))
    else:
        firmware_version = read_text(version_path).strip()
        if not RELEASE_SEMVER_RE.fullmatch(firmware_version):
            issues.append(issue(root, version_path, 1, "firmware VERSION is not release SemVer"))
        readme = root / "README.md"
        if readme.is_file():
            readme_text = read_text(readme)
            for expected in (
                f"Firmware version {firmware_version}",
                f"firmware-v{firmware_version}",
            ):
                if expected not in readme_text:
                    issues.append(issue(root, readme, 1, f"README badge does not contain {expected!r}"))

    mappings = (
        (
            "hackylens.hmpy",
            "wire-major",
            (
                ("tools/hmpy_protocol.py", r"^PROTOCOL_VERSION\s*=\s*(\d+)\s*$", "HMPY host version"),
                ("firmware/src/services/hmpy_codec.h", r"^#define\s+HMPY_PROTOCOL_VERSION\s+(\d+)U?\s*$", "HMPY firmware version"),
            ),
            root / "docs" / "HMPY_PROTOCOL.md",
            r"^# HackyLens MicroPython Protocol \(HMPY\) v(\d+)\s*$",
        ),
        (
            "hackylens.external-link",
            "wire-major",
            (
                ("firmware/src/services/external_link_protocol.h", r"^#define\s+HK_LINK_PROTOCOL_VERSION\s+(\d+)U?\s*$", "External Link version"),
            ),
            root / "docs" / "EXTERNAL_LINK_PROTOCOL.md",
            r"^# HackyLens External Link Protocol v(\d+)\s*$",
        ),
        (
            "hackylens.ai-model-package",
            "schema-major",
            (
                ("tools/ai_model.py", r"^MANIFEST_VERSION\s*=\s*(\d+)\s*$", "AI host manifest version"),
                ("firmware/src/storage/ai_model_storage.c", r"^#define\s+AI_MANIFEST_VERSION\s+(\d+)U?\s*$", "AI firmware manifest version"),
            ),
            None,
            None,
        ),
        (
            "hackylens.micropython-api",
            "api-major",
            (),
            root / "docs" / "MICROPYTHON_API.md",
            r"^# HackyLens MicroPython API v(\d+)\s*$",
        ),
    )

    for contract_id, encoded_field, constants, heading_path, heading_pattern in mappings:
        major, major_issues = contract_encoded_major(
            root, contracts, contract_id, encoded_field
        )
        issues.extend(major_issues)
        if major is None:
            continue
        for relative, pattern, label in constants:
            value, constant_issues = fixed_constant(root, relative, pattern, label)
            issues.extend(constant_issues)
            if value is not None and value != major:
                issues.append(
                    issue(
                        root,
                        root / relative,
                        1,
                        f"{label} {value} does not match {contract_id} {encoded_field} {major}",
                    )
                )
        if heading_path is not None and heading_pattern is not None:
            match = re.search(heading_pattern, read_text(heading_path), flags=re.MULTILINE)
            if not match:
                issues.append(issue(root, heading_path, 1, "versioned contract heading is missing"))
            elif int(match.group(1)) != major:
                issues.append(
                    issue(root, heading_path, 1,
                          f"heading major does not match {contract_id} {encoded_field}")
                )
    return issues


def release_version_tuple(value: str) -> tuple[int, int, int] | None:
    if RELEASE_SEMVER_RE.fullmatch(value) is None:
        return None
    major, minor, patch = value.split(".")
    return int(major), int(minor), int(patch)


def check_phase3_contracts(
    root: Path, contracts: dict[str, tuple[Path, FrontMatter]]
) -> list[Issue]:
    """Keep the initial Phase 3 contract set mutually compatible and in scope."""

    issues: list[Issue] = []
    expected_version = (0, 1, 0)
    expected_range = ">=0.1.0,<0.2.0"

    for contract_id, policy in PHASE3_CONTRACTS.items():
        if contract_id not in contracts:
            issues.append(issue(
                root, root / policy["path"], 1,
                f"missing Phase 3 contract {contract_id!r}",
            ))
            continue
        path, front = contracts[contract_id]
        expected_path = (root / policy["path"]).resolve()
        if path.resolve() != expected_path:
            issues.append(issue(
                root, path, 1,
                f"{contract_id} must live at {policy['path'].as_posix()}",
            ))
        if release_version_tuple(front.values.get("version", "")) != expected_version:
            issues.append(issue(
                root, path, front.lines.get("version", 1),
                f"{contract_id} must remain on initial version 0.1.0",
            ))
        if front.values.get("stability") != "experimental":
            issues.append(issue(
                root, path, front.lines.get("stability", 1),
                f"{contract_id} must remain experimental",
            ))
        if front.values.get("phase") != "3":
            issues.append(issue(
                root, path, front.lines.get("phase", 1),
                f"{contract_id} must declare phase 3",
            ))

        for field, target_id in policy["compatibility"].items():
            value = front.values.get(field, "")
            match = COMPATIBILITY_RANGE_RE.fullmatch(value)
            if match is None or value != expected_range:
                issues.append(issue(
                    root, path, front.lines.get(field, 1),
                    f"{field} must be the initial experimental range {expected_range}",
                ))
                continue
            target = contracts.get(target_id)
            target_version = (
                release_version_tuple(target[1].values.get("version", ""))
                if target else None
            )
            lower = release_version_tuple(match.group(1))
            upper = release_version_tuple(match.group(2))
            if target_version is None or lower is None or upper is None or not (
                lower <= target_version < upper
            ):
                issues.append(issue(
                    root, path, front.lines.get(field, 1),
                    f"{field} does not accept {target_id} version",
                ))

        text = read_text(path)
        for pattern in PHASE4_SCHEMA_PATTERNS:
            match = pattern.search(text)
            if match:
                line = text.count("\n", 0, match.start()) + 1
                issues.append(issue(
                    root, path, line,
                    "Phase 3 native-app contract contains forbidden Phase 4 scope",
                ))

    manifest = contracts.get("hackylens.native-app-manifest")
    if manifest:
        path, front = manifest
        required = {
            "schema-major": "1",
            "format-scope": "native-app-build",
            "runtime-parsed": "false",
        }
        for field, expected in required.items():
            if front.values.get(field) != expected:
                issues.append(issue(
                    root, path, front.lines.get(field, 1),
                    f"native app manifest {field} must be {expected}",
                ))
    return issues


def check_phase3_teardown_deadline(root: Path) -> list[Issue]:
    """Keep the single runtime-owned teardown deadline fully specified."""

    issues: list[Issue] = []
    for relative, requirements in TEARDOWN_DEADLINE_REQUIREMENTS.items():
        path = root / relative
        if not path.is_file():
            issues.append(issue(root, path, 1, "teardown deadline document is missing"))
            continue
        text = read_text(path)
        for pattern, message in requirements:
            if re.search(pattern, text, flags=re.IGNORECASE) is None:
                issues.append(issue(root, path, 1, message))
    return issues


def check_phase3_app_context_grants(root: Path) -> list[Issue]:
    """Keep exact manifest grants and one-owner context lifetime normative."""

    issues: list[Issue] = []
    for relative, requirements in APP_CONTEXT_GRANT_REQUIREMENTS.items():
        path = root / relative
        if not path.is_file():
            issues.append(issue(root, path, 1, "app context document is missing"))
            continue
        text = read_text(path)
        for pattern, message in requirements:
            if re.search(pattern, text, flags=re.IGNORECASE) is None:
                issues.append(issue(root, path, 1, message))
    return issues


def check_phase3_app_runtime_integration(root: Path) -> list[Issue]:
    """Keep the Phase 3.7 event/render/switch contract mechanically present."""

    issues: list[Issue] = []
    for relative, requirements in APP_RUNTIME_INTEGRATION_REQUIREMENTS.items():
        path = root / relative
        if not path.is_file():
            issues.append(issue(root, path, 1, "app runtime integration document is missing"))
            continue
        text = read_text(path)
        for pattern, message in requirements:
            if re.search(pattern, text, flags=re.IGNORECASE) is None:
                issues.append(issue(root, path, 1, message))
    return issues


def check_phase3_app_sdk_core(root: Path) -> list[Issue]:
    """Keep the standalone SDK, host fake, and dependency closure normative."""

    issues: list[Issue] = []
    for relative, requirements in APP_SDK_CORE_REQUIREMENTS.items():
        path = root / relative
        if not path.is_file():
            issues.append(issue(root, path, 1, "Feature App SDK document is missing"))
            continue
        text = read_text(path)
        for pattern, message in requirements:
            if re.search(pattern, text, flags=re.IGNORECASE) is None:
                issues.append(issue(root, path, 1, message))
    return issues


def check_app_manifest_schema(root: Path) -> list[Issue]:
    """Keep the schema-1 build-time grammar and safety rules normative."""

    path = root / "docs" / "spec" / "APP_MANIFEST.md"
    if not path.is_file():
        return [issue(root, path, 1, "Native App Manifest contract is missing")]
    text = read_text(path)
    issues: list[Issue] = []
    for pattern, message in APP_MANIFEST_SCHEMA_REQUIREMENTS:
        if re.search(pattern, text, flags=re.IGNORECASE) is None:
            issues.append(issue(root, path, 1, message))
    return issues


def check_phase2_status_consistency(root: Path) -> list[Issue]:
    """Prevent the version table from reopening completed SEN0305 acceptance."""

    current_path = root / "docs" / "CURRENT_STATE.md"
    roadmap_path = root / "docs" / "ROADMAP.md"
    versioning_path = root / "docs" / "spec" / "VERSIONING.md"
    required = (current_path, roadmap_path, versioning_path)
    if any(not path.is_file() for path in required):
        return []
    current = plain_text(read_text(current_path))
    roadmap = plain_text(read_text(roadmap_path))
    if "phase 2 is complete" not in current or "статус: **done**" not in roadmap:
        return []
    versioning = plain_text(read_text(versioning_path))
    issues: list[Issue] = []
    if "physical qualification in progress" in versioning:
        issues.append(issue(
            root,
            versioning_path,
            1,
            "Firmware status contradicts completed Phase 2 physical acceptance",
        ))
    for required_status in (
        "physically accepted on sen0305",
        "maix cube compile-conformance-only",
        "general hardware portability not claimed",
    ):
        if required_status not in versioning:
            issues.append(issue(
                root,
                versioning_path,
                1,
                f"Firmware status must state {required_status!r}",
            ))
    return issues


def check_adrs(root: Path) -> list[Issue]:
    issues: list[Issue] = []
    adr_dir = root / "docs" / "adr"
    if not adr_dir.is_dir():
        return [issue(root, adr_dir, 1, "ADR directory is missing")]

    records: dict[str, list[tuple[Path, FrontMatter]]] = {}
    paths = sorted(
        path for path in adr_dir.glob("*.md")
        if path.name not in {"README.md", "template.md"}
    )

    for path in paths:
        name_match = ADR_FILE_RE.fullmatch(path.name)
        if not name_match:
            issues.append(issue(root, path, 1, "invalid ADR filename"))
            continue
        front = parse_front_matter(path)
        number = name_match.group(1)
        records.setdefault(number, []).append((path, front))
        for key in ("adr", "title", "status", "date", "deciders"):
            if not front.values.get(key):
                issues.append(issue(root, path, 1, f"missing ADR field {key!r}"))
        if front.values.get("adr") and front.values["adr"] != number:
            issues.append(issue(root, path, front.lines.get("adr", 1), "ADR number does not match filename"))
        status = front.values.get("status", "")
        if status and status not in {"proposed", "accepted", "rejected", "superseded"}:
            issues.append(issue(root, path, front.lines.get("status", 1), f"invalid ADR status {status!r}"))
        date_value = front.values.get("date", "")
        if date_value:
            try:
                date.fromisoformat(date_value)
            except ValueError:
                issues.append(issue(root, path, front.lines.get("date", 1), "ADR date must be YYYY-MM-DD"))
        if status == "superseded" and not front.values.get("superseded-by"):
            issues.append(issue(root, path, 1, "superseded ADR lacks superseded-by"))
        if status != "superseded" and front.values.get("superseded-by"):
            issues.append(
                issue(root, path, front.lines.get("superseded-by", 1),
                      "only a superseded ADR may declare superseded-by")
            )
        if front.values.get("supersedes") and status != "accepted":
            issues.append(
                issue(root, path, front.lines.get("supersedes", 1),
                      "only an accepted ADR may supersede another ADR")
            )

        text = read_text(path)
        for section in ADR_SECTIONS:
            if not re.search(rf"^## {re.escape(section)}\s*$", text, flags=re.MULTILINE):
                issues.append(issue(root, path, 1, f"missing ADR section {section!r}"))

    for number, entries in records.items():
        if len(entries) > 1:
            first = relative_path(root, entries[0][0])
            for path, _ in entries[1:]:
                issues.append(
                    issue(root, path, 1, f"duplicate ADR number {number}; first declared in {first}")
                )

    canonical = {number: entries[0] for number, entries in records.items()}
    for number, (path, front) in canonical.items():
        for field, reciprocal in (
            ("supersedes", "superseded-by"),
            ("superseded-by", "supersedes"),
        ):
            reference = front.values.get(field, "")
            if not reference:
                continue
            line = front.lines.get(field, 1)
            if not ADR_REFERENCE_RE.fullmatch(reference):
                issues.append(issue(root, path, line, f"{field} must be a four-digit ADR number"))
                continue
            if reference == number:
                issues.append(issue(root, path, line, f"ADR must not {field} itself"))
                continue
            if reference not in canonical:
                issues.append(issue(root, path, line, f"{field} references missing ADR {reference}"))
                continue

            target_path, target_front = canonical[reference]
            if target_front.values.get(reciprocal, "") != number:
                target = relative_path(root, target_path)
                issues.append(
                    issue(
                        root,
                        path,
                        line,
                        f"ADR {reference} in {target} must declare {reciprocal}: {number}",
                    )
                )
            if field == "supersedes" and target_front.values.get("status") != "superseded":
                issues.append(
                    issue(root, path, line, f"superseded ADR {reference} must have status superseded")
                )
            if field == "superseded-by" and target_front.values.get("status") != "accepted":
                issues.append(
                    issue(root, path, line, f"superseding ADR {reference} must have status accepted")
                )
    return issues


def check_repository(root: Path = ROOT) -> list[Issue]:
    root = root.resolve()
    paths = contract_paths(root)
    contract_issues, contracts = validate_contract_documents(root, paths)
    issues = list(contract_issues)
    issues.extend(check_links(root, markdown_files(root)))
    issues.extend(check_preview_markers(root, (root / path for path in ENTRY_DOCUMENTS)))
    issues.extend(check_forbidden_claims(root, (root / path for path in CLAIM_SURFACES)))
    issues.extend(check_canonical_versions(root, contracts))
    issues.extend(check_phase3_contracts(root, contracts))
    issues.extend(check_app_manifest_schema(root))
    issues.extend(check_phase3_teardown_deadline(root))
    issues.extend(check_phase3_app_context_grants(root))
    issues.extend(check_phase3_app_runtime_integration(root))
    issues.extend(check_phase3_app_sdk_core(root))
    issues.extend(check_phase2_status_consistency(root))
    issues.extend(check_adrs(root))
    return sorted(issues, key=lambda item: (str(item.path), item.line, item.message))


def main() -> int:
    issues = check_repository()
    if issues:
        for found in issues:
            print(f"{found.path.as_posix()}:{found.line}: {found.message}", file=sys.stderr)
        print(f"[FAIL] documentation guard found {len(issues)} issue(s)", file=sys.stderr)
        return 1
    print("[OK] documentation governance guard passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
