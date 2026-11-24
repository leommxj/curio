#!/usr/bin/env python3
"""Generate module registries, build source lists, and browser bundles."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULES = ROOT / 'firmware/modules'


def write(path, content):
    if not path.exists() or path.read_text() != content:
        path.write_text(content)


def register(output, name, header, type_name, symbols):
    lines = [f'#include "{header}"']
    lines += [f'extern const {type_name} {symbol};' for symbol in symbols]
    lines += [f'const {type_name} *const {name}s[] = {{']
    lines += [f'    &{symbol},' for symbol in symbols] or ['    NULL,']
    lines += ['};', f'const size_t {name}_count = {len(symbols)};']
    write(output, '\n'.join(lines) + '\n')


def generate(output=None):
    output = Path(output or ROOT / 'build/modules').resolve()
    output.mkdir(parents=True, exist_ok=True)
    registry = json.loads((MODULES / 'registry.json').read_text())
    groups = {}
    test_groups = {}
    inputs = [MODULES / 'registry.json', Path(__file__).resolve()]
    for plural, kind in [('widgets', 'widget'), ('alerts', 'alert')]:
        names = registry[plural]
        groups[plural] = [path for name in names for path in sorted((MODULES / plural / name).glob('*.c'))
                          if path.name not in ('view.c', 'device.c', 'test.c')]
        tested = [name for name in names if (MODULES / plural / name / 'test.c').exists()]
        test_file = output / f'{kind}_tests.c'
        declarations = [f'void test_{kind}_{name}(void);' for name in tested]
        dispatch = [f'void curio_{kind}_module_tests(void)', '{']
        dispatch += [f'    test_{kind}_{name}();' for name in tested]
        write(test_file, '\n'.join(declarations + dispatch + ['}']) + '\n')
        test_groups[kind] = [MODULES / plural / name / 'test.c' for name in tested] + [test_file]
        header = 'curio_layout.h' if kind == 'widget' else 'curio_alerts.h'
        register(output / f'{plural}.c', f'curio_{kind}_type', header, f'curio_{kind}_type_t',
                 [f'curio_{kind}_{name}' for name in names])
    views = [MODULES / 'widgets' / name / 'view.c' for name in registry['widgets']]
    register(output / 'widget_views.c', 'curio_widget_view', 'curio_widget_view.h', 'curio_widget_view_t',
             [f'curio_widget_{name}_view' for name in registry['widgets']])
    services = [name for name in registry['widgets'] if (MODULES / 'widgets' / name / 'device.c').exists()]
    register(output / 'widget_services.c', 'curio_widget_service', 'curio_widget_service.h', 'curio_widget_service_t',
             [f'curio_widget_{name}_service' for name in services])
    web = ROOT / 'firmware/main/web'
    js = ['const widgetTypes = {}, alertTypes = {};']
    css = [(web / 'style.css').read_text()]
    inputs += [web / 'app.js', web / 'style.css']
    for plural in ('widgets', 'alerts'):
        for name in registry[plural]:
            directory = MODULES / plural / name
            js.append((directory / 'web.js').read_text())
            inputs.append(directory / 'web.js')
            if (directory / 'style.css').exists():
                css.append((directory / 'style.css').read_text())
                inputs.append(directory / 'style.css')
    js.append((web / 'app.js').read_text())
    write(output / 'app.js', '\n'.join(js) + '\n')
    write(output / 'style.css', '\n'.join(css) + '\n')
    groups.update(common=[MODULES / 'config.c'], views=[MODULES / 'view.c', *views],
                  services=[MODULES / 'widgets' / name / 'device.c' for name in services])
    cmake = []
    for key, paths in groups.items():
        cmake.append('set(CURIO_MODULE_' + key.upper() + '\n' + '\n'.join(f'    "{p}"' for p in paths) + '\n)')
    cmake.append('set(CURIO_MODULE_INPUTS\n' + '\n'.join(f'    "{p}"' for p in inputs) + '\n)')
    write(output / 'modules.cmake', '\n'.join(cmake) + '\n')
    portable = groups['common'] + groups['widgets'] + groups['alerts'] + [output / 'widgets.c', output / 'alerts.c']
    make_lines = ['MODULE_SOURCES := ' + ' '.join(map(str, portable)),
                  'MODULE_VIEW_SOURCES := ' + ' '.join(map(str, groups['views'] + [output / 'widget_views.c']))]
    make_lines += [f'MODULE_{kind.upper()}_TEST_SOURCES := ' + ' '.join(map(str, paths)) for kind, paths in test_groups.items()]
    write(output / 'modules.mk', '\n'.join(make_lines) + '\n')
    return {'output': output, 'portable': portable, **groups}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    generate(parser.parse_args().output)
