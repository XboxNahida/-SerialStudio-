"""受管工具归档处理；仅处理校验后的 ZIP，不执行归档内程序。"""
import hashlib
import json
import pathlib
import re
import shutil
import stat
import sys
import zipfile

def safe_name(name):
    if not name or "\\" in name or name.startswith("/"):
        raise ValueError("归档包含非法路径")
    parts = name.rstrip("/").split("/")
    for part in parts:
        if (part in ("", ".", "..") or part[-1:] in (" ", ".")
            or any(ord(c) < 32 or c in ':*?"<>|' for c in part)
            or re.match(r"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)", part, re.I)):
            raise ValueError("归档包含 Windows 非法或歧义路径")
    return "/".join(parts)

def extract(archive, destination, component):
    destination = pathlib.Path(destination)
    if destination.exists():
        raise ValueError("解压目标必须是新目录")
    with zipfile.ZipFile(archive) as z:
        total = 0
        selected = []
        names = set()
        entries = z.infolist()
        if len(entries) > 20000:
            raise ValueError("归档文件过多")
        for info in entries:
            name = safe_name(info.filename)
            mode = info.external_attr >> 16
            if stat.S_ISLNK(mode) or (stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR)):
                raise ValueError("归档禁止符号链接和特殊文件")
            if info.is_dir():
                continue
            if name.casefold() in names or name.casefold() == ".serialstudio-files.json":
                raise ValueError("归档包含重复文件或保留清单")
            names.add(name.casefold())
            total += info.file_size
            if total > 1024 * 1024 * 1024:
                raise ValueError("解压大小超过 1 GiB")
            if not component.get("include") or name in component["include"]:
                selected.append((info, name))
        selected_names = {name for _, name in selected}
        if any(p not in selected_names for p in component.get("entrypoints", [])):
            raise ValueError("归档缺少必需程序")
        destination.mkdir(parents=True)
        receipt = []
        for info, name in selected:
            path = destination / name
            path.parent.mkdir(parents=True, exist_ok=True)
            digest = hashlib.sha256()
            with z.open(info) as source, path.open("xb") as target:
                while chunk := source.read(262144):
                    target.write(chunk)
                    digest.update(chunk)
            receipt.append({"path": name, "sha256": digest.hexdigest()})
        (destination / ".serialstudio-files.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2), encoding="utf-8")

if __name__ == "__main__":
    try:
        # 参数由应用以独立 argv 传入，不经 shell 解释。
        extract(sys.argv[1], sys.argv[2], json.loads(pathlib.Path(sys.argv[3]).read_text(encoding="utf-8")))
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
