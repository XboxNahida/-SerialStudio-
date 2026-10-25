"""在工程临时目录验证独立发布资产，不连接网络。"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/生成指令库发布.py'


class 指令库发布验证(unittest.TestCase):
    def run_generator(self, output, *extra):
        return subprocess.run([sys.executable, str(SCRIPT), '--version', '1.1.0',
                               '--output', str(output), *extra], capture_output=True)

    def test_资产版本摘要与清单匹配(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as tmp:
            output = Path(tmp) / 'assets'
            result = self.run_generator(output)
            self.assertEqual(result.returncode, 0, result.stderr.decode('utf-8', errors='replace'))
            manifest = json.loads((output / 'terminal-catalog-manifest.json').read_text(encoding='utf-8'))
            data = (output / manifest['fileName']).read_bytes()
            library = json.loads(data)
            digest = hashlib.sha256(data).hexdigest()
            self.assertEqual(manifest['size'], len(data))
            self.assertEqual(manifest['sha256'], digest)
            self.assertEqual(library['catalogVersion'], '1.1.0')
            self.assertEqual(library['minAppVersion'], manifest['minAppVersion'])
            self.assertEqual(len(library['commands']), 589)
            self.assertEqual(manifest['downloadUrl'],
                             'https://github.com/XboxNahida/-SerialStudio-/releases/download/catalog-v1.1.0/' + manifest['fileName'])
            self.assertEqual((output / (manifest['fileName'] + '.sha256')).read_text().split()[0], digest)
            self.assertNotEqual(self.run_generator(output).returncode, 0, '拒绝覆盖已有发布目录')

    def test_坏源文件不生成半成品(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as tmp:
            tmp = Path(tmp)
            source = tmp / 'bad.json'
            entries = json.loads((ROOT / 'data/terminal_commands.json').read_text(encoding='utf-8'))
            entries.append(entries[0])
            source.write_text(json.dumps(entries), encoding='utf-8')
            self.assertNotEqual(self.run_generator(tmp / 'assets', '--source', str(source)).returncode, 0)
            self.assertFalse((tmp / 'assets').exists())

    def test_拒绝路径越界和无效版本(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as tmp:
            self.assertNotEqual(self.run_generator(Path(tmp) / 'assets', '--version', '../bad').returncode, 0)
            self.assertNotEqual(self.run_generator(ROOT.parent / 'forbidden-assets').returncode, 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
