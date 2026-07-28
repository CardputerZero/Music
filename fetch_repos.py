import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def load_repos():
    with (ROOT / "repos.json").open(encoding="utf-8") as stream:
        return json.load(stream)


def fetch_repo(repo, update_existing=True):
    path = ROOT / repo["path"]
    if not (path / ".git").exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", repo["url"], str(path)], check=True)
    elif update_existing:
        subprocess.run(
            ["git", "-C", str(path), "fetch", "--tags", "--prune"], check=True
        )

    ref = repo.get("branch")
    if ref:
        subprocess.run(["git", "-C", str(path), "checkout", ref], check=True)

    if repo.get("with_submodules", False):
        subprocess.run(
            [
                "git",
                "-C",
                str(path),
                "submodule",
                "update",
                "--init",
                "--recursive",
            ],
            check=True,
        )


def ensure_dependencies():
    missing = [repo for repo in load_repos() if not (ROOT / repo["path"] / ".git").exists()]
    for repo in missing:
        fetch_repo(repo, update_existing=False)


if __name__ == "__main__":
    for dependency in load_repos():
        fetch_repo(dependency)

