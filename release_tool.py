#!/usr/bin/env python3
"""
Release deploy/pack tool for math_search_win (Windows).

Purpose:
- Build a runnable release directory under dist/
- Deploy Qt runtime with windeployqt
- Copy project-owned runtime resources with runtime-aware mapping
- Validate critical runtime files after deployment
- Optionally pack the release directory as zip

Inputs:
- Release executable path (or build directory + exe name)
- windeployqt path (or Qt bin directory / PATH)
- Dist output settings and resource copy options

Outputs:
- dist/<dist_name>/...
- dist/<dist_name>_release.zip (when running package/all)

Usage:
- python release_tool.py deploy
- python release_tool.py verify --verbose
- python release_tool.py all
- python release_tool.py deploy --dry-run --verbose
"""

from __future__ import annotations

import argparse
import fnmatch
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


DEFAULT_APP_NAME = "math_search_win"
DEFAULT_WINDEPLOYQT = Path(r"D:\Qt\6.11.0\msvc2022_64\bin\windeployqt.exe")

# Runtime style/resource file patterns copied from src/ui/style into app_resources/styles.
# This avoids shipping C++ source files while preserving runtime style assets.
STYLE_RUNTIME_PATTERNS = (
    "*.qss",
    "*.css",
    "*.json",
    "*.svg",
    "*.png",
    "*.jpg",
    "*.jpeg",
    "*.webp",
    "*.bmp",
    "*.ttf",
    "*.otf",
    "*.ico",
    "*.html",
    "*.js",
)


@dataclass(frozen=True)
class ValidationCheck:
    label: str
    path: Path
    expected: str = "any"  # any | file | dir
    required: bool = True


@dataclass(frozen=True)
class ReleaseConfig:
    command: str
    project_root: Path
    build_dir: Path
    exe_name: str
    exe_path: Path
    dist_root: Path
    dist_dir: Path
    zip_path: Path
    windeployqt_path: Path | None
    skip_windeployqt: bool
    include_docs: bool
    include_readme: bool
    copy_cache_content: bool
    dry_run: bool
    verbose: bool


class ReleaseTool:
    def __init__(self, config: ReleaseConfig) -> None:
        self.config = config

    def info(self, message: str) -> None:
        print(f"[INFO] {message}")

    def warn(self, message: str) -> None:
        print(f"[WARN] {message}")

    def debug(self, message: str) -> None:
        if self.config.verbose:
            print(f"[DEBUG] {message}")

    def error(self, message: str) -> None:
        print(f"[ERROR] {message}")

    def _ensure_dir(self, path: Path, label: str) -> None:
        if self.config.dry_run:
            self.info(f"[dry-run] create {label}: {path}")
            return
        path.mkdir(parents=True, exist_ok=True)
        self.debug(f"ensured {label}: {path}")

    def _remove_dir(self, path: Path, label: str) -> None:
        if not path.exists():
            return
        if self.config.dry_run:
            self.info(f"[dry-run] remove {label}: {path}")
            return
        try:
            shutil.rmtree(path)
        except Exception as exc:
            raise RuntimeError(
                f"Failed to remove existing {label}: {path}. "
                "Close processes that may lock files, or use a new --dist-name."
            ) from exc
        self.info(f"Removed {label}: {path}")

    def _copy_file(self, source: Path, target: Path, label: str) -> None:
        if not source.is_file():
            raise FileNotFoundError(f"{label} not found: {source}")
        self.info(f"Copy {label}: {source} -> {target}")
        if self.config.dry_run:
            return
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)

    def _copy_directory(self, source: Path, target: Path, label: str, required: bool) -> bool:
        if not source.exists():
            if required:
                raise FileNotFoundError(f"{label} not found: {source}")
            self.warn(f"Skip missing {label}: {source}")
            return False

        if not source.is_dir():
            if required:
                raise NotADirectoryError(f"{label} is not a directory: {source}")
            self.warn(f"Skip non-directory {label}: {source}")
            return False

        self.info(f"Copy {label}: {source} -> {target}")
        if self.config.dry_run:
            return True
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source, target, dirs_exist_ok=True)
        return True

    def _copy_filtered_tree(
        self,
        source: Path,
        target: Path,
        label: str,
        include_patterns: tuple[str, ...],
        required: bool,
    ) -> int:
        if not source.exists():
            if required:
                raise FileNotFoundError(f"{label} source directory not found: {source}")
            self.warn(f"Skip missing {label}: {source}")
            return 0

        if not source.is_dir():
            raise NotADirectoryError(f"{label} source is not a directory: {source}")

        normalized_patterns = tuple(pattern.lower() for pattern in include_patterns)
        copied_count = 0

        self.info(f"Copy {label} (filtered): {source} -> {target}")
        self.debug(f"{label} include patterns: {', '.join(normalized_patterns)}")

        for file_path in source.rglob("*"):
            if not file_path.is_file():
                continue

            rel = file_path.relative_to(source)
            rel_posix = rel.as_posix().lower()
            file_name = file_path.name.lower()
            matched = any(
                fnmatch.fnmatch(file_name, pattern) or fnmatch.fnmatch(rel_posix, pattern)
                for pattern in normalized_patterns
            )
            if not matched:
                self.debug(f"Skip non-runtime file: {file_path}")
                continue

            copied_count += 1
            if self.config.dry_run:
                self.info(f"[dry-run] copy runtime style asset: {file_path} -> {target / rel}")
                continue

            destination = target / rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file_path, destination)

        if copied_count == 0 and required:
            raise RuntimeError(
                f"{label} copy produced no files. "
                f"Source={source}, patterns={include_patterns}"
            )

        self.info(f"{label} copied files: {copied_count}")
        return copied_count

    def _run_command(
        self,
        command: list[str],
        cwd: Path | None = None,
        env_overrides: dict[str, str] | None = None,
    ) -> None:
        display = " ".join(f'"{part}"' if " " in part else part for part in command)
        self.info(f"Run command: {display}")
        if self.config.dry_run:
            return

        env = os.environ.copy()
        if env_overrides:
            env.update(env_overrides)
        result = subprocess.run(command, cwd=str(cwd) if cwd else None, env=env)
        if result.returncode != 0:
            raise RuntimeError(f"Command failed ({result.returncode}): {display}")

    def _prepare_dist_dir(self) -> None:
        self.info(f"Prepare dist directory: {self.config.dist_dir}")
        self._remove_dir(self.config.dist_dir, "dist directory")
        self._ensure_dir(self.config.dist_dir, "dist directory")

    def _copy_executable(self) -> Path:
        if not self.config.exe_path.is_file():
            raise FileNotFoundError(f"Release executable not found: {self.config.exe_path}")

        dist_exe = self.config.dist_dir / self.config.exe_name
        self._copy_file(self.config.exe_path, dist_exe, "release executable")
        return dist_exe

    def _deploy_qt_runtime(self, dist_exe: Path) -> None:
        if self.config.skip_windeployqt:
            self.warn("Skip windeployqt by --skip-windeployqt.")
            return

        if self.config.windeployqt_path is None:
            if self.config.dry_run:
                self.warn("windeployqt is unresolved; dry-run skips actual deployment command.")
                return
            raise FileNotFoundError("windeployqt path is empty.")
        if not self.config.windeployqt_path.is_file():
            raise FileNotFoundError(f"windeployqt not found: {self.config.windeployqt_path}")

        cmd = [
            str(self.config.windeployqt_path),
            "--release",
            "--compiler-runtime",
        ]
        if self.config.verbose:
            cmd.extend(["--verbose", "2"])
        cmd.append(str(dist_exe))
        qt_bin_dir = str(self.config.windeployqt_path.parent)
        merged_path = qt_bin_dir + os.pathsep + os.environ.get("PATH", "")
        self._run_command(cmd, env_overrides={"PATH": merged_path})

    def _copy_project_resources(self) -> None:
        project_root = self.config.project_root
        dist_dir = self.config.dist_dir

        # App business/runtime resources.
        self._copy_directory(
            source=project_root / "data",
            target=dist_dir / "data",
            label="data directory",
            required=True,
        )
        self._copy_directory(
            source=project_root / "app_resources",
            target=dist_dir / "app_resources",
            label="app_resources directory",
            required=True,
        )

        # Runtime style resources: map source style files into app_resources/styles.
        style_source = project_root / "src" / "ui" / "style"
        style_target = dist_dir / "app_resources" / "styles"
        self._copy_filtered_tree(
            source=style_source,
            target=style_target,
            label="style runtime assets",
            include_patterns=STYLE_RUNTIME_PATTERNS,
            required=True,
        )

        if not self.config.dry_run:
            style_main = style_target / "app.qss"
            if not style_main.is_file():
                raise FileNotFoundError(
                    "Expected style file missing after copy: "
                    f"{style_main}. Runtime expects app.qss."
                )

        # license directory: copy if exists, otherwise create empty directory.
        license_source = project_root / "license"
        license_target = dist_dir / "license"
        if license_source.exists() and license_source.is_dir():
            self._copy_directory(
                source=license_source,
                target=license_target,
                label="license directory",
                required=False,
            )
        else:
            self.warn(
                "Source license directory missing; creating empty license directory "
                "for runtime contract."
            )
            self._ensure_dir(license_target, "license directory")

        # cache directory: by default create empty folder.
        cache_source = project_root / "cache"
        cache_target = dist_dir / "cache"
        if self.config.copy_cache_content:
            copied = self._copy_directory(
                source=cache_source,
                target=cache_target,
                label="cache directory",
                required=False,
            )
            if not copied:
                self.warn("cache source missing; creating empty cache directory.")
                self._ensure_dir(cache_target, "cache directory")
        else:
            self.info("Create empty cache directory (no cache content copy).")
            self._ensure_dir(cache_target, "cache directory")
        self._ensure_dir(cache_target / "webengine", "cache/webengine directory")

        if self.config.include_docs:
            self._copy_directory(
                source=project_root / "docs",
                target=dist_dir / "docs",
                label="docs directory",
                required=True,
            )

        if self.config.include_readme:
            readme_source = project_root / "README.md"
            if readme_source.is_file():
                self._copy_file(readme_source, dist_dir / "README.md", "README file")
            else:
                self.warn(f"README.md not found, skip copy: {readme_source}")

    def deploy(self) -> None:
        self.info("=== Deploy start ===")
        self.debug(f"project_root={self.config.project_root}")
        self.debug(f"build_dir={self.config.build_dir}")
        self.debug(f"exe_path={self.config.exe_path}")
        self.debug(f"dist_dir={self.config.dist_dir}")
        self.debug(
            "windeployqt="
            + (
                str(self.config.windeployqt_path)
                if self.config.windeployqt_path is not None
                else "<none>"
            )
        )

        self._prepare_dist_dir()
        dist_exe = self._copy_executable()
        self._deploy_qt_runtime(dist_exe)
        self._copy_project_resources()

        if self.config.dry_run:
            self.warn("Dry-run mode: skip deploy validation.")
        else:
            self.verify()

        self.info(f"Deploy completed: {self.config.dist_dir}")

    def package(self) -> None:
        self.info("=== Package start ===")
        if not self.config.dist_dir.is_dir():
            if self.config.dry_run:
                self.warn(
                    "Dry-run mode: dist directory does not exist yet, "
                    "skip real zip creation."
                )
                return
            raise FileNotFoundError(f"Dist directory not found: {self.config.dist_dir}")

        if not self.config.dry_run:
            self.verify()

        self._ensure_dir(self.config.dist_root, "dist root")
        if self.config.zip_path.exists():
            if self.config.dry_run:
                self.info(f"[dry-run] remove old zip: {self.config.zip_path}")
            else:
                self.config.zip_path.unlink()
                self.info(f"Removed old zip: {self.config.zip_path}")

        self.info(f"Create zip package: {self.config.zip_path}")
        if self.config.dry_run:
            return

        with ZipFile(self.config.zip_path, "w", compression=ZIP_DEFLATED) as archive:
            for file_path in self.config.dist_dir.rglob("*"):
                if not file_path.is_file():
                    continue
                arcname = Path(self.config.dist_dir.name) / file_path.relative_to(self.config.dist_dir)
                archive.write(file_path, arcname.as_posix())

        self.info(f"Package completed: {self.config.zip_path}")

    def verify(self) -> None:
        self.info("=== Verify release directory ===")
        checks = self._build_validation_checks()
        failures: list[str] = []

        for check in checks:
            ok = self._check_path(check)
            status = "OK" if ok else ("FAIL" if check.required else "WARN")
            self.info(f"[{status}] {check.label}: {check.path}")
            if not ok and check.required:
                failures.append(f"{check.label}: {check.path}")

        if failures:
            raise RuntimeError(
                "Release validation failed. Missing required items:\n- "
                + "\n- ".join(failures)
            )

        self.info("Release validation passed.")

    def _build_validation_checks(self) -> list[ValidationCheck]:
        dist_dir = self.config.dist_dir
        checks = [
            ValidationCheck("Application exe", dist_dir / self.config.exe_name, expected="file"),
            ValidationCheck("Qt6Core.dll", dist_dir / "Qt6Core.dll", expected="file"),
            ValidationCheck("Qt6Gui.dll", dist_dir / "Qt6Gui.dll", expected="file"),
            ValidationCheck("Qt6Widgets.dll", dist_dir / "Qt6Widgets.dll", expected="file"),
            ValidationCheck("Qt6WebEngineCore.dll", dist_dir / "Qt6WebEngineCore.dll", expected="file"),
            ValidationCheck("Qt6WebEngineWidgets.dll", dist_dir / "Qt6WebEngineWidgets.dll", expected="file"),
            ValidationCheck("QtWebEngineProcess.exe", dist_dir / "QtWebEngineProcess.exe", expected="file"),
            ValidationCheck("platforms directory", dist_dir / "platforms", expected="dir"),
            ValidationCheck("platforms/qwindows.dll", dist_dir / "platforms" / "qwindows.dll", expected="file"),
            ValidationCheck("Qt resources directory", dist_dir / "resources", expected="dir"),
            ValidationCheck("Qt resources/icudtl.dat", dist_dir / "resources" / "icudtl.dat", expected="file"),
            ValidationCheck("app_resources directory", dist_dir / "app_resources", expected="dir"),
            ValidationCheck(
                "detail template",
                dist_dir / "app_resources" / "detail" / "detail_template.html",
                expected="file",
            ),
            ValidationCheck("detail.js", dist_dir / "app_resources" / "detail" / "detail.js", expected="file"),
            ValidationCheck("detail.css", dist_dir / "app_resources" / "detail" / "detail.css", expected="file"),
            ValidationCheck("katex.min.css", dist_dir / "app_resources" / "katex" / "katex.min.css", expected="file"),
            ValidationCheck("katex.min.js", dist_dir / "app_resources" / "katex" / "katex.min.js", expected="file"),
            ValidationCheck("app style qss", dist_dir / "app_resources" / "styles" / "app.qss", expected="file"),
            ValidationCheck("data directory", dist_dir / "data", expected="dir"),
            ValidationCheck(
                "backend_search_index.json",
                dist_dir / "data" / "backend_search_index.json",
                expected="file",
            ),
            ValidationCheck(
                "canonical_content_v2.json",
                dist_dir / "data" / "canonical_content_v2.json",
                expected="file",
            ),
            ValidationCheck("cache directory", dist_dir / "cache", expected="dir"),
            ValidationCheck("license directory", dist_dir / "license", expected="dir"),
            ValidationCheck("imageformats plugin directory", dist_dir / "imageformats", expected="dir", required=False),
            ValidationCheck("styles plugin directory", dist_dir / "styles", expected="dir", required=False),
            ValidationCheck("translations directory", dist_dir / "translations", expected="dir", required=False),
        ]

        if self.config.include_docs:
            checks.append(ValidationCheck("docs directory", dist_dir / "docs", expected="dir"))
        if self.config.include_readme:
            checks.append(ValidationCheck("README.md", dist_dir / "README.md", expected="file"))

        return checks

    @staticmethod
    def _check_path(check: ValidationCheck) -> bool:
        if check.expected == "file":
            return check.path.is_file()
        if check.expected == "dir":
            return check.path.is_dir()
        return check.path.exists()


def resolve_path(base_dir: Path, raw_path: str) -> Path:
    candidate = Path(raw_path).expanduser()
    if not candidate.is_absolute():
        candidate = base_dir / candidate
    return candidate.resolve()


def resolve_windeployqt(
    project_root: Path,
    explicit_path: str | None,
    qt_bin_dir: str | None,
    skip_windeployqt: bool,
    required: bool,
) -> Path | None:
    if skip_windeployqt:
        return None

    candidates: list[Path] = []
    if explicit_path:
        candidates.append(resolve_path(project_root, explicit_path))
    if qt_bin_dir:
        candidates.append(resolve_path(project_root, qt_bin_dir) / "windeployqt.exe")

    env_path = os.environ.get("WINDEPLOYQT", "").strip()
    if env_path:
        candidates.append(Path(env_path).expanduser().resolve())

    which_path = shutil.which("windeployqt.exe") or shutil.which("windeployqt")
    if which_path:
        candidates.append(Path(which_path).expanduser().resolve())

    dedup: set[str] = set()
    for candidate in candidates:
        key = str(candidate).lower()
        if key in dedup:
            continue
        dedup.add(key)
        if candidate.is_file():
            return candidate

    checked = "\n- ".join(str(item) for item in candidates) if candidates else "<none>"
    if required:
        raise FileNotFoundError(
            "windeployqt.exe not found.\n"
            "Set one of:\n"
            "- --windeployqt <path>\n"
            "- --qt-bin-dir <qt_bin_dir>\n"
            "- WINDEPLOYQT env\n"
            "- PATH\n"
            f"Checked:\n- {checked}"
        )
    return None


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Release deployment and packaging tool for math_search_win."
    )
    parser.add_argument(
        "--project-root",
        default=str(Path(__file__).resolve().parent),
        help="Project root directory (default: folder of this script).",
    )
    parser.add_argument(
        "--build-dir",
        default=r"out\build\msvc-release\Release",
        help="Release build directory that contains the executable.",
    )
    parser.add_argument(
        "--exe-name",
        default=f"{DEFAULT_APP_NAME}.exe",
        help="Executable name in release build output.",
    )
    parser.add_argument(
        "--exe-path",
        default=None,
        help="Executable full/relative path. If set, overrides --build-dir + --exe-name.",
    )
    parser.add_argument(
        "--dist-root",
        default="dist",
        help="Output dist root directory.",
    )
    parser.add_argument(
        "--dist-name",
        default=DEFAULT_APP_NAME,
        help="Release directory name under dist root.",
    )
    parser.add_argument(
        "--zip-name",
        default=None,
        help="Zip file name under dist root (default: <dist-name>_release.zip).",
    )
    parser.add_argument(
        "--windeployqt",
        default=str(DEFAULT_WINDEPLOYQT),
        help=(
            "Path to windeployqt.exe "
            f"(default: {DEFAULT_WINDEPLOYQT})."
        ),
    )
    parser.add_argument(
        "--qt-bin-dir",
        default=None,
        help="Qt bin directory, used to locate windeployqt.exe automatically.",
    )
    parser.add_argument(
        "--skip-windeployqt",
        action="store_true",
        help="Skip running windeployqt (for troubleshooting only).",
    )
    parser.add_argument(
        "--copy-cache-content",
        action="store_true",
        help="Copy existing cache/* content to dist/cache (default is empty cache directory).",
    )
    parser.add_argument(
        "--include-docs",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Copy docs directory to release output (default: true).",
    )
    parser.add_argument(
        "--include-readme",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Copy README.md to release output (default: true).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print planned operations without writing files.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable verbose logs.",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("deploy", help="Generate dist release directory.")
    subparsers.add_parser("verify", help="Validate existing dist release directory.")
    subparsers.add_parser("package", help="Create zip from dist release directory.")
    subparsers.add_parser("all", help="Run deploy + package.")
    return parser


def build_config(args: argparse.Namespace) -> ReleaseConfig:
    project_root = resolve_path(Path.cwd(), args.project_root)
    build_dir = resolve_path(project_root, args.build_dir)

    if args.exe_path:
        exe_path = resolve_path(project_root, args.exe_path)
        exe_name = exe_path.name
    else:
        exe_name = args.exe_name
        exe_path = build_dir / exe_name

    dist_root = resolve_path(project_root, args.dist_root)
    dist_dir = dist_root / args.dist_name
    zip_name = args.zip_name or f"{args.dist_name}_release.zip"
    zip_path = dist_root / zip_name

    windeployqt_required = (
        args.command in {"deploy", "all"} and not args.skip_windeployqt and not args.dry_run
    )

    windeployqt_path = resolve_windeployqt(
        project_root=project_root,
        explicit_path=args.windeployqt,
        qt_bin_dir=args.qt_bin_dir,
        skip_windeployqt=args.skip_windeployqt,
        required=windeployqt_required,
    )

    return ReleaseConfig(
        command=args.command,
        project_root=project_root,
        build_dir=build_dir,
        exe_name=exe_name,
        exe_path=exe_path,
        dist_root=dist_root,
        dist_dir=dist_dir,
        zip_path=zip_path,
        windeployqt_path=windeployqt_path,
        skip_windeployqt=args.skip_windeployqt,
        include_docs=args.include_docs,
        include_readme=args.include_readme,
        copy_cache_content=args.copy_cache_content,
        dry_run=args.dry_run,
        verbose=args.verbose,
    )


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        config = build_config(args)
        tool = ReleaseTool(config)

        if config.command == "deploy":
            tool.deploy()
        elif config.command == "verify":
            tool.verify()
        elif config.command == "package":
            tool.package()
        elif config.command == "all":
            tool.deploy()
            tool.package()
        else:
            parser.print_help()
            return 1
        return 0
    except Exception as exc:
        print(f"[ERROR] {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
