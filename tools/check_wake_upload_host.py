"""仅提取固件实际C++函数并以现有WSL g++编译运行主机mock，不构建固件或连接设备。"""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'firmware/assistant_espidf/main/control_protocol.cpp'
FUNCTIONS = ('cache_wake_audio','send_wake_audio_segment','upload_wake_audio','reset_wake_audio_cache','set_active_wifi_performance','service_active_wifi_performance','snapshot_wake_prefix','confirm_wake_audio_release','accept_wake_listening','service_wake_audio_reset','abort_wake_audio','upload_pending_wake_audio','note_wake_clip_audio','finish_wake_clip','queue_wake_audio','service_wake_audio_tx','send_capture_audio','send_control','send_wake_detect','begin_listening','service_manual_listening','capture_manual_audio','control_protocol_listen_start','control_protocol_listen_stop','note_wake_boundary_audio','accept_manual_input_ready')


def actual_function(source, name):
    declaration = re.search(r'^(?:static\s+)?(?:void|bool)\s+' + re.escape(name) + r'\s*\([^;]*?\)\s*\{', source, flags=re.MULTILINE)
    if declaration is None:
        raise ValueError('实际固件函数未找到：' + name)
    start = source.index('{', declaration.start())
    depth, index, state = 0, start, 'code'
    while index < len(source):
        character = source[index]
        following = source[index:index + 2]
        if state == 'line':
            if character == '\n': state = 'code'
        elif state == 'block':
            if following == '*/': state = 'code'; index += 1
        elif state in ('"', "'"):
            if character == '\\': index += 1
            elif character == state: state = 'code'
        elif following == '//': state = 'line'; index += 1
        elif following == '/*': state = 'block'; index += 1
        elif character in ('"', "'"): state = character
        elif character == '{': depth += 1
        elif character == '}':
            depth -= 1
            if depth == 0:
                return source[declaration.start():index + 1]
        index += 1
    raise ValueError('实际固件函数边界不完整：' + name)


def wsl_path(path):
    value = str(path.resolve()).replace('\\', '/')
    if len(value) < 3 or value[1:3] != ':/':
        raise ValueError('需要本机Windows盘路径')
    return '/mnt/' + value[0].lower() + value[2:]


def main():
    source_bytes = SOURCE.read_bytes()
    source = source_bytes.decode('utf-8-sig')
    extracted = '\n\n'.join(actual_function(source, name) for name in FUNCTIONS)
    harness = (ROOT / 'tests/wake_upload_host_check.cpp').read_text(encoding='utf-8-sig')
    marker = '// ACTUAL_WAKE_FUNCTIONS'
    if harness.count(marker) != 1:
        raise ValueError('主机harness注入位置必须唯一')
    temporary_root = ROOT / 'runtime/host-checks'
    temporary_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='wake-upload-', dir=temporary_root) as temporary:
        translation = Path(temporary) / 'actual_wake_upload_check.cpp'
        binary = Path(temporary) / 'actual_wake_upload_check'
        translation.write_text(harness.replace(marker, extracted), encoding='utf-8')
        compiler = ['wsl.exe','-d','Ubuntu-26.04','--exec','/usr/bin/g++','-std=c++17','-Wall','-Wextra','-O0',wsl_path(translation),'-o',wsl_path(binary)]
        compiled = subprocess.run(compiler, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
        if compiled.stdout: print(compiled.stdout, end='')
        if compiled.stderr: print(compiled.stderr, end='')
        report = {'source_sha256':hashlib.sha256(source_bytes).hexdigest(),'actual_function_names':list(FUNCTIONS),'compiler':'WSL Ubuntu-26.04 /usr/bin/g++','compile_exit':compiled.returncode,'test_exit':None,'device_used':False,'firmware_built':False}
        if compiled.returncode == 0:
            tested = subprocess.run(['wsl.exe','-d','Ubuntu-26.04','--exec',wsl_path(binary)], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=30)
            if tested.stdout: print(tested.stdout, end='')
            if tested.stderr: print(tested.stderr, end='')
            report['test_exit'] = tested.returncode
        print(json.dumps(report, ensure_ascii=False))
        return 0 if report['compile_exit'] == 0 and report['test_exit'] == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
