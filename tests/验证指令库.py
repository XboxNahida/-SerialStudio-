"""静态检查指令数据及明确列出的常用任务；不会执行任何指令模板。"""
import collections
import hashlib
import json
from pathlib import Path
import re
import shlex
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIELDS = ('system', 'category', 'title', 'syntax', 'example', 'description')
MINIMUM = {'linux': 100, 'powershell': 80, 'cmd': 50, 'android': 80}


class 指令库验证(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.commands = json.loads((ROOT / 'data/terminal_commands.json').read_text(encoding='utf-8'))

    def test_系统分类及数量(self):
        counts = collections.Counter(c['system'] for c in self.commands)
        self.assertEqual(set(counts), set(MINIMUM))
        for system, minimum in MINIMUM.items():
            self.assertGreaterEqual(counts[system], minimum, system)
            self.assertGreaterEqual(len({c['category'] for c in self.commands if c['system'] == system}), 6)
        print('指令数：' + ', '.join(f'{s}={counts[s]}' for s in MINIMUM) + f', 总计={len(self.commands)}')

    def test_原有88条能力保留(self):
        original = [{k: c[k] for k in FIELDS} for c in self.commands[:88]]
        digest = hashlib.sha256(json.dumps(original, ensure_ascii=False, sort_keys=True).encode()).hexdigest()
        self.assertEqual(digest, '872a72f8cf0c257c09264f64a17101d51f8514116b1b379747e8a98e4171c8e1')

    def test_稳定标识字段与单行边界(self):
        ids = []
        for c in self.commands:
            with self.subTest(title=c['title']):
                for field in (*FIELDS, 'id', 'requires', 'interaction'):
                    self.assertIsInstance(c.get(field), str, field)
                    self.assertTrue(c[field].strip(), field)
                self.assertRegex(c['id'], rf"^{c['system']}-[a-z0-9-]+$")
                ids.append(c['id'])
                for field in ('syntax', 'example'):
                    self.assertFalse(any(ord(ch) < 32 or ord(ch) == 127 or ch in '\u2028\u2029' for ch in c[field]))
                self.assertIn(c['interaction'], ('shell', 'editor-key'))
                self.assertIsInstance(c.get('tags', []), list)
                self.assertTrue(all(isinstance(t, str) and t for t in c.get('tags', [])))
                if c['interaction'] == 'editor-key':
                    self.assertIn(c.get('key'), (':wq', ':q!', 'Esc', 'Ctrl+O', 'Ctrl+X'))
                    self.assertTrue(c.get('delivery'))
                    self.assertIn('编辑器', c['requires'])
                else:
                    self.assertNotIn(c['example'], (':wq', ':q!', 'Esc', 'Ctrl+X'))
        self.assertEqual(len(ids), len(set(ids)))
        signatures = [(c['system'], c['example']) for c in self.commands]
        self.assertEqual(len(signatures), len(set(signatures)), '不得重复示例凑数')

    def test_系统语法边界(self):
        for c in self.commands:
            ex = c['example']
            if c['system'] in ('linux', 'android') and c['interaction'] == 'shell':
                with self.subTest(id=c.get('id')):
                    self.assertTrue(shlex.split(ex), '检查 Shell 引号是否成对；不代替目标 Shell 执行验收')
            if c['system'] == 'cmd':
                self.assertNotIn('$env:', ex)
                self.assertNotRegex(ex, r'\b(Get|Set|New|Remove)-[A-Za-z]+')
            if c['system'] == 'powershell':
                self.assertNotIn('%TEMP%', ex)
                self.assertNotRegex(ex, r'\b(ping -n|dir /[asb]|cd /d)\b')
            if c['system'] == 'android':
                self.assertNotRegex(ex, r'^adb\s', '此库运行在设备 Shell 中')
                self.assertNotRegex(ex, r'\b(systemctl|apt-get|nmcli)\b')
                if ex.startswith('toybox '):
                    self.assertIn('toybox', c['requires'])

    def test_字典字段及高级语法(self):
        for c in self.commands:
            if c['interaction'] != 'shell':
                continue
            with self.subTest(id=c['id']):
                self.assertTrue(c.get('location'))
                self.assertTrue(c.get('usage'))
                for p in c['parameters']:
                    self.assertIsInstance(p['index'], int)
                    self.assertTrue(p['label'])
                    self.assertTrue(p['hint'])
                    self.assertTrue(all(v['value'] and v['description'] for v in p['choices']))
                for option in c['options']:
                    self.assertTrue(option['value'])
                    self.assertTrue(option['description'])
                for example in c['advanced']:
                    self.assertTrue(example['title'])
                    self.assertTrue(example['description'])
                    self.assertFalse(any(ord(ch) < 32 for ch in example['command']))
                    if c['system'] in ('linux', 'android'):
                        self.assertTrue(shlex.split(example['command']))
                    if c['system'] == 'android':
                        self.assertNotRegex(example['command'], r'^adb\s')

    def test_常用任务矩阵可追溯及覆盖率(self):
        path = ROOT / '指令任务覆盖矩阵.md'
        self.assertTrue(path.exists(), '需明确定义任务分母，不能以命令数量推算覆盖率')
        commands = {c.get('id'): c for c in self.commands}
        counts = collections.defaultdict(lambda: [0, 0])
        evidence = path.read_text(encoding='utf-8').split('## 逐项证据', 1)
        self.assertEqual(len(evidence), 2)
        for line in evidence[1].splitlines():
            cols = [x.strip() for x in line.split('|')]
            if len(cols) != 5 or cols[1] not in MINIMUM:
                continue
            system, task, refs = cols[1:4]
            self.assertTrue(task)
            counts[system][1] += 1
            if refs == '暂缺':
                continue
            ids = [x.strip(' `') for x in refs.split(',')]
            self.assertTrue(ids)
            for identifier in ids:
                self.assertTrue(identifier in commands, f'未知指令 ID：{identifier}')
                self.assertEqual(commands[identifier]['system'], system)
            counts[system][0] += 1
        self.assertEqual(set(counts), set(MINIMUM))
        for system, (covered, total) in counts.items():
            self.assertGreaterEqual(total, 25, '任务范围需要具体且足够广')
            self.assertGreaterEqual(covered / total, .85, system)
            print(f'任务覆盖：{system} {covered}/{total} = {covered / total:.1%}')


if __name__ == '__main__':
    unittest.main(verbosity=2)
