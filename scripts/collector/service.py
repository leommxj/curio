#!/usr/bin/env python3
"""Install or remove the Linux systemd collector service."""
import argparse
import os
from pathlib import Path
import pwd
import shutil
import subprocess
import sys
import tempfile

SERVICE = 'curio-collector.service'


def run(args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def atomic_copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.curio-', dir=target.parent)
    try:
        with os.fdopen(fd, 'wb') as output:
            with source.open('rb') as incoming:
                shutil.copyfileobj(incoming, output)
            os.fchmod(output.fileno(), 0o755)
        os.replace(name, target)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def quote(value, expand_dollars=False):
    value = str(value)
    if any(c in value for c in '\r\n\0'):
        raise ValueError('Service paths cannot contain line breaks or NUL.')
    if expand_dollars:
        value = value.replace('$', '$$')
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%') + '"'


def service_paths(system, config_root):
    target = Path('/usr/local/bin/curio-collector') if system else Path.home() / '.local/bin/curio-collector'
    unit = Path('/etc/systemd/system') / SERVICE if system else config_root / 'systemd/user' / SERVICE
    return target, unit


def deploy(args, binary, config, config_root, account):
    system = args.system
    target, unit = service_paths(system, config_root)
    ctl = ['systemctl'] + ([] if system else ['--user'])
    if args.action == 'uninstall':
        if not unit.exists():
            print('Service not installed.')
            return
        run(ctl + ['disable', '--now', SERVICE])
        unit.unlink()
        run(ctl + ['daemon-reload'])
        return
    drop_in = unit.parent / (SERVICE + '.d')
    if drop_in.is_dir() and any(drop_in.glob('*.conf')):
        raise ValueError(f'Remove or review existing service overrides before installing: {drop_in}')
    other_unit = (config_root / 'systemd/user' / SERVICE) if system else Path('/etc/systemd/system') / SERVICE
    if other_unit.exists():
        raise ValueError(f'Uninstall the other service mode first: {other_unit}')
    run(ctl + ['show', '--property=Version'])
    runtime_home = account.pw_dir if system else str(Path.home())
    runtime_path = f'{runtime_home}/.local/bin:/usr/local/bin:/usr/bin:/bin'
    environment = [f'HOME={runtime_home}', f'XDG_CONFIG_HOME={config_root}', f'PATH={runtime_path}']
    if system:
        with tempfile.TemporaryDirectory(prefix='curio-check-') as staging:
            os.chmod(staging, 0o755)
            check_binary = Path(staging) / 'curio-collector'
            shutil.copyfile(binary, check_binary)
            check_binary.chmod(0o755)
            run(['runuser', '-u', account.pw_name, '--', 'env', *environment,
                 check_binary, 'check', '--config', config])
    else:
        run([binary, 'check', '--config', config])
    template = Path(__file__).with_name(SERVICE).read_text()
    if any(c in str(target) for c in '"\\'):
        raise ValueError('Systemd executable path cannot contain quotes or backslashes.')
    template = template.replace('Type=simple', 'Type=simple\n' +
                                '\n'.join('Environment=' + quote(value) for value in environment))
    template = template.replace('ExecStart=%h/.local/bin/curio-collector --config=%h/.config/curio/collector.yaml',
                                f'ExecStart={quote(target)} {quote("--config=" + str(config), expand_dollars=True)}')
    if system:
        template = template.replace('Type=simple', 'Type=simple\nUser=' + account.pw_name)
        template = template.replace('WantedBy=default.target', 'WantedBy=multi-user.target')
    atomic_copy(binary, target)
    unit.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.curio-', dir=unit.parent)
    try:
        with os.fdopen(fd, 'w') as output:
            output.write(template)
            os.fchmod(output.fileno(), 0o644)
        os.replace(name, unit)
    finally:
        if os.path.exists(name):
            os.unlink(name)
    run(ctl + ['daemon-reload'])
    loaded = subprocess.run(ctl + ['show', '--property=FragmentPath', '--value', SERVICE],
                            check=True, capture_output=True, text=True).stdout.strip()
    if Path(loaded) != unit:
        raise ValueError(f'Systemd loaded {loaded or "no unit"}; expected {unit}. Check the manager configuration directory.')
    run(ctl + ['enable', SERVICE])
    run(ctl + ['restart', SERVICE])
    run(ctl + ['is-active', '--quiet', SERVICE])


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if not argv or argv[0] not in ('install', 'uninstall'):
        raise ValueError('Choose install or uninstall.')
    action = argv[0]
    parser = argparse.ArgumentParser(prog=f'collector-service {action}',
                                     description='Linux systemd collector deployment. Default: --user.')
    parser.set_defaults(action=action)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--user', action='store_true')
    mode.add_argument('--system', action='store_true')

    installing = action == 'install'
    if installing:
        parser.add_argument('--binary', type=Path, required=True, help='Collector executable to install.')
        parser.add_argument('--config', type=Path, help='Existing YAML; prepare with collector init first.')
        parser.add_argument('--run-as', help='System service user; root must specify this.')
    args = parser.parse_args(argv[1:])
    if sys.platform != 'linux' or not shutil.which('systemctl'):
        raise ValueError('Linux systemd is required.')
    if not args.system and os.getuid() == 0:
        raise ValueError('Run --user as a non-root user.')
    account = pwd.getpwuid(os.getuid())
    config_root = Path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config').absolute()
    config = None
    if args.action == 'install':
        if args.run_as and not args.system:
            raise ValueError('--run-as requires --system.')
        if args.system:
            if not args.run_as and os.getuid() == 0:
                raise ValueError('Root must specify --run-as USER.')
            account = pwd.getpwnam(args.run_as) if args.run_as else account
            if account.pw_uid == 0:
                raise ValueError('--run-as must be a non-root user.')
            if account.pw_uid != os.getuid():
                config_root = Path(account.pw_dir) / '.config'
        config = (args.config or config_root / 'curio/collector.yaml').absolute()
    binary = args.binary.absolute() if installing else None
    if installing and (not binary.is_file() or not os.access(binary, os.X_OK)):
        raise ValueError(f'Collector binary is not executable: {binary}')
    if args.system and os.getuid() != 0:
        command = ['sudo', 'env', f'XDG_CONFIG_HOME={config_root}', sys.executable, Path(__file__).resolve(),
                   args.action, '--system']
        if installing:
            command[2:2] = [f'CURIO_DEPLOY_CONFIG_ROOT={config_root}']
            command += ['--binary', binary, '--run-as', account.pw_name, '--config', config]
        run(command)
    else:
        # sudo passes the selected configuration root explicitly.
        if installing and args.system and os.environ.get('CURIO_DEPLOY_CONFIG_ROOT'):
            config_root = Path(os.environ['CURIO_DEPLOY_CONFIG_ROOT']).absolute()
        deploy(args, binary, config, config_root, account)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
