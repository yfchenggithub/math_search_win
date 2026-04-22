import argparse
import shutil
import subprocess
import sys
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED


# =========================================================
# 固定配置：按你当前项目路径写死
# =========================================================
PROJECT_ROOT = Path(r"D:\work\math_search_win")
APP_NAME = "math_search_win"

EXE_PATH = PROJECT_ROOT / r"out\build\msvc-release\Release\math_search_win.exe"
WINDEPLOYQT = Path(r"D:\Qt\6.11.0\msvc2022_64\bin\windeployqt.exe")

DATA_DIR = PROJECT_ROOT / "data"
CACHE_DIR = PROJECT_ROOT / "cache"
APP_RESOURCES_DIR = PROJECT_ROOT / "app_resources"
README_PATH = PROJECT_ROOT / "README.md"

DIST_ROOT = PROJECT_ROOT / "dist"
DIST_DIR = DIST_ROOT / APP_NAME
ZIP_PATH = DIST_ROOT / f"{APP_NAME}_release.zip"


def info(msg: str) -> None:
    print(f"[INFO] {msg}")


def warn(msg: str) -> None:
    print(f"[WARN] {msg}")


def error(msg: str) -> None:
    print(f"[ERROR] {msg}")


def ensure_exists(path: Path, label: str) -> None:
    if not path.exists():
        raise FileNotFoundError(f"{label} not found: {path}")


def remove_dir(path: Path) -> None:
    if path.exists():
        info(f"Removing old directory: {path}")
        shutil.rmtree(path)


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def copy_file(src: Path, dst: Path) -> None:
    ensure_exists(src, "File")
    ensure_dir(dst.parent)
    shutil.copy2(src, dst)
    info(f"Copied file: {src} -> {dst}")


def copy_dir(src: Path, dst: Path, required: bool = False) -> None:
    if not src.exists():
        if required:
            raise FileNotFoundError(f"Required directory not found: {src}")
        warn(f"Skip missing directory: {src}")
        return

    ensure_dir(dst)
    info(f"Copying directory: {src} -> {dst}")
    shutil.copytree(src, dst, dirs_exist_ok=True)


def run_command(cmd: list[str], cwd: Path | None = None) -> None:
    info("Running command:")
    print("       " + " ".join(f'"{c}"' if " " in c else c for c in cmd))

    result = subprocess.run(cmd, cwd=str(cwd) if cwd else None)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed with exit code {result.returncode}")


def zip_directory(src_dir: Path, zip_path: Path) -> None:
    if not src_dir.exists():
        raise FileNotFoundError(f"Dist directory not found: {src_dir}")

    if zip_path.exists():
        info(f"Removing old zip: {zip_path}")
        zip_path.unlink()

    info(f"Creating zip package: {zip_path}")
    with ZipFile(zip_path, "w", compression=ZIP_DEFLATED) as zf:
        for file_path in src_dir.rglob("*"):
            if file_path.is_file():
                arcname = file_path.relative_to(src_dir)
                zf.write(file_path, arcname)


def deploy() -> None:
    info(f"PROJECT_ROOT       = {PROJECT_ROOT}")
    info(f"EXE_PATH           = {EXE_PATH}")
    info(f"WINDEPLOYQT        = {WINDEPLOYQT}")
    info(f"DATA_DIR           = {DATA_DIR}")
    info(f"CACHE_DIR          = {CACHE_DIR}")
    info(f"APP_RESOURCES_DIR  = {APP_RESOURCES_DIR}")
    info(f"DIST_DIR           = {DIST_DIR}")

    ensure_exists(WINDEPLOYQT, "windeployqt.exe")
    ensure_exists(EXE_PATH, "Release exe")

    remove_dir(DIST_DIR)
    ensure_dir(DIST_DIR)

    # 1) 复制 exe
    copy_file(EXE_PATH, DIST_DIR / f"{APP_NAME}.exe")

    # 2) windeployqt 部署 Qt 依赖
    run_command(
        [
            str(WINDEPLOYQT),
            "--release",
            "--compiler-runtime",
            str(DIST_DIR / f"{APP_NAME}.exe"),
        ]
    )

    # 3) 复制项目运行时目录
    copy_dir(DATA_DIR, DIST_DIR / "data", required=False)
    copy_dir(CACHE_DIR, DIST_DIR / "cache", required=False)
    copy_dir(APP_RESOURCES_DIR, DIST_DIR / "app_resources", required=False)

    # 4) README
    if README_PATH.exists():
        copy_file(README_PATH, DIST_DIR / "README.md")
    else:
        warn(f"README not found, skip: {README_PATH}")

    info("Deployment completed successfully.")
    info(f"Dist directory: {DIST_DIR}")
    print()
    print("[CHECK] 请人工确认：")
    print(f"        1. {DIST_DIR / (APP_NAME + '.exe')}")
    print(f"        2. {DIST_DIR / 'platforms' / 'qwindows.dll'}")
    print(f"        3. {DIST_DIR / 'QtWebEngineProcess.exe'}")
    print(f"        4. {DIST_DIR / 'data'}")
    print(f"        5. {DIST_DIR / 'cache'}")
    print(f"        6. {DIST_DIR / 'app_resources'}")
    print(f"        7. {DIST_DIR / 'resources'}   (Qt WebEngine 自己的资源)")
    print()
    print("[CHECK] 再重点验证：")
    print("        - 搜索是否正常")
    print("        - 详情页是否白屏")
    print("        - KaTeX 公式是否正常渲染")
    print("        - 整个 dist 目录复制到其他位置后是否仍可运行")


def package() -> None:
    zip_directory(DIST_DIR, ZIP_PATH)
    info(f"Package created: {ZIP_PATH}")


def all_in_one() -> None:
    deploy()
    package()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Release deployment and packaging tool for math_search_win"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("deploy", help="Generate dist release directory")
    subparsers.add_parser("package", help="Create zip from dist directory")
    subparsers.add_parser("all", help="Run deploy + package")

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        if args.command == "deploy":
            deploy()
        elif args.command == "package":
            package()
        elif args.command == "all":
            all_in_one()
        else:
            parser.print_help()
            return 1
        return 0
    except Exception as exc:
        error(str(exc))
        return 1


if __name__ == "__main__":
    sys.exit(main())
