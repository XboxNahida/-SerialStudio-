"""生成独立指令库 Release 的三个附件；不会上传或执行指令。"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import tempfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.compile(r'(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})(?:-(alpha|beta|rc)\.(0|[1-9][0-9]{0,8}))?')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def local_path(value):
    path = Path(value)
    path = (ROOT / path).resolve() if not path.is_absolute() else path.resolve()
    require(path.is_relative_to(ROOT) and path != ROOT, '路径必须位于本工程内')
    return path


def text(value, single=False):
    return (isinstance(value, str) and bool(value.strip())
            and len(value) <= (4096 if single else 32768)
            and all(ord(c) != 0 and (not single or (ord(c) >= 32 and ord(c) != 127 and c not in '\u2028\u2029')) for c in value))


def validate(entries):
    require(isinstance(entries, list) and 0 < len(entries) <= 20000, '指令列表必须为非空数组，最多 20000 条')
    ids = set()
    for entry in entries:
        require(isinstance(entry, dict), '条目必须为对象')
        system, identifier = entry.get('system'), entry.get('id')
        require(system in ('linux', 'powershell', 'cmd', 'android'), '未知系统')
        require(isinstance(identifier, str) and re.fullmatch(system + r'-[a-z0-9-]+', identifier)
                and identifier not in ids, f'无效或重复标识：{identifier}')
        ids.add(identifier)
        for field in ('category', 'title', 'description'):
            require(text(entry.get(field)), f'{identifier} 的 {field} 无效')
        for field in ('syntax', 'example'):
            require(text(entry.get(field), True), f'{identifier} 的 {field} 必须为单行文本')
        require(entry.get('interaction') in ('shell', 'editor-key'), '交互类型错误')
        if entry['interaction'] == 'editor-key':
            require(entry.get('key') in (':wq', ':q!', 'Esc', 'Ctrl+O', 'Ctrl+X'), '编辑器按键错误')
        for field in ('requires', 'usage', 'location', 'delivery'):
            if field in entry:
                require(text(entry[field]), f'{field} 必须为文本')
        if 'tags' in entry:
            require(isinstance(entry['tags'], list) and all(text(v) for v in entry['tags']), '标签无效')
        for field in ('parameters', 'options', 'advanced'):
            values = entry.get(field, [])
            require(isinstance(values, list), f'{field} 必须为数组')
            for part in values:
                require(isinstance(part, dict), f'{field} 元素必须为对象')
                if field == 'parameters':
                    index = part.get('index')
                    require(type(index) is int and 0 <= index <= 256 and text(part.get('label'))
                            and text(part.get('hint')) and isinstance(part.get('choices'), list), '参数说明错误')
                    require(all(isinstance(v, dict) and text(v.get('value'), True)
                                and text(v.get('description')) for v in part['choices']), '参数候选错误')
                elif field == 'options':
                    require(text(part.get('value'), True) and text(part.get('description')), '选项说明错误')
                else:
                    require(text(part.get('title')) and text(part.get('description'))
                            and text(part.get('command'), True), '高级示例错误')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True, help='指令库版本，例如 1.1.0；独立于 APP 版本')
    parser.add_argument('--minimum-app', default='1.3.0', help='支持外置库的最低 APP 版本')
    parser.add_argument('--source', default='data/terminal_commands.json', help='维护源数组 JSON')
    parser.add_argument('--output', help='新的工程内输出目录；拒绝覆盖已有目录')
    args = parser.parse_args()
    try:
        require(VERSION.fullmatch(args.version) and VERSION.fullmatch(args.minimum_app), '版本格式无效')
        source = local_path(args.source)
        output = local_path(args.output or f'build/catalog-release-v{args.version}')
        require(not output.exists(), '输出目录已存在，请使用新的目录')
        require(source.stat().st_size <= 16 * 1024 * 1024, '源文件超过 16 MiB')
        entries = json.loads(source.read_text(encoding='utf-8'))
        validate(entries)
        package = dict(schemaVersion=1, catalogVersion=args.version, minAppVersion=args.minimum_app, commands=entries)
        data = (json.dumps(package, ensure_ascii=False, indent=2, allow_nan=False) + '\n').encode('utf-8')
        require(len(data) <= 16 * 1024 * 1024, '发布文件超过 16 MiB')
        name = f'terminal-catalog-v{args.version}.json'
        digest = hashlib.sha256(data).hexdigest()
        manifest = dict(schemaVersion=1, catalogVersion=args.version, minAppVersion=args.minimum_app,
                        fileName=name, size=len(data), sha256=digest,
                        downloadUrl=f'https://github.com/XboxNahida/-SerialStudio-/releases/download/catalog-v{args.version}/{name}',
                        publishedAt=datetime.now(timezone.utc).isoformat().replace('+00:00', 'Z'))
        output.parent.mkdir(parents=True, exist_ok=True)
        # 在同一卷准备完整附件，最后重命名目录；失败不留下可被误发的半套文件。
        with tempfile.TemporaryDirectory(dir=output.parent, prefix='.catalog-stage-') as staging:
            assets = Path(staging) / 'assets'
            assets.mkdir()
            (assets / name).write_bytes(data)
            (assets / (name + '.sha256')).write_text(f'{digest}  {name}\n', encoding='utf-8')
            (assets / 'terminal-catalog-manifest.json').write_text(
                json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            assets.rename(output)
        print(f'已生成 {len(entries)} 条指令：{output}')
        print('请检查后手动发布 catalog-v 标签的独立 Release；本脚本没有上传。')
    except (ValueError, OSError, TypeError) as error:
        parser.exit(1, f'生成失败：{error}\n')


if __name__ == '__main__':
    main()
